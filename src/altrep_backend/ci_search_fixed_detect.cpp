
/* This file is part of the 'stringi' project.
 * Copyright (c) 2013-2025, Marek Gagolewski <https://www.gagolewski.com/>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "ci_stringi.h"
#include "ci_parallel.h"
#include "io/reader_utils.h"
#include "fixed/options.h"
#include "io/string_view.h"
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


namespace search_fixed_detect {

CHARR_NEUTRAL_HELPER bool direct_ascii_encoding(
    shared::StringEncoding encoding
) noexcept
{
    return encoding == shared::StringEncoding::ascii ||
        encoding == shared::StringEncoding::utf8 ||
        encoding == shared::StringEncoding::ascii_or_utf8;
}


CHARR_NEUTRAL_HELPER bool contains_ascii_byte(
    const char* data, R_len_t length, unsigned char pattern
) noexcept
{
    const unsigned char* current =
        reinterpret_cast<const unsigned char*>(data);
    const unsigned char* end = current + length;
    for (; current != end; ++current) {
        if (*current == pattern)
            return true;
    }
    return false;
}


CHARR_NEUTRAL_HELPER R_len_t recycling_length(
    R_len_t first, R_len_t second, bool& warning
) noexcept
{
    warning = false;
    if (first <= 0 || second <= 0)
        return 0;

    const R_len_t result = first > second ? first : second;
    warning = result % first != 0 || result % second != 0;
    return result;
}


// Everything the direct ASCII kernel needs that does not vary by element.
// It is filled once on the main thread and then only read, so a worker can
// share it by value or by const reference.
struct DirectAsciiState {
    unsigned char pattern_byte;
    bool negate;
};


// "This worker refused no element." Any other value is a task index.
constexpr R_len_t direct_all_eligible = -1;


// The pattern and options phase of the direct path: O(1), main thread, and
// the same decision for the serial scan and the parallel body.
CHARR_NEUTRAL_HELPER bool direct_ascii_eligible(
    const charport::StrViews& patterns,
    R_len_t pattern_length,
    shared::FixedSearchOptions options,
    bool negate,
    DirectAsciiState& state
) noexcept
{
    if (options.case_insensitive || options.overlap || pattern_length != 1)
        return false;

    const shared::StringView pattern = io::as_shared_view(patterns[0]);
    if (pattern.is_na() || !direct_ascii_encoding(pattern.enc) ||
            pattern.ptr == nullptr || pattern.len != 1 ||
            static_cast<unsigned char>(pattern.ptr[0]) > 0x7f) {
        return false;
    }

    state.pattern_byte = static_cast<unsigned char>(pattern.ptr[0]);
    state.negate = negate;
    return true;
}


// One element of the direct path. Returns false when this subject is not one
// the direct path can answer, having written nothing at `i`; the caller
// decides whether that ends a serial scan or a worker's chunk.
CHARR_NEUTRAL_HELPER bool detect_ascii_one(
    const DirectAsciiState& state,
    const charport::StrView& source,
    R_len_t i,
    int& max_count,
    int* result
) noexcept
{
    const shared::StringView subject = io::as_shared_view(source);
    if (!subject.is_na() &&
            (!direct_ascii_encoding(subject.enc) || subject.len < 0 ||
             (subject.ptr == nullptr && subject.len > 0))) {
        return false;
    }

    if (max_count == 0) {
        result[i] = NA_LOGICAL;
        return true;
    }
    if (subject.is_na()) {
        result[i] = NA_LOGICAL;
        return true;
    }

    const bool found = subject.len > 0 && contains_ascii_byte(
        subject.ptr, subject.len, state.pattern_byte
    );
    result[i] = state.negate ? !found : found;
    if (max_count > 0 && result[i])
        --max_count;
    return true;
}


CHARR_NEUTRAL_HELPER bool detect_ascii_scalar_direct(
    const charport::StrViews& subjects,
    const charport::StrViews& patterns,
    R_len_t pattern_length,
    R_len_t vectorize_length,
    shared::FixedSearchOptions options,
    bool negate,
    int& max_count,
    int* result,
    R_len_t& general_start
) noexcept
{
    DirectAsciiState state = {};
    if (!direct_ascii_eligible(
            patterns, pattern_length, options, negate, state)) {
        return false;
    }

    // Eligibility required a scalar pattern, so recycling made
    // vectorize_length equal subject_length and subjects[i] is in bounds.
    for (R_len_t i = 0; i < vectorize_length; ++i) {
        if (!detect_ascii_one(state, subjects[i], i, max_count, result)) {
            general_start = i;
            return false;
        }
    }

    return true;
}


/*
 * The direct path over worker threads, running the same element kernel the
 * serial scan runs. A worker reads prefetched views and writes the logical
 * output at its own task indices only: no R API, no Reader, no allocation.
 * A worker that meets a subject the kernel refuses records that index and
 * stops, leaving the rest of its chunk to the general path.
 */
class DirectBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER DirectBody(
        const charport::StrViews& subjects,
        DirectAsciiState state,
        int max_count,
        int* result,
        std::vector<R_len_t>& first_ineligible
    ) noexcept
        : subjects_(subjects), state_(state), max_count_(max_count),
          result_(result), first_ineligible_(first_ineligible)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        // The plan is only eligible when max_count is infinite, so this copy
        // never changes. It keeps one kernel signature for both paths, the
        // way the general Body below also copies the counter per worker.
        int max_count = max_count_;
        while (context.next_chunk()) {
            const R_len_t begin = static_cast<R_len_t>(context.begin);
            const R_len_t end = static_cast<R_len_t>(context.end);
            for (R_len_t i = begin; i < end; ++i) {
                if (!detect_ascii_one(
                        state_, subjects_[i], i, max_count, result_)) {
                    first_ineligible_[
                        static_cast<std::size_t>(context.worker)
                    ] = i;
                    context.stop_early();
                    return;
                }
            }
        }
    }

private:
    const charport::StrViews& subjects_;
    DirectAsciiState state_;
    int max_count_;
    int* result_;
    std::vector<R_len_t>& first_ineligible_;
};


/*
 * Reduce the workers' reports to the index a serial scan would have stopped
 * at. Chunks are contiguous and ordered, so if any worker refused index j,
 * every index at or above j is either unwritten or will be overwritten: the
 * general path resumes at general_start, and its Body::run() starts each
 * chunk at max(context.begin, general_start_). Indices below the minimum
 * were written by the direct kernel and are kept.
 */
CHARR_NEUTRAL_HELPER R_len_t lowest_ineligible(
    const std::vector<R_len_t>& reports
) noexcept
{
    R_len_t first = direct_all_eligible;
    for (std::size_t i = 0; i < reports.size(); ++i) {
        if (reports[i] != direct_all_eligible &&
                (first == direct_all_eligible || reports[i] < first)) {
            first = reports[i];
        }
    }
    return first;
}


CHARR_CXX_HELPER void normalize_views(
    const charport::StrViews& source,
    R_len_t source_length,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    std::vector<shared::StringView>& output
)
{
    output.resize(static_cast<std::size_t>(source_length));
    for (R_len_t i = 0; i < source_length; ++i) {
        output[static_cast<std::size_t>(i)] = shared::normalize_utf8(
            io::as_shared_view(source[i]), converter, storage
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


class Body final : public ParallelBody {
public:
    CHARR_CXX_HELPER Body(
        const std::vector<shared::StringView>& subjects,
        const std::vector<shared::StringView>& patterns,
        R_len_t vectorize_length,
        R_len_t general_start,
        shared::FixedSearchOptions options,
        bool negate,
        int max_count,
        int* result
    ) noexcept
        : subjects_(subjects), patterns_(patterns),
          vectorize_length_(vectorize_length), general_start_(general_start),
          options_(options), negate_(negate), max_count_(max_count),
          result_(result)
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
        if (general_start_ > 0 && pattern_length != 1) {
            throw std::logic_error(
                "fixed-detect direct prefix requires a scalar pattern"
            );
        }

        shared::FixedMatcher matcher;
        int max_count = max_count_;
        if (pattern_length == 1) {
            const shared::StringView& pattern = patterns_[0];
            while (context.next_chunk()) {
                R_len_t begin = static_cast<R_len_t>(context.begin);
                if (begin < general_start_)
                    begin = general_start_;
                const R_len_t end = static_cast<R_len_t>(context.end);
                for (R_len_t i = begin; i < end; ++i) {
                    detect_one(
                        i, subject_length, pattern, options_, negate_,
                        max_count, matcher, result_
                    );
                }
            }
            return;
        }

        while (context.next_chunk()) {
            const R_len_t begin = static_cast<R_len_t>(context.begin);
            const R_len_t end = static_cast<R_len_t>(context.end);
            for (R_len_t lane = begin; lane < end; ++lane) {
                const shared::StringView& pattern = patterns_[
                    static_cast<std::size_t>(lane)
                ];
                R_len_t i = lane;
                for (;;) {
                    detect_one(
                        i, subject_length, pattern, options_, negate_,
                        max_count, matcher, result_
                    );
                    if (pattern_length >= vectorize_length_-i)
                        break;
                    i += pattern_length;
                }
            }
        }
    }

private:
    CHARR_CXX_HELPER void detect_one(
        R_len_t i,
        R_len_t subject_length,
        const shared::StringView& pattern,
        shared::FixedSearchOptions options,
        bool negate,
        int& max_count,
        shared::FixedMatcher& matcher,
        int* result
    ) const
    {
        const shared::StringView& subject = subjects_[
            static_cast<std::size_t>(i % subject_length)
        ];

        if (max_count == 0) {
            result[i] = NA_LOGICAL;
        }
        else if (subject.is_na() || pattern.is_na() || pattern.len <= 0) {
            result[i] = NA_LOGICAL;
        }
        else if (subject.len <= 0) {
            result[i] = negate;
        }
        else {
            const bool found = matcher.contains(subject, pattern, options);
            result[i] = negate ? !found : found;
        }

        if (max_count > 0 && result[i] == TRUE)
            --max_count;
    }

    const std::vector<shared::StringView>& subjects_;
    const std::vector<shared::StringView>& patterns_;
    R_len_t vectorize_length_;
    R_len_t general_start_;
    shared::FixedSearchOptions options_;
    bool negate_;
    int max_count_;
    int* result_;
};


CHARR_R_HELPER void emit_warnings(
    bool recycling_warning, R_len_t empty_pattern_warnings
) noexcept
{
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);
    for (R_len_t i = 0; i < empty_pattern_warnings; ++i)
        Rf_warning(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
}

} // namespace search_fixed_detect

using namespace search_fixed_detect;


/**
 * Detect if a pattern occurs in a string [fast but dummy bitewise compare]
 *
 * @param str character vector
 * @param pattern character vector
 * @param negate single bool
 * @param max_count single int
 * @return logical vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *    corrected behavior on empty str/pattern
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-23)
 *    make StriException-friendly, use fixed::PatternSet
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          ci_detect_fixed now uses byte search only
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-07)
 *    FR #110, #23: opts_fixed arg added
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-02-14)
 *    use shared::ByteSearchMatcher
 *
 * @version 1.0-3 (Marek Gagolewski, 2016-02-03)
 *    #216: `negate` arg added
 *
 * @version 1.3.1 (Marek Gagolewski, 2019-02-08)
 *    #232: `max_count` arg added
 */
CHARR_ENTRYPOINT SEXP ci_detect_fixed(
    SEXP str, SEXP pattern, SEXP negate,
    SEXP max_count, SEXP opts_fixed
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool negate_1 = ci__prepare_arg_logical_1_notNA_r(
        negate, "negate"
    );
    int max_count_1 = ci__prepare_arg_integer_1_notNA_r(
        max_count, "max_count"
    );
    const shared::FixedSearchOptions options = fixed::prepare_options(
        opts_fixed, false
    );
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    pattern = entry_protections.protect_one(
        ci__prepare_arg_string_r(pattern, "pattern")
    );


    bool recycling_warning = false;
    R_len_t empty_pattern_warnings = 0;
    R_len_t general_start = 0;

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
        std::vector<R_len_t> first_ineligible;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t subject_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                const R_len_t pattern_length = io::checked_r_len(
                    XLENGTH(pattern), "character vectors"
                );
                const R_len_t vectorize_length = recycling_length(
                    subject_length, pattern_length, recycling_warning
                );

                result = entry_protections.reprotect_one(
                    Rf_allocVector(LGLSXP, vectorize_length), result_index
                );
                int* output = LOGICAL(result);

                if (vectorize_length > 0) {
                    subject_reader.reset(str);
                    if (subject_reader.size() != subject_length) {
                        throw std::runtime_error(
                            "Reader length changed during fixed detection"
                        );
                    }

                    pattern_reader.reset(pattern);
                    if (pattern_reader.size() != pattern_length) {
                        throw std::runtime_error(
                            "Reader length changed during fixed detection"
                        );
                    }

                    subject_views.resize(subject_length);
                    subject_reader.views(
                        0, subject_length,
                        subject_views.ptrs(), subject_views.lengths(),
                        subject_views.encodings()
                    );
                    pattern_views.resize(pattern_length);
                    pattern_reader.views(
                        0, pattern_length,
                        pattern_views.ptrs(), pattern_views.lengths(),
                        pattern_views.encodings()
                    );

                    const R_len_t tasks = pattern_length == 1
                        ? vectorize_length
                        : pattern_length;
                    const shared::ParallelPlan plan = shared::parallel_plan(
                        max_count_1 < 0, tasks
                    );
                    // Both paths run the same element kernel, so asking for
                    // more threads never swaps in the more expensive general
                    // kernel; it only divides the direct one.
                    bool direct = false;
                    DirectAsciiState direct_state = {};
                    if (plan.workers == 1) {
                        direct = detect_ascii_scalar_direct(
                            subject_views, pattern_views, pattern_length,
                            vectorize_length, options, negate_1, max_count_1,
                            output, general_start
                        );
                    }
                    else if (direct_ascii_eligible(
                            pattern_views, pattern_length, options, negate_1,
                            direct_state)) {
                        // Eligibility required a scalar pattern, so tasks
                        // above is vectorize_length and the plan already
                        // chunks the range the body walks.
                        first_ineligible.assign(
                            static_cast<std::size_t>(plan.workers),
                            direct_all_eligible
                        );
                        DirectBody direct_body(
                            subject_views, direct_state, max_count_1,
                            output, first_ineligible
                        );
                        shared::run_parallel(
                            plan, vectorize_length, direct_body
                        );
                        const R_len_t first = lowest_ineligible(
                            first_ineligible
                        );
                        direct = first == direct_all_eligible;
                        if (!direct)
                            general_start = first;
                    }

                    if (!direct) {
                        normalize_views(
                            subject_views, subject_length,
                            subject_converter, subject_storage, subjects
                        );
                        normalize_views(
                            pattern_views, pattern_length,
                            pattern_converter, pattern_storage, patterns
                        );
                        empty_pattern_warnings = count_empty_patterns(patterns);
                        Body body(
                            subjects, patterns, vectorize_length,
                            general_start, options, negate_1, max_count_1,
                            output
                        );
                        shared::run_parallel(plan, tasks, body);
                    }
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_warnings(recycling_warning, empty_pattern_warnings);
    );
}

} } // namespace charr::altrep_backend
