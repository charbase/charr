#include "parallel.h"

#include <atomic>
#include <climits>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <thread>
#include <vector>

namespace charr {
namespace shared {

namespace parallel_detail {

// Wide enough that a threaded failure carries the same text a serial one
// would: EntryErrorState and StriException both hold 4096 bytes.
constexpr std::size_t message_size = 4096;
constexpr unsigned worker_maximum = 256;

/*
 * How finely the task range is cut. Chunks exist to balance uneven work, so
 * the count scales with the worker count rather than being fixed: the ratio
 * of chunks to workers is what decides how well a heavy tail is shared, and
 * it stays the same however many workers there are.
 *
 * 128 per worker was measured, not guessed. On 200,000 rows where 1% carry
 * 200x the bytes, four threads reach 1.90x at 32 per worker and 3.74x at 128,
 * against 3.83x on the same rows shuffled; 512 adds nothing. The cheap
 * kernels that pay the claim without needing the balance -- str_length and
 * fixed count over a million rows -- do not move across that range.
 *
 * The floor keeps a chunk long enough to be worth entering. Claiming one is a
 * single relaxed fetch_add, so at 256 elements the claim is already lost in
 * the loop that follows it. Neither number decides whether threads are used,
 * only how the work is cut once they are, so neither is a profitability gate.
 *
 * No heuristic survives every distribution. Both are therefore overridable
 * from the environment for a workload whose shape defeats the default.
 */
constexpr R_xlen_t chunks_per_worker = 128;
constexpr R_xlen_t minimum_chunk_tasks = 256;

// The largest whole number a double represents exactly, which bounds what an
// R numeric can carry into a setting without silently rounding.
constexpr double count_maximum = 4503599627370496.0;

/*
 * The live settings. Written from R by the three accessors, read by
 * parallel_plan on the same thread before any worker starts, so no
 * synchronization is needed and none is implied.
 */
int threads_setting = 1;
R_xlen_t chunks_per_worker_setting = chunks_per_worker;
R_xlen_t minimum_chunk_setting = minimum_chunk_tasks;

/*
 * One worker's outcome. The message is a fixed buffer rather than an
 * exception_ptr: an exception object must not outlive the thread that raised
 * it, and a worker failure only ever becomes an R condition, which carries no
 * C++ type. This is the same trade charport documents for its own boundary.
 *
 * `failed_at` is the first task of the chunk the worker held when it threw,
 * which is what orders failures now that a worker's chunks are not one
 * contiguous slice.
 */
struct WorkerReport {
    bool failed;
    bool stopped_early;
    R_xlen_t failed_at;
    char message[message_size];
};


/*
 * The shared cursor workers draw chunks from. Ascending order is load-bearing
 * twice over: it keeps the failure rule exact, and it means the chunks left
 * unclaimed when a body stops early are always the highest ones.
 */
class ChunkQueue {
public:
    CHARR_NEUTRAL_HELPER ChunkQueue(R_xlen_t tasks, R_xlen_t chunk) noexcept
        : next_(0), tasks_(tasks), chunk_(chunk < 1 ? 1 : chunk)
    {
    }

    CHARR_CXX_HELPER bool claim(R_xlen_t& begin, R_xlen_t& end) noexcept
    {
        const R_xlen_t first = next_.fetch_add(
            chunk_, std::memory_order_relaxed
        );
        if (first >= tasks_)
            return false;
        begin = first;
        end = tasks_-first < chunk_ ? tasks_ : first+chunk_;
        return true;
    }

    // Only meaningful after every worker has been joined.
    CHARR_NEUTRAL_HELPER bool drained() const noexcept
    {
        return next_.load(std::memory_order_relaxed) >= tasks_;
    }

private:
    std::atomic<R_xlen_t> next_;
    R_xlen_t tasks_;
    R_xlen_t chunk_;
};


/*
 * Read a settings scalar. The R accessor has already rejected anything that is
 * not a single positive whole number, so the checks here exist only for a
 * direct .Call from package code: an unusable value leaves the setting alone
 * rather than failing, because it is a tuning knob.
 */
CHARR_R_HELPER bool setting_value(SEXP value, R_xlen_t& out) noexcept
{
    if (XLENGTH(value) != 1)
        return false;

    double number;
    if (TYPEOF(value) == INTSXP) {
        const int stored = INTEGER_RO(value)[0];
        if (stored == NA_INTEGER)
            return false;
        number = static_cast<double>(stored);
    }
    else if (TYPEOF(value) == REALSXP) {
        number = REAL_RO(value)[0];
        if (number != number)
            return false;
    }
    else {
        return false;
    }

    if (number < 1)
        return false;
    out = static_cast<R_xlen_t>(
        number > count_maximum ? count_maximum : number
    );
    return true;
}


// A count as R sees it: integer while one fits, otherwise a double, which is
// what the accessors return and therefore what they accept back.
CHARR_R_HELPER SEXP setting_scalar(R_xlen_t value) noexcept
{
    if (value > static_cast<R_xlen_t>(INT_MAX))
        return Rf_ScalarReal(static_cast<double>(value));
    return Rf_ScalarInteger(static_cast<int>(value));
}


// Build the answer before storing, so an allocation failure leaves the
// setting where it was rather than applying a change R never learns about.
CHARR_R_HELPER SEXP exchange_setting(R_xlen_t& setting, SEXP value) noexcept
{
    SEXP previous = setting_scalar(setting);
    R_xlen_t requested;
    if (value != R_NilValue && setting_value(value, requested))
        setting = requested;
    return previous;
}

CHARR_NEUTRAL_HELPER R_xlen_t chunk_tasks(
    R_xlen_t tasks, unsigned workers, R_xlen_t per_worker, R_xlen_t floor_tasks
) noexcept {
    const R_xlen_t targets = static_cast<R_xlen_t>(workers)*per_worker;
    const R_xlen_t even = tasks/targets + (tasks % targets != 0 ? 1 : 0);
    R_xlen_t chunk = even > floor_tasks ? even : floor_tasks;

    // The floor may coarsen the cut but must never take it below one chunk
    // per worker: a short vector would otherwise fall into a single chunk and
    // run on one thread, which is worse than the fixed slice this replaced.
    const R_xlen_t count = static_cast<R_xlen_t>(workers);
    const R_xlen_t widest = tasks/count + (tasks % count != 0 ? 1 : 0);
    if (chunk > widest)
        chunk = widest;
    return chunk < 1 ? 1 : chunk;
}


CHARR_CXX_HELPER void run_guarded(
    ParallelBody& body, WorkerContext& context, WorkerReport& report
) noexcept {
    try {
        body.run(context);
    }
    catch (...) {
        report.failed = true;
        report.failed_at = context.begin;
        body.describe_error(report.message, message_size);
    }
}


/*
 * The one callable std::thread is instantiated with. Keeping the thread type
 * closed over a single named type keeps one row in the external effect
 * manifest instead of one per parallel operation, and keeps every use of
 * <thread> inside this translation unit.
 */
class WorkerEntry {
public:
    CHARR_NEUTRAL_HELPER WorkerEntry(
        ParallelBody& body, WorkerContext& context, WorkerReport& report
    ) noexcept
        : body_(&body), context_(&context), report_(&report)
    {
    }

    CHARR_CXX_HELPER void operator()() const noexcept
    {
        run_guarded(*body_, *context_, *report_);
    }

private:
    ParallelBody* body_;
    WorkerContext* context_;
    WorkerReport* report_;
};


CHARR_CXX_HELPER bool spawn_worker(
    std::vector<std::thread>& threads, ParallelBody& body,
    WorkerContext& context, WorkerReport& report
) noexcept {
    try {
        threads.emplace_back(WorkerEntry(body, context, report));
        return true;
    }
    catch (...) {
        // Thread creation is the one resource here that a healthy process can
        // still refuse. Report the failure and let the caller run the chunk
        // inline rather than failing an operation that serial code completes.
        return false;
    }
}


// A failed join leaves a thread whose destructor terminates anyway, so there
// is no recoverable state to return to.
CHARR_CXX_HELPER void join_all(
    std::vector<std::thread>& threads
) noexcept {
    for (std::vector<std::thread>::iterator it = threads.begin();
            it != threads.end(); ++it) {
        if (it->joinable())
            it->join();
    }
}


CHARR_NEUTRAL_HELPER unsigned clamp_workers(
    R_xlen_t tasks, int requested
) noexcept {
    if (requested < 2 || tasks < 2)
        return 1;

    R_xlen_t limit = tasks;
    if (limit > static_cast<R_xlen_t>(requested))
        limit = static_cast<R_xlen_t>(requested);
    if (limit > static_cast<R_xlen_t>(worker_maximum))
        limit = static_cast<R_xlen_t>(worker_maximum);
    return limit < 2 ? 1U : static_cast<unsigned>(limit);
}


} // namespace parallel_detail


CHARR_R_HELPER SEXP C_charr_threads(SEXP value) noexcept
{
    SEXP previous = Rf_ScalarInteger(parallel_detail::threads_setting);
    R_xlen_t requested;
    if (value != R_NilValue &&
            parallel_detail::setting_value(value, requested)) {
        const R_xlen_t limit =
            static_cast<R_xlen_t>(parallel_detail::worker_maximum);
        parallel_detail::threads_setting =
            static_cast<int>(requested > limit ? limit : requested);
    }
    return previous;
}


CHARR_R_HELPER SEXP C_charr_chunks_per_worker(SEXP value) noexcept
{
    return parallel_detail::exchange_setting(
        parallel_detail::chunks_per_worker_setting, value
    );
}


CHARR_R_HELPER SEXP C_charr_min_chunk(SEXP value) noexcept
{
    return parallel_detail::exchange_setting(
        parallel_detail::minimum_chunk_setting, value
    );
}


CHARR_CXX_HELPER bool WorkerContext::next_chunk() noexcept
{
    return queue_->claim(begin, end);
}


CHARR_CXX_HELPER void WorkerContext::stop_early() noexcept
{
    report_->stopped_early = true;
}


CHARR_CXX_HELPER void ParallelBody::describe_error(
    char* message, std::size_t size
) noexcept {
    // Precondition: an exception is being handled. The driver is the only
    // caller and calls this from its worker handler.
    try {
        throw;
    }
    catch (const std::exception& error) {
        std::snprintf(message, size, "%s", error.what());
    }
    catch (...) {
        std::snprintf(message, size, "unknown C++ exception");
    }
}


CHARR_NEUTRAL_HELPER ParallelPlan parallel_plan(
    bool eligible, R_xlen_t tasks
) noexcept {
    ParallelPlan plan;
    plan.workers = 1;
    // A serial plan is one chunk covering everything, so a body sees the same
    // loop shape either way. An empty range claims nothing regardless.
    plan.chunk = tasks > 0 ? tasks : 1;
    if (!eligible)
        return plan;

    plan.workers = parallel_detail::clamp_workers(
        tasks, parallel_detail::threads_setting
    );
    if (plan.workers < 2)
        return plan;

    // Past here tasks >= 2, so the chunk count is at least one and the cap
    // below cannot drive the worker count to zero.
    plan.chunk = parallel_detail::chunk_tasks(
        tasks, plan.workers,
        parallel_detail::chunks_per_worker_setting,
        parallel_detail::minimum_chunk_setting
    );
    const R_xlen_t chunks = tasks/plan.chunk +
        (tasks % plan.chunk != 0 ? 1 : 0);
    if (chunks < static_cast<R_xlen_t>(plan.workers))
        plan.workers = static_cast<unsigned>(chunks);
    return plan;
}


CHARR_CXX_HELPER void run_parallel(
    const ParallelPlan& plan, R_xlen_t tasks, ParallelBody& body
) {
    const unsigned workers = plan.workers < 2 || tasks < 2 ? 1 : plan.workers;

    // A serial plan takes the same path with one worker and one chunk, so a
    // body only ever sees the one loop shape.
    parallel_detail::ChunkQueue queue(
        tasks, plan.chunk
    );
    std::vector<parallel_detail::WorkerReport> reports(workers);
    std::vector<WorkerContext> contexts;
    contexts.reserve(static_cast<std::size_t>(workers));
    for (unsigned worker = 0; worker < workers; ++worker) {
        contexts.push_back(
            WorkerContext(worker, workers, queue, reports[worker])
        );
    }

    if (workers < 2) {
        // Serial: let the exception propagate as it always has.
        body.run(contexts[0]);
        return;
    }

    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(workers - 1));

    unsigned started = 1;
    while (started < workers &&
            parallel_detail::spawn_worker(
                threads, body, contexts[started], reports[started])) {
        ++started;
    }

    // This thread draws chunks too, and also runs any worker that could not
    // be started. Every worker goes through the same handler, so the failure
    // of the calling thread cannot skip the join.
    parallel_detail::run_guarded(body, contexts[0], reports[0]);
    for (unsigned worker = started; worker < workers; ++worker)
        parallel_detail::run_guarded(body, contexts[worker], reports[worker]);

    parallel_detail::join_all(threads);

    // Chunks are handed out in ascending order, so every chunk below the
    // lowest failing one was claimed before it and its outcome is recorded.
    // The lowest recorded failure is therefore the one a serial run would
    // have reached first.
    const parallel_detail::WorkerReport* first = nullptr;
    bool stopped_early = false;
    for (unsigned worker = 0; worker < workers; ++worker) {
        const parallel_detail::WorkerReport& report = reports[worker];
        stopped_early = stopped_early || report.stopped_early;
        if (report.failed &&
                (first == nullptr || report.failed_at < first->failed_at)) {
            first = &report;
        }
    }
    if (first != nullptr)
        throw std::runtime_error(first->message);

    // Nothing failed and nothing abandoned its attempt, so every chunk must
    // have been claimed. A body that returned without looping would leave
    // part of the output unwritten, which is silent; this makes it loud.
    if (!stopped_early && !queue.drained()) {
        throw std::logic_error(
            "parallel body returned before its chunks were drained"
        );
    }
}


} // namespace shared
} // namespace charr
