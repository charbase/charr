#ifndef CHARR_SHARED_PARALLEL_H
#define CHARR_SHARED_PARALLEL_H

#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif
#include <Rinternals.h>

#include "lint.h"

#include <cstddef>
#include <vector>

namespace charr {
namespace shared {

namespace parallel_detail {
class ChunkQueue;
struct WorkerReport;
}

/*
 * One worker's share of an operation's task range. A task is whatever the
 * operation splits: an output element for an elementwise map, a recycling
 * lane for a pattern-vectorized search. The driver only divides [0, tasks)
 * into contiguous chunks; the body decides what a task index means.
 *
 * The worker index is also the output shard index, so an operation writing
 * charvec output can hand it straight to a sharded builder.
 *
 * A worker draws chunks one at a time rather than receiving a fixed slice.
 * Element cost is not uniform -- one long string can outweigh a thousand
 * short ones -- and a fixed slice makes the whole region wait for whichever
 * worker drew the expensive part of the vector. Chunks are handed out in
 * ascending order, so a worker that finishes a cheap chunk comes back for the
 * next one and the tail is shared.
 *
 * Ascending order is also what preserves the failure rule. Every chunk below
 * the lowest failing chunk was necessarily claimed before it, so its outcome
 * is recorded, and the lowest recorded failure is the one a serial run would
 * have reached first.
 *
 * The worker index carries no order. Which worker holds which chunk depends
 * only on timing, so anything that must follow serial order -- the failure,
 * and the warnings raised before it -- is keyed by the chunk, never by the
 * worker. SerialWarnings below does that for warnings.
 */
class WorkerContext {
public:
    unsigned worker;
    unsigned workers;
    // The chunk this worker currently holds. Valid only after next_chunk()
    // has returned true.
    R_xlen_t begin;
    R_xlen_t end;

    // Claim the next chunk, or return false once the queue is drained. A
    // body's run() loops on this and never touches the queue otherwise.
    CHARR_CXX_HELPER bool next_chunk() noexcept;

    // A body that stops before the queue is drained must say so. Leaving
    // chunks unclaimed is legal -- the direct byte paths abandon their
    // attempt on the first element they cannot handle -- but it is only
    // legal deliberately, so the driver can still catch a body that forgot
    // to loop.
    CHARR_CXX_HELPER void stop_early() noexcept;

    // Where this worker is in serial order: the first task of the chunk it
    // holds, or -1 before its first claim, when it is still setting up and a
    // serial run would not yet have reached any task.
    CHARR_NEUTRAL_HELPER R_xlen_t serial_position() const noexcept
    {
        return holding_ ? begin : -1;
    }

    CHARR_NEUTRAL_HELPER WorkerContext(
        unsigned worker_index, unsigned worker_count,
        parallel_detail::ChunkQueue& queue,
        parallel_detail::WorkerReport& report
    ) noexcept
        : worker(worker_index), workers(worker_count), begin(0), end(0),
          queue_(&queue), report_(&report), holding_(false)
    {
    }

private:
    parallel_detail::ChunkQueue* queue_;
    parallel_detail::WorkerReport* report_;
    bool holding_;
};

/*
 * The kernel of a data-parallel operation, lifted out of the entry point's
 * unwind callback so that one body serves the serial and the threaded plan.
 *
 * run() carries the ordinary charr contract for a C++ helper: it may own
 * native state, allocate, and throw, and it may not call the R API. Worker
 * threads run with no R protection, no unwind boundary, and no way to enter
 * R, so a body that reaches R would corrupt the interpreter rather than
 * signal a condition.
 */
class ParallelBody {
public:
    // One body serves every worker, so run() may execute concurrently with
    // itself: it may read the body's members but must not write them. State a
    // worker mutates is a local of run(); state that outlives the region is
    // an owner in the entry point's Frame that run() reaches through a
    // reference and writes only at its own task indices.
    //
    // run() is called once per worker, not once per chunk, so resources a
    // worker owns -- a matcher, a collator, a break iterator -- are built
    // once and reused across every chunk that worker draws. The body's own
    // loop is therefore two deep:
    //
    //     Matcher matcher;
    //     while (context.next_chunk()) {
    //         for (R_xlen_t i = context.begin; i < context.end; ++i) { ... }
    //     }
    //
    // A warning is not raised here, since that is an R call. run() counts it
    // in a SerialWarnings the entry point owns, and the entry point raises
    // what that reports once the region is over.
    CHARR_CXX_HELPER virtual void run(WorkerContext& context) = 0;

    // Called from inside the driver's worker handler while the failing
    // exception is active. Backends override it to name their own exception
    // types; the shared implementation covers std::exception and unknown
    // exceptions.
    CHARR_CXX_HELPER virtual void describe_error(
        char* message, std::size_t size
    ) noexcept;

protected:
    // Bodies are operation-local values, never deleted through this base, so
    // the destructor stays non-virtual and trivial. That keeps a body out of
    // the entry point's Frame: it owns nothing that needs cleanup.
    //
    // The default constructor is declared rather than left implicit because
    // installing a vtable is not a trivial special member, and the linter
    // classifies only what a role annotation names.
    CHARR_NEUTRAL_HELPER ParallelBody() noexcept = default;
    ~ParallelBody() = default;
};


struct ParallelPlan {
    unsigned workers;
    // Tasks per chunk. Decided once so the driver never recomputes it
    // against a worker count the plan already adjusted.
    R_xlen_t chunk;
};


/*
 * Warnings of one kind that a body raises on workers, counted in the order a
 * serial run would have raised them.
 *
 * On success every warning is raised, so the count is the total. On failure
 * a serial run raises only the warnings of the tasks it reached, which are
 * the tasks below the failing one plus whatever the failing task raised
 * before it threw. A worker's index says nothing about which tasks those
 * are, so each warning is counted against the chunk its worker held:
 *
 *   - a chunk below the lowest failing chunk was claimed before it and ran
 *     to completion, so all of its warnings belong;
 *   - the failing chunk ran in order on one worker up to the throw, so its
 *     warnings are exactly those a serial run raised in that chunk;
 *   - a chunk above it is past the failure and none of its warnings belong.
 *
 * A warning raised before the worker's first claim -- while it builds its
 * matcher or iterator -- precedes every task and always belongs.
 *
 * The owner lives in the entry point's Frame. run_parallel_with_warnings()
 * sizes it on the main thread and marks the failure point before it
 * rethrows, so add() needs no allocation or synchronization: each chunk has
 * one writer. count() is read on the main thread once the driver has
 * returned or thrown.
 */
class CHARR_OWNER_TYPE SerialWarnings {
public:
    CHARR_CXX_HELPER SerialWarnings() noexcept;

    SerialWarnings(const SerialWarnings&) = delete;
    SerialWarnings& operator=(const SerialWarnings&) = delete;
    SerialWarnings(SerialWarnings&&) = delete;
    SerialWarnings& operator=(SerialWarnings&&) = delete;

    // Worker side: count `number` warnings at the worker's serial position.
    CHARR_NEUTRAL_HELPER void add(
        const WorkerContext& context, R_xlen_t number = 1
    ) noexcept;

    // Main thread, after the region: how many warnings a serial run raised.
    CHARR_NEUTRAL_HELPER R_xlen_t count() const noexcept;

    // Driver side only. reset() runs before any worker starts; fail_at()
    // takes the serial position of the lowest failure after the join.
    CHARR_CXX_HELPER void reset(
        unsigned workers, R_xlen_t tasks, R_xlen_t chunk
    );
    CHARR_NEUTRAL_HELPER void fail_at(R_xlen_t position) noexcept;

private:
    std::vector<R_xlen_t> setup_;
    std::vector<R_xlen_t> chunks_;
    R_xlen_t chunk_;
    // Chunks that count: all of them, unless a threaded run failed.
    R_xlen_t counted_;
};


/*
 * The three settings that tune threading. They live here rather than in R
 * options because threading happens only in native code: an operation never
 * needs to know the thread count on the R side, so making R carry it to the
 * boundary only added an argument to every entry point.
 *
 *   charr_threads()            how many threads an eligible operation may use
 *   charr_chunks_per_worker()  how many pieces of work each thread should get
 *   charr_min_chunk()          never cut a piece smaller than this
 *
 * Each accessor validates in R and then calls its entry point here, which
 * returns the previous value and stores the new one when `value` is not NULL.
 * They are written from the R thread and read on the R thread, before any
 * worker exists, so plain statics are enough.
 */
CHARR_R_HELPER SEXP C_charr_threads(SEXP value) noexcept;
CHARR_R_HELPER SEXP C_charr_chunks_per_worker(SEXP value) noexcept;
CHARR_R_HELPER SEXP C_charr_min_chunk(SEXP value) noexcept;


/*
 * Select the worker count and the chunk size from the current settings.
 * `eligible` records any semantic condition that requires serial execution.
 *
 * Neither chunking setting decides whether threads are used, only how the
 * work is divided once they are, so neither is a profitability gate.
 *
 * The worker count is capped by the task count and then by the number of
 * chunks the cut actually produces: starting a worker that cannot draw a
 * chunk costs a thread and buys nothing.
 *
 * Reads nothing from R, so an operation that discovers a smaller task count
 * part way through can call it again from a helper that must not touch R.
 */
CHARR_NEUTRAL_HELPER ParallelPlan parallel_plan(
    bool eligible, R_xlen_t tasks
) noexcept;


/*
 * Run one body over [0, tasks). A serial plan calls run() on this thread with
 * the whole range and lets its exception propagate. A threaded plan draws
 * chunks here and on workers, joins unconditionally, and then re-raises the
 * failure belonging to the lowest task index as a std::runtime_error carrying
 * the original message.
 *
 * run_parallel_with_warnings() also prepares `warnings` for the body and,
 * when a threaded run fails, limits it to what a serial run would have
 * raised. After it returns or throws, warnings.count() is the serial count.
 * It has its own name rather than overloading run_parallel(), so a call from
 * a template with a dependent body still resolves to one function.
 */
CHARR_CXX_HELPER void run_parallel(
    const ParallelPlan& plan, R_xlen_t tasks, ParallelBody& body
);
CHARR_CXX_HELPER void run_parallel_with_warnings(
    const ParallelPlan& plan, R_xlen_t tasks, ParallelBody& body,
    SerialWarnings& warnings
);

} // namespace shared
} // namespace charr

#endif
