// Derived from stringi.
// Copyright (c) 2013-2025, Marek Gagolewski. See inst/COPYRIGHTS.

#include "ci_stringi.h"
#include "ci_parallel.h"
#include "io/reader_utils.h"
#include "fixed/options.h"
#include "io/string_view.h"
#include "io/utf8_output.h"
#include "../shared/entrypoint.h"
#include "../shared/fixed_search.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <charport.h>

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <vector>

namespace charr { namespace altrep_backend {

namespace search_fixed_extract_first {

CHARR_NEUTRAL_HELPER R_len_t recycling_length(
    R_len_t subject_length, R_len_t pattern_length,
    bool& warning
) noexcept
{
    warning = false;
    if (subject_length <= 0 || pattern_length <= 0)
        return 0;

    const R_len_t result = subject_length > pattern_length
        ? subject_length
        : pattern_length;
    warning = result % subject_length != 0 ||
        result % pattern_length != 0;
    return result;
}


CHARR_NEUTRAL_HELPER inline bool direct_view(
    const charport::StrView& value,
    charport::StrView& output, bool& modified
) noexcept
{
    modified = false;
    if (value.is_na()) {
        output = value;
        return true;
    }

    if (value.enc != CETYPE_EXT_ASCII &&
            value.enc != CETYPE_EXT_UTF8 &&
            value.enc != CETYPE_EXT_ASCII_OR_UTF8) {
        return false;
    }
    if (value.len < 0 || value.ptr == nullptr)
        return false;

    output = value;
    if (output.enc != CETYPE_EXT_ASCII &&
            output.len >= 3 &&
            static_cast<unsigned char>(output.ptr[0]) == 0xefU &&
            static_cast<unsigned char>(output.ptr[1]) == 0xbbU &&
            static_cast<unsigned char>(output.ptr[2]) == 0xbfU) {
        output.ptr += 3;
        output.len -= 3;
        modified = true;
    }
    return true;
}


CHARR_NEUTRAL_HELPER inline charport::StrView matched_output_view(
    const char* data, int length
) noexcept
{
    cetype_ext_t encoding = CETYPE_EXT_ASCII;
    for (int i = 0; i < length; ++i) {
        if (static_cast<unsigned char>(data[i]) > 0x7fU) {
            encoding = CETYPE_EXT_UTF8;
            break;
        }
    }
    return charport::StrView{data, length, encoding};
}


/*
 * The recycling shape of one direct attempt. Resolving it once keeps the
 * element kernel free of the vector lengths, so the same kernel serves the
 * serial driver and the parallel body.
 */
struct ExtractDirectState {
    R_xlen_t subject_size;
    R_xlen_t pattern_size;
    bool direct_subjects;
    bool direct_patterns;
};


// Everything the direct path can decide before looking at an element.
CHARR_NEUTRAL_HELPER bool extract_direct_eligible(
    const charport::StrViews& subjects,
    const charport::StrViews& patterns,
    shared::FixedSearchOptions options,
    R_len_t vectorize_length,
    ExtractDirectState& state
) noexcept
{
    if (options.case_insensitive || options.overlap ||
            vectorize_length <= 0 ||
            subjects.size() <= 0 || patterns.size() <= 0) {
        return false;
    }

    state.subject_size = subjects.size();
    state.pattern_size = patterns.size();
    state.direct_subjects = subjects.size() == vectorize_length;
    state.direct_patterns = patterns.size() == vectorize_length;
    return true;
}


/*
 * One output element of the direct path, written through a sink that reaches
 * either builder, so that the serial and the threaded run share one kernel.
 * Returns false when this element is not representable by the byte path,
 * having written nothing. Both operands are derived from the element index
 * alone, and the body touches no R API and no R allocation, so a worker may
 * run it.
 */
CHARR_CXX_HELPER bool extract_direct_one(
    const charport::StrViews& subjects,
    const charport::StrViews& patterns,
    const ExtractDirectState& state,
    R_len_t i,
    io::OutputSink& sink
)
{
    charport::StrView pattern;
    bool pattern_modified = false;
    if (!direct_view(
            patterns[state.direct_patterns
                ? static_cast<R_xlen_t>(i)
                : static_cast<R_xlen_t>(i) % state.pattern_size],
            pattern, pattern_modified
        ) || pattern_modified ||
            (!pattern.is_na() && pattern.len <= 0)) {
        return false;
    }

    charport::StrView subject;
    bool subject_modified = false;
    if (!direct_view(
            subjects[state.direct_subjects
                ? static_cast<R_xlen_t>(i)
                : static_cast<R_xlen_t>(i) % state.subject_size],
            subject, subject_modified
        )) {
        return false;
    }

    if (subject.is_na() || pattern.is_na()) {
        sink.set_na(i);
        return true;
    }

    const int match = shared::find_first_exact_bytes(
        subject.ptr, subject.len, pattern.ptr, pattern.len
    );
    if (match < 0) {
        sink.set_na(i);
    }
    else {
        sink.set_validated(
            i, matched_output_view(pattern.ptr, pattern.len)
        );
    }
    return true;
}


CHARR_CXX_HELPER bool extract_direct(
    const charport::StrViews& subjects,
    const charport::StrViews& patterns,
    shared::FixedSearchOptions options,
    R_len_t vectorize_length,
    io::OutputBuilder& output
)
{
    ExtractDirectState state;
    if (!extract_direct_eligible(
            subjects, patterns, options, vectorize_length, state)) {
        return false;
    }

    io::OutputSink sink(output);
    for (R_len_t i = 0; i < vectorize_length; ++i) {
        if (!extract_direct_one(subjects, patterns, state, i, sink))
            return false;
    }
    return true;
}


/*
 * The lowest element index any worker refused, or `none` when the direct
 * attempt covered every element. Chunks are contiguous and ordered, so the
 * lowest refusal is the one a serial scan would have reached first: below it
 * every element was written by the direct kernel, and at or above it every
 * element is either unwritten or rewritten, because this operation abandons
 * the whole direct attempt and rebuilds the output from index 0. The minimum
 * therefore reports exactly what the serial scan would have reported.
 */
CHARR_NEUTRAL_HELPER R_len_t lowest_ineligible(
    const std::vector<R_len_t>& slots, R_len_t none
) noexcept
{
    R_len_t result = none;
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (slots[i] < result)
            result = slots[i];
    }
    return result;
}


CHARR_CXX_HELPER void normalize_views(
    const charport::StrViews& input,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    std::vector<shared::StringView>& output
)
{
    output.resize(static_cast<std::size_t>(input.size()));
    for (R_xlen_t i = 0; i < input.size(); ++i) {
        output[static_cast<std::size_t>(i)] = shared::normalize_utf8(
            io::as_shared_view(input[i]), converter, storage
        );
    }
}


CHARR_NEUTRAL_HELPER R_len_t count_empty_patterns(
    const std::vector<shared::StringView>& patterns
) noexcept
{
    R_len_t result = 0;
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        const shared::StringView& pattern = patterns[i];
        if (!pattern.is_na() && pattern.len <= 0)
            ++result;
    }
    return result;
}


CHARR_R_HELPER void emit_empty_pattern_warnings(
    R_len_t count
) noexcept
{
    for (R_len_t i = 0; i < count; ++i)
        Rf_warning(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
}


/*
 * The direct byte path on worker threads. A task is one output element, not
 * one recycling lane as in Body below: the direct kernel derives both of its
 * operands from the element index, so the element range is the natural split
 * and it keeps the chunks contiguous in output order. The plan carries only
 * the worker count; the task basis belongs to the run_parallel call.
 */
class DirectBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER DirectBody(
        const charport::StrViews& subjects,
        const charport::StrViews& patterns,
        const ExtractDirectState& state,
        std::vector<R_len_t>& first_ineligible,
        io::ParallelOutputBuilder& output
    ) noexcept
        : subjects_(subjects), patterns_(patterns), state_(state),
          first_ineligible_(first_ineligible), output_(output)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        io::OutputSink sink(output_, context.worker);
        while (context.next_chunk()) {
            for (R_xlen_t task = context.begin;
                    task < context.end; ++task) {
                const R_len_t i = static_cast<R_len_t>(task);
                if (!extract_direct_one(
                        subjects_, patterns_, state_, i, sink)) {
                    // Claim no further chunks and leave the rest unwritten.
                    // The caller discards every shard before the general
                    // path runs.
                    first_ineligible_[context.worker] = i;
                    context.stop_early();
                    return;
                }
            }
        }
    }

private:
    const charport::StrViews& subjects_;
    const charport::StrViews& patterns_;
    ExtractDirectState state_;
    std::vector<R_len_t>& first_ineligible_;
    io::ParallelOutputBuilder& output_;
};


class Body final : public ParallelBody {
public:
    CHARR_CXX_HELPER Body(
        const std::vector<shared::StringView>& subjects,
        const std::vector<shared::StringView>& patterns,
        R_len_t vectorize_length, shared::FixedSearchOptions options,
        io::ParallelOutputBuilder& output
    ) noexcept
        : subjects_(subjects), patterns_(patterns),
          vectorize_length_(vectorize_length), options_(options),
          output_(output)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        const R_len_t subject_length =
            static_cast<R_len_t>(subjects_.size());
        const R_len_t pattern_length =
            static_cast<R_len_t>(patterns_.size());
        shared::FixedMatcher matcher;
        if (pattern_length == 1) {
            while (context.next_chunk()) {
                for (R_xlen_t task = context.begin;
                        task < context.end; ++task) {
                    extract_one(
                        static_cast<R_len_t>(task), subjects_, patterns_[0],
                        subject_length, options_, matcher, output_,
                        context.worker
                    );
                }
            }
            return;
        }

        while (context.next_chunk()) {
            for (R_xlen_t task = context.begin;
                    task < context.end; ++task) {
                const R_len_t lane = static_cast<R_len_t>(task);
                const shared::StringView& pattern = patterns_[
                    static_cast<std::size_t>(lane)
                ];
                R_len_t i = lane;
                for (;;) {
                    extract_one(
                        i, subjects_, pattern, subject_length, options_,
                        matcher, output_, context.worker
                    );
                    if (pattern_length >= vectorize_length_-i)
                        break;
                    i += pattern_length;
                }
            }
        }
    }

private:
    CHARR_CXX_HELPER static void extract_one(
        R_len_t i, const std::vector<shared::StringView>& subjects,
        const shared::StringView& pattern, R_len_t subject_length,
        shared::FixedSearchOptions options, shared::FixedMatcher& matcher,
        io::ParallelOutputBuilder& output, unsigned worker
    )
    {
        const shared::StringView& subject = subjects[
            static_cast<std::size_t>(i % subject_length)
        ];
        if (subject.is_na() || pattern.is_na() || pattern.len <= 0) {
            output.set_na(worker, i);
            return;
        }
        shared::FixedRange match{0, 0};
        if (!matcher.find_first(subject, pattern, options, match)) {
            output.set_na(worker, i);
            return;
        }
        output.set_validated(
            worker, i, matched_output_view(
                subject.ptr+match.start, match.end-match.start
            )
        );
    }

    const std::vector<shared::StringView>& subjects_;
    const std::vector<shared::StringView>& patterns_;
    R_len_t vectorize_length_;
    shared::FixedSearchOptions options_;
    io::ParallelOutputBuilder& output_;
};

} // namespace search_fixed_extract_first

using namespace search_fixed_extract_first;


CHARR_ENTRYPOINT SEXP ci_extract_first_fixed(
    SEXP str, SEXP pattern, SEXP opts_fixed
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();


    const shared::FixedSearchOptions options = fixed::prepare_options(
        opts_fixed, false
    );
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    pattern = entry_protections.protect_one(
        ci__prepare_arg_string_r(pattern, "pattern")
    );

    const R_xlen_t subject_xlength = XLENGTH(str);
    const R_xlen_t pattern_xlength = XLENGTH(pattern);
    if (subject_xlength > R_LEN_T_MAX || pattern_xlength > R_LEN_T_MAX)
        Rf_error("long character vectors are not supported");
    const R_len_t subject_length = static_cast<R_len_t>(subject_xlength);
    const R_len_t pattern_length = static_cast<R_len_t>(pattern_xlength);
    bool recycling_warning = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, recycling_warning
    );
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);
    const R_len_t tasks = vectorize_length == 0
        ? 0 : pattern_length == 1
            ? vectorize_length : pattern_length;
    const shared::ParallelPlan plan = shared::parallel_plan(true, tasks);


    R_len_t empty_pattern_warnings = 0;

    try {
        charport::Reader subject_reader;
        charport::Reader pattern_reader;
        charport::StrViews subject_views;
        charport::StrViews pattern_views;
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        std::vector<shared::StringView> subjects;
        std::vector<shared::StringView> patterns;
        shared::FixedMatcher matcher;
        io::OutputBuilder output(0);
        io::ParallelOutputBuilder parallel_output;
        std::vector<R_len_t> first_ineligible;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                if (plan.workers == 1)
                    output.reset(vectorize_length);
                if (vectorize_length > 0) {
                    subject_reader.reset(str);
                    if (subject_reader.size() != subject_length) {
                        throw std::runtime_error(
                            "Reader length changed during fixed extraction"
                        );
                    }
                    subject_views.resize(subject_length);
                    subject_reader.views(
                        0, subject_length,
                        subject_views.ptrs(), subject_views.lengths(),
                        subject_views.encodings()
                    );

                    pattern_reader.reset(pattern);
                    if (pattern_reader.size() != pattern_length) {
                        throw std::runtime_error(
                            "Reader length changed during fixed extraction"
                        );
                    }
                    pattern_views.resize(pattern_length);
                    pattern_reader.views(
                        0, pattern_length,
                        pattern_views.ptrs(), pattern_views.lengths(),
                        pattern_views.encodings()
                    );

                    /*
                     * A threaded request must not lose the byte path.
                     * Running the general kernel instead would pay for a
                     * whole normalization pass and a several times more
                     * expensive element, which more than cancels the split.
                     * Both plans therefore try the same direct kernel, and
                     * only the sink differs.
                     *
                     * The threaded attempt is all or nothing: a refusal
                     * anywhere means the general path rebuilds every element
                     * from index 0, and it resets the sharded builder before
                     * it writes, so partially filled shards are destroyed
                     * with the store they belonged to and can never reach
                     * the result.
                     */
                    ExtractDirectState direct_state;
                    bool direct = false;
                    if (plan.workers == 1) {
                        direct = extract_direct(
                            subject_views, pattern_views, options,
                            vectorize_length, output
                        );
                    }
                    else if (extract_direct_eligible(
                            subject_views, pattern_views, options,
                            vectorize_length, direct_state)) {
                        parallel_output.reset(
                            vectorize_length, plan.workers
                        );
                        first_ineligible.assign(
                            plan.workers, vectorize_length
                        );
                        DirectBody direct_body(
                            subject_views, pattern_views, direct_state,
                            first_ineligible, parallel_output
                        );
                        shared::run_parallel(
                            plan, vectorize_length, direct_body
                        );
                        direct = lowest_ineligible(
                            first_ineligible, vectorize_length
                        ) == vectorize_length;
                        if (direct) {
                            result = entry_protections.reprotect_one(
                                parallel_output.to_sexp(), result_index
                            );
                        }
                    }
                    if (!direct) {
                        if (plan.workers == 1)
                            output.reset(vectorize_length);
                        normalize_views(
                            subject_views, subject_converter,
                            subject_storage, subjects
                        );
                        normalize_views(
                            pattern_views, pattern_converter,
                            pattern_storage, patterns
                        );
                        empty_pattern_warnings =
                            count_empty_patterns(patterns);

                        if (plan.workers > 1) {
                            parallel_output.reset(
                                vectorize_length, plan.workers
                            );
                            Body body(
                                subjects, patterns, vectorize_length,
                                options, parallel_output
                            );
                            shared::run_parallel(plan, tasks, body);
                            result = entry_protections.reprotect_one(
                                parallel_output.to_sexp(), result_index
                            );
                        }
                        else {
                            for (R_len_t lane = 0;
                                    lane < pattern_length; ++lane) {
                                const shared::StringView& pattern_value =
                                    patterns[static_cast<std::size_t>(lane)];
                                R_len_t i = lane;
                                for (;;) {
                                    const shared::StringView& subject =
                                        subjects[static_cast<std::size_t>(
                                            i % subject_length
                                        )];
                                    if (subject.is_na() ||
                                            pattern_value.is_na() ||
                                            pattern_value.len <= 0) {
                                        output.set_na(i);
                                    }
                                    else {
                                        shared::FixedRange match{0, 0};
                                        const bool found = matcher.find_first(
                                            subject, pattern_value,
                                            options, match
                                        );
                                        if (!found) {
                                            output.set_na(i);
                                        }
                                        else {
                                            output.set_validated(
                                                i,
                                                matched_output_view(
                                                    subject.ptr+match.start,
                                                    match.end-match.start
                                                )
                                            );
                                        }
                                    }

                                    if (pattern_length >= vectorize_length-i)
                                        break;
                                    i += pattern_length;
                                }
                            }
                        }
                    }
                }

                if (plan.workers == 1) {
                    result = entry_protections.reprotect_one(
                        output.to_sexp(), result_index
                    );
                }
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_empty_pattern_warnings(empty_pattern_warnings);
    );
}

} } // namespace charr::altrep_backend
