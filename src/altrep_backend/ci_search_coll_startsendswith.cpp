
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
#include "io/reader_utils.h"
#include "collator/options.h"
#include "io/string_view.h"
#include "../shared/collation_search.h"
#include "../shared/collator.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "ci_parallel.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <charport.h>

#include <cstddef>
#include <exception>
#include <stdexcept>


namespace charr { namespace altrep_backend {

namespace search_coll_startsendswith {

CHARR_NEUTRAL_HELPER R_len_t recycling_length(
    R_len_t first, R_len_t second, R_len_t third, bool& warning
) noexcept
{
    warning = false;
    if (first <= 0 || second <= 0 || third <= 0)
        return 0;

    R_len_t result = first > second ? first : second;
    if (third > result)
        result = third;
    warning = result % first != 0 || result % second != 0 ||
        result % third != 0;
    return result;
}


CHARR_CXX_HELPER void require_icu_success(UErrorCode status)
{
    if (U_FAILURE(status))
        throw StriException(status);
}


CHARR_CXX_HELPER void normalize_views(
    charport::StrViews& source,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage
)
{
    for (R_xlen_t i = 0; i < source.size(); ++i) {
        const shared::StringView value = io::as_shared_view(source[i]);
        if (value.enc == shared::StringEncoding::bytes)
            throw StriException(MSG__BYTESENC);
        const shared::StringView normalized =
            shared::normalize_utf8_preserve_bom(
                value, converter, storage
            );
        const charport::StrView prepared = io::as_charport_view(normalized);
        source.ptrs()[i] = prepared.ptr;
        source.lengths()[i] = prepared.len;
        source.encodings()[i] = prepared.enc;
    }
}


CHARR_CXX_HELPER void stage_utf16(
    const charport::StrViews& source,
    shared::CollationInputs& output
)
{
    output.resize(static_cast<std::size_t>(source.size()));
    for (R_xlen_t i = 0; i < source.size(); ++i) {
        output.set(
            static_cast<std::size_t>(i),
            io::as_shared_view(source[i])
        );
    }
}


CHARR_CXX_HELPER int count_empty_patterns(
    const shared::CollationInputs& patterns
)
{
    int result = 0;
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        const shared::CollationInput pattern = patterns.get(i);
        if (!pattern.missing && pattern.length <= 0)
            ++result;
    }
    return result;
}


CHARR_CXX_HELPER void match_sequence(
    const charport::StrViews& subjects,
    const shared::CollationInput& pattern,
    const int* positions,
    R_len_t position_length,
    R_len_t first,
    R_len_t limit,
    R_len_t step,
    UCollator* collator,
    bool starts,
    bool negate,
    shared::CollationCursor& subject_cursor,
    shared::CollationMatcher& matcher,
    int* output
)
{
    const R_len_t subject_length = static_cast<R_len_t>(subjects.size());
    R_len_t i = first;
    while (i < limit) {
        const R_len_t raw_subject = i % subject_length;
        const shared::StringView source = io::as_shared_view(
            subjects[raw_subject]
        );

        if (source.is_na() || pattern.missing || pattern.length <= 0) {
            output[i] = NA_LOGICAL;
        }
        else if (source.len <= 0) {
            output[i] = negate;
        }
        else {
            const shared::CollationInput subject = subject_cursor.get(
                static_cast<const void*>(subjects.ptrs() + raw_subject),
                source
            );
            const int position = positions[i % position_length];
            if (position == NA_INTEGER) {
                output[i] = NA_LOGICAL;
            }
            else {
                const int offset = starts
                    ? shared::utf16_start_offset(subject, position)
                    : shared::utf16_end_offset(subject, position);
                bool matched = false;
                if ((starts && offset < subject.length) ||
                        (!starts && offset > 0)) {
                    UErrorCode status = U_ZERO_ERROR;
                    matched = starts
                        ? matcher.starts_with(
                            collator, subject, pattern, offset, status
                        )
                        : matcher.ends_with(
                            collator, subject, pattern, offset, status
                        );
                    require_icu_success(status);
                }
                output[i] = static_cast<int>(matched != negate);
            }
        }

        if (step >= limit-i)
            break;
        i += step;
    }
}


class Body final : public ParallelBody {
public:
    CHARR_CXX_HELPER Body(
        const charport::StrViews& subjects,
        const shared::CollationInputs& patterns,
        const int* positions,
        R_len_t position_length,
        R_len_t vectorize_length,
        const shared::CollatorOptions& options,
        bool starts,
        bool negate,
        int* output,
        bool& root_fallback_warning
    ) noexcept
        : subjects_(subjects), patterns_(patterns), positions_(positions),
          position_length_(position_length),
          vectorize_length_(vectorize_length), options_(options),
          starts_(starts), negate_(negate), output_(output),
          root_fallback_warning_(root_fallback_warning)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        shared::Collator collator;
        const shared::CollatorOpenResult opened = collator.reset(options_);
        if (context.worker == 0)
            root_fallback_warning_ = opened.root_fallback;
        require_icu_success(opened.status);

        shared::CollationCursor subject_cursor;
        shared::CollationMatcher matcher;
        const R_len_t pattern_length = static_cast<R_len_t>(patterns_.size());
        if (pattern_length == 1) {
            while (context.next_chunk()) {
                match_sequence(
                    subjects_, patterns_.get(0), positions_,
                    position_length_,
                    static_cast<R_len_t>(context.begin),
                    static_cast<R_len_t>(context.end), 1,
                    collator.get(), starts_, negate_,
                    subject_cursor, matcher, output_
                );
            }
            return;
        }

        while (context.next_chunk()) {
            const R_len_t lane_end = static_cast<R_len_t>(context.end);
            for (R_len_t lane = static_cast<R_len_t>(context.begin);
                    lane < lane_end; ++lane) {
                match_sequence(
                    subjects_, patterns_.get(static_cast<std::size_t>(lane)),
                    positions_, position_length_,
                    lane, vectorize_length_, pattern_length,
                    collator.get(), starts_, negate_,
                    subject_cursor, matcher, output_
                );
            }
        }
    }

private:
    const charport::StrViews& subjects_;
    const shared::CollationInputs& patterns_;
    const int* positions_;
    R_len_t position_length_;
    R_len_t vectorize_length_;
    const shared::CollatorOptions& options_;
    bool starts_;
    bool negate_;
    int* output_;
    bool& root_fallback_warning_;
};


CHARR_R_HELPER void emit_warnings(
    bool root_fallback_warning,
    bool recycling_warning,
    int empty_pattern_warnings
) noexcept
{
    if (root_fallback_warning) {
        Rf_warning(
            "%s", ICUError::getICUerrorName(U_USING_DEFAULT_WARNING)
        );
    }
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);
    for (int i = 0; i < empty_pattern_warnings; ++i)
        Rf_warning(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
}

} // namespace search_coll_startsendswith

using namespace search_coll_startsendswith;


/**
 * Detect if a string starts with a pattern match
 *
 * @param str character vector
 * @param pattern character vector
 * @param from integer vector
 * @param opts_collator named list
 * @return logical vector
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-01)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.4.7 (Marek Gagolewski, 2020-08-24)
 *    #345: `negate` arg added
 */
CHARR_ENTRYPOINT SEXP ci_startswith_coll(
    SEXP str, SEXP pattern, SEXP from,
    SEXP negate, SEXP opts_collator
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool negate_1 = ci__prepare_arg_logical_1_notNA_r(
        negate, "negate"
    );
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    pattern = entry_protections.protect_one(
        ci__prepare_arg_string_r(pattern, "pattern")
    );
    from = entry_protections.protect_one(
        ci__prepare_arg_integer_r(from, "from")
    );
    const shared::CollatorOptions options =
        collator::prepare_options(opts_collator);


    bool root_fallback_warning = false;
    bool recycling_warning = false;
    int empty_pattern_warnings = 0;

    try {
        charport::Reader subject_reader;
        charport::Reader pattern_reader;
        charport::StrViews subject_views;
        charport::StrViews pattern_views;
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        shared::CollationInputs patterns;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t subject_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                const R_len_t pattern_length = io::checked_r_len(
                    XLENGTH(pattern), "character vectors"
                );
                const R_len_t position_length = io::checked_r_len(
                    XLENGTH(from), "integer vectors"
                );
                bool pending_recycling_warning = false;
                const R_len_t vectorize_length = recycling_length(
                    subject_length, pattern_length, position_length,
                    pending_recycling_warning
                );

                recycling_warning = pending_recycling_warning;

                if (vectorize_length > 0) {
                    subject_reader.reset(str);
                    if (subject_reader.size() != subject_length) {
                        throw std::runtime_error(
                            "Reader length changed during collation starts-with"
                        );
                    }
                    subject_views.resize(subject_length);
                    subject_reader.views(
                        0, subject_length,
                        subject_views.ptrs(), subject_views.lengths(),
                        subject_views.encodings()
                    );
                    normalize_views(
                        subject_views, subject_converter, subject_storage
                    );

                    pattern_reader.reset(pattern);
                    if (pattern_reader.size() != pattern_length) {
                        throw std::runtime_error(
                            "Reader length changed during collation starts-with"
                        );
                    }
                    pattern_views.resize(pattern_length);
                    pattern_reader.views(
                        0, pattern_length,
                        pattern_views.ptrs(), pattern_views.lengths(),
                        pattern_views.encodings()
                    );
                    normalize_views(
                        pattern_views, pattern_converter, pattern_storage
                    );
                    stage_utf16(pattern_views, patterns);
                    empty_pattern_warnings = count_empty_patterns(patterns);
                }

                result = entry_protections.reprotect_one(
                    Rf_allocVector(LGLSXP, vectorize_length), result_index
                );
                int* output = LOGICAL(result);
                const R_xlen_t tasks = vectorize_length <= 0
                    ? 0
                    : pattern_length == 1
                        ? vectorize_length
                        : pattern_length;
                const shared::ParallelPlan plan = shared::parallel_plan(
                    true, tasks
                );
                Body body(
                    subject_views, patterns, INTEGER_RO(from),
                    position_length, vectorize_length, options,
                    true, negate_1, output, root_fallback_warning
                );
                shared::run_parallel(plan, tasks, body);

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_warnings(
            root_fallback_warning,
            recycling_warning,
            empty_pattern_warnings
        );
    );
}


/**
 * Detect if a string ends with a pattern match
 *
 * @param str character vector
 * @param pattern character vector
 * @param to integer vector
 * @param opts_collator named list
 * @return logical vector
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-01)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.4.7 (Marek Gagolewski, 2020-08-24)
 *    #345: `negate` arg added
 */
CHARR_ENTRYPOINT SEXP ci_endswith_coll(
    SEXP str, SEXP pattern, SEXP to,
    SEXP negate, SEXP opts_collator
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool negate_1 = ci__prepare_arg_logical_1_notNA_r(
        negate, "negate"
    );
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    pattern = entry_protections.protect_one(
        ci__prepare_arg_string_r(pattern, "pattern")
    );
    to = entry_protections.protect_one(
        ci__prepare_arg_integer_r(to, "to")
    );
    const shared::CollatorOptions options =
        collator::prepare_options(opts_collator);


    bool root_fallback_warning = false;
    bool recycling_warning = false;
    int empty_pattern_warnings = 0;

    try {
        charport::Reader subject_reader;
        charport::Reader pattern_reader;
        charport::StrViews subject_views;
        charport::StrViews pattern_views;
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        shared::CollationInputs patterns;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t subject_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                const R_len_t pattern_length = io::checked_r_len(
                    XLENGTH(pattern), "character vectors"
                );
                const R_len_t position_length = io::checked_r_len(
                    XLENGTH(to), "integer vectors"
                );
                bool pending_recycling_warning = false;
                const R_len_t vectorize_length = recycling_length(
                    subject_length, pattern_length, position_length,
                    pending_recycling_warning
                );

                recycling_warning = pending_recycling_warning;

                if (vectorize_length > 0) {
                    subject_reader.reset(str);
                    if (subject_reader.size() != subject_length) {
                        throw std::runtime_error(
                            "Reader length changed during collation ends-with"
                        );
                    }
                    subject_views.resize(subject_length);
                    subject_reader.views(
                        0, subject_length,
                        subject_views.ptrs(), subject_views.lengths(),
                        subject_views.encodings()
                    );
                    normalize_views(
                        subject_views, subject_converter, subject_storage
                    );

                    pattern_reader.reset(pattern);
                    if (pattern_reader.size() != pattern_length) {
                        throw std::runtime_error(
                            "Reader length changed during collation ends-with"
                        );
                    }
                    pattern_views.resize(pattern_length);
                    pattern_reader.views(
                        0, pattern_length,
                        pattern_views.ptrs(), pattern_views.lengths(),
                        pattern_views.encodings()
                    );
                    normalize_views(
                        pattern_views, pattern_converter, pattern_storage
                    );
                    stage_utf16(pattern_views, patterns);
                    empty_pattern_warnings = count_empty_patterns(patterns);
                }

                result = entry_protections.reprotect_one(
                    Rf_allocVector(LGLSXP, vectorize_length), result_index
                );
                int* output = LOGICAL(result);
                const R_xlen_t tasks = vectorize_length <= 0
                    ? 0
                    : pattern_length == 1
                        ? vectorize_length
                        : pattern_length;
                const shared::ParallelPlan plan = shared::parallel_plan(
                    true, tasks
                );
                Body body(
                    subject_views, patterns, INTEGER_RO(to),
                    position_length, vectorize_length, options,
                    false, negate_1, output, root_fallback_warning
                );
                shared::run_parallel(plan, tasks, body);

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_warnings(
            root_fallback_warning,
            recycling_warning,
            empty_pattern_warnings
        );
    );
}

} } // namespace charr::altrep_backend
