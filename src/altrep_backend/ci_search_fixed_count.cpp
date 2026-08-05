
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
#include <stdexcept>
#include <vector>

namespace charr { namespace altrep_backend {


namespace search_fixed_count {

CHARR_NEUTRAL_HELPER inline R_len_t count_ascii_byte(
    const char* data, R_len_t length, unsigned char pattern
) noexcept
{
    R_len_t count = 0;
    const unsigned char* current =
        reinterpret_cast<const unsigned char*>(data);
    const unsigned char* end = current + length;
    for (; current != end; ++current)
        count += (*current == pattern);
    return count;
}


CHARR_NEUTRAL_HELPER bool direct_ascii_encoding(
    shared::StringEncoding encoding
) noexcept
{
    return encoding == shared::StringEncoding::ascii ||
        encoding == shared::StringEncoding::utf8 ||
        encoding == shared::StringEncoding::ascii_or_utf8;
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


/*
 * A scalar pattern of one ASCII byte, the only shape the direct kernel
 * counts. Requiring pattern_length == 1 is also what keeps a task index and
 * a subject index the same thing: recycling_length() returns the larger of
 * the two lengths, so vectorize_length == subject_length here and a task
 * index is always in bounds of the subject views.
 */
CHARR_NEUTRAL_HELPER bool direct_ascii_pattern(
    const charport::StrViews& patterns, R_len_t pattern_length,
    shared::FixedSearchOptions options, unsigned char& pattern_byte
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
    pattern_byte = static_cast<unsigned char>(pattern.ptr[0]);
    return true;
}


/*
 * One element of the direct kernel, written once and used by both the
 * serial scan and the worker body so the two cannot drift.
 *
 * Returns false when the record needs the general path, leaving result[i]
 * unwritten. Classifying a record costs nothing here because the kernel
 * already loads its pointer, length, and encoding to count it.
 */
CHARR_NEUTRAL_HELPER bool count_ascii_element(
    const charport::StrViews& subjects, R_len_t i,
    unsigned char pattern_byte, int* result
) noexcept
{
    const shared::StringView value = io::as_shared_view(subjects[i]);
    if (value.is_na()) {
        result[i] = NA_INTEGER;
        return true;
    }
    if (!direct_ascii_encoding(value.enc) || value.len < 0 ||
            (value.ptr == nullptr && value.len > 0)) {
        return false;
    }
    result[i] = value.len == 0
        ? 0
        : count_ascii_byte(value.ptr, value.len, pattern_byte);
    return true;
}


CHARR_NEUTRAL_HELPER bool count_ascii_scalar_direct(
    const charport::StrViews& subjects,
    const charport::StrViews& patterns,
    R_len_t pattern_length,
    R_len_t vectorize_length,
    shared::FixedSearchOptions options,
    int* result,
    R_len_t& general_start
) noexcept
{
    unsigned char pattern_byte = 0;
    if (!direct_ascii_pattern(
            patterns, pattern_length, options, pattern_byte)) {
        return false;
    }

    for (R_len_t i = 0; i < vectorize_length; ++i) {
        if (!count_ascii_element(subjects, i, pattern_byte, result)) {
            general_start = i;
            return false;
        }
    }

    return true;
}


/*
 * The direct kernel on workers. Each worker classifies its own records as
 * it counts them, so the operation does not pay a serial eligibility pass
 * over every subject before the parallel region.
 *
 * A worker that meets a record the kernel cannot count parks that index in
 * its own slot of first_ineligible and stops, leaving the rest of its chunk
 * unwritten for the general path.
 */
class DirectAsciiBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER DirectAsciiBody(
        const charport::StrViews& subjects, unsigned char pattern,
        int* result, std::vector<R_len_t>& first_ineligible
    ) noexcept
        : subjects_(subjects), pattern_(pattern), result_(result),
          first_ineligible_(first_ineligible)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        while (context.next_chunk()) {
            const R_len_t begin = static_cast<R_len_t>(context.begin);
            const R_len_t end = static_cast<R_len_t>(context.end);
            for (R_len_t i = begin; i < end; ++i) {
                if (!count_ascii_element(subjects_, i, pattern_, result_)) {
                    first_ineligible_[context.worker] = i;
                    context.stop_early();
                    return;
                }
            }
        }
    }

private:
    const charport::StrViews& subjects_;
    unsigned char pattern_;
    int* result_;
    std::vector<R_len_t>& first_ineligible_;
};


/*
 * Where the general path resumes after the direct kernel, or `fallback`
 * when no worker rejected a record.
 *
 * Chunks are contiguous and ordered, so the lowest index any worker
 * rejected is the index the serial scan would have stopped at. Below it
 * every record was counted by the direct kernel: a worker only stops at a
 * rejected index, and there is none lower. At or above it every record is
 * either unwritten or overwritten, because Body::run() resumes at
 * max(chunk begin, general_start) and writes to the end of its chunk.
 */
CHARR_NEUTRAL_HELPER R_len_t lowest_ineligible(
    const std::vector<R_len_t>& first_ineligible, R_len_t fallback
) noexcept
{
    R_len_t result = fallback;
    for (std::size_t i = 0; i < first_ineligible.size(); ++i) {
        if (first_ineligible[i] < result)
            result = first_ineligible[i];
    }
    return result;
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


CHARR_NEUTRAL_HELPER int count_empty_patterns(
    const std::vector<shared::StringView>& patterns
) noexcept
{
    int result = 0;
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
        int* result
    ) noexcept
        : subjects_(subjects), patterns_(patterns),
          vectorize_length_(vectorize_length), general_start_(general_start),
          options_(options), result_(result)
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
                "fixed-count direct prefix requires a scalar pattern"
            );
        }

        shared::FixedMatcher matcher;
        if (pattern_length == 1) {
            const shared::StringView& pattern = patterns_[0];
            while (context.next_chunk()) {
                R_len_t begin = static_cast<R_len_t>(context.begin);
                if (begin < general_start_)
                    begin = general_start_;
                const R_len_t end = static_cast<R_len_t>(context.end);
                for (R_len_t i = begin; i < end; ++i) {
                    count_one(
                        i, subject_length, pattern, options_, matcher, result_
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
                    count_one(
                        i, subject_length, pattern, options_, matcher, result_
                    );
                    if (pattern_length >= vectorize_length_-i)
                        break;
                    i += pattern_length;
                }
            }
        }
    }

private:
    CHARR_CXX_HELPER void count_one(
        R_len_t i,
        R_len_t subject_length,
        const shared::StringView& pattern,
        shared::FixedSearchOptions options,
        shared::FixedMatcher& matcher,
        int* result
    ) const
    {
        const shared::StringView& subject = subjects_[
            static_cast<std::size_t>(i % subject_length)
        ];

        if (subject.is_na() || pattern.is_na() || pattern.len <= 0) {
            result[i] = NA_INTEGER;
        }
        else if (subject.len <= 0) {
            result[i] = 0;
        }
        else {
            result[i] = matcher.count(subject, pattern, options);
        }
    }

    const std::vector<shared::StringView>& subjects_;
    const std::vector<shared::StringView>& patterns_;
    R_len_t vectorize_length_;
    R_len_t general_start_;
    shared::FixedSearchOptions options_;
    int* result_;
};


CHARR_R_HELPER void emit_warnings(
    bool recycling_warning, int empty_pattern_warnings
) noexcept
{
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);
    for (int i = 0; i < empty_pattern_warnings; ++i)
        Rf_warning(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
}

} // namespace search_fixed_count

using namespace search_fixed_count;


/**
 * Count the number of recurrences of \code{pattern} in \code{str}
 * [fast but dummy bitewise compare]
 *
 * @param str strings to search in
 * @param pattern patterns to search for
 * @return integer vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          corrected behavior on empty str/pattern
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-23)
 *          make StriException-friendly,
 *          use fixed::PatternSet
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          ci_count_fixed now uses byte search only
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-07)
 *    FR #110, #23: opts_fixed arg added
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-02-14)
 *    use shared::ByteSearchMatcher
 */
CHARR_ENTRYPOINT SEXP ci_count_fixed(
    SEXP str, SEXP pattern, SEXP opts_fixed
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const shared::FixedSearchOptions options = fixed::prepare_options(
        opts_fixed, true
    );
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    pattern = entry_protections.protect_one(
        ci__prepare_arg_string_r(pattern, "pattern")
    );


    bool recycling_warning = false;
    int empty_pattern_warnings = 0;
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
                    Rf_allocVector(INTSXP, vectorize_length), result_index
                );
                int* output = INTEGER(result);

                if (vectorize_length > 0) {
                    subject_reader.reset(str);
                    if (subject_reader.size() != subject_length) {
                        throw std::runtime_error(
                            "Reader length changed during fixed counting"
                        );
                    }

                    pattern_reader.reset(pattern);
                    if (pattern_reader.size() != pattern_length) {
                        throw std::runtime_error(
                            "Reader length changed during fixed counting"
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
                        true, tasks
                    );
                    // A serial plan classifies and counts in one scan; a
                    // threaded plan hands both to the workers and reduces
                    // their verdicts afterwards.
                    bool direct = plan.workers == 1 &&
                        count_ascii_scalar_direct(
                            subject_views, pattern_views, pattern_length,
                            vectorize_length, options, output, general_start
                        );
                    if (plan.workers > 1) {
                        unsigned char pattern_byte = 0;
                        if (direct_ascii_pattern(
                                pattern_views, pattern_length,
                                options, pattern_byte)) {
                            first_ineligible.assign(
                                static_cast<std::size_t>(plan.workers),
                                vectorize_length
                            );
                            DirectAsciiBody body(
                                subject_views, pattern_byte, output,
                                first_ineligible
                            );
                            shared::run_parallel(
                                plan, vectorize_length, body
                            );
                            const R_len_t resume = lowest_ineligible(
                                first_ineligible, vectorize_length
                            );
                            direct = resume == vectorize_length;
                            if (!direct)
                                general_start = resume;
                        }
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
                            general_start, options, output
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
