// Copied from stringi 19e9586ba39b3320df49355e32bd18d74ed6098f; stri_* renamed to ci_*. See inst/COPYRIGHTS.
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
#include "collator/options.h"
#include "io/string_view.h"
#include "../shared/collation_search.h"
#include "../shared/collator.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <cstddef>
#include <exception>


namespace charr { namespace base_backend {

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


CHARR_CXX_HELPER R_len_t count_empty_patterns(
    const shared::CollationInputs& patterns
)
{
    R_len_t result = 0;
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        const shared::CollationInput pattern = patterns.get(i);
        if (!pattern.missing && pattern.length <= 0)
            ++result;
    }
    return result;
}


CHARR_CXX_HELPER void match_normalized(
    const shared::CollationInputs& subjects,
    const shared::CollationInputs& patterns,
    const int* positions,
    R_len_t subject_length,
    R_len_t pattern_length,
    R_len_t position_length,
    R_len_t vectorize_length,
    UCollator* collator,
    bool starts,
    bool negate,
    shared::CollationMatcher& matcher,
    int* output
)
{
    for (R_len_t lane = 0; lane < pattern_length; ++lane) {
        const shared::CollationInput pattern = patterns.get(
            static_cast<std::size_t>(lane)
        );
        R_len_t i = lane;
        for (;;) {
            const shared::CollationInput subject = subjects.get(
                static_cast<std::size_t>(i % subject_length)
            );

            if (subject.missing || pattern.missing || pattern.length <= 0) {
                output[i] = NA_LOGICAL;
            }
            else if (subject.length <= 0) {
                output[i] = negate;
            }
            else {
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

            if (pattern_length >= vectorize_length-i)
                break;
            i += pattern_length;
        }
    }
}


CHARR_R_HELPER void emit_warnings(
    bool root_fallback_warning,
    bool recycling_warning,
    R_len_t empty_pattern_warnings
) noexcept
{
    if (root_fallback_warning) {
        Rf_warning(
            "%s", ICUError::getICUerrorName(U_USING_DEFAULT_WARNING)
        );
    }
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);
    for (R_len_t i = 0; i < empty_pattern_warnings; ++i)
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
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    pattern = entry_protections.protect_one(ci__prepare_arg_string_r(pattern, "pattern"));
    from = entry_protections.protect_one(ci__prepare_arg_integer_r(from, "from"));
    const shared::CollatorOptions options =
        collator::prepare_options(opts_collator);

    const R_len_t subject_length = LENGTH(str);
    const R_len_t pattern_length = LENGTH(pattern);
    const R_len_t position_length = LENGTH(from);
    bool recycling_needed = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, position_length,
        recycling_needed
    );

    bool root_fallback_warning = false;
    bool recycling_warning = false;
    R_len_t empty_pattern_warnings = 0;

    try {
        shared::Collator collator_owner;
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        shared::CollationInputs subjects;
        shared::CollationInputs patterns;
        shared::CollationMatcher matcher;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const shared::CollatorOpenResult opened =
                    collator_owner.reset(options);
                root_fallback_warning = opened.root_fallback;
                require_icu_success(opened.status);
                recycling_warning = recycling_needed;

                if (vectorize_length > 0) {
                    subjects.resize(
                        static_cast<std::size_t>(subject_length)
                    );
                    const SEXP* subject_values = STRING_PTR_RO(str);
                    for (R_len_t i = 0; i < subject_length; ++i) {
                        const shared::StringView value =
                            shared::normalize_utf8_preserve_bom(
                                io::as_shared_view(subject_values[i]),
                                subject_converter, subject_storage
                            );
                        subjects.set(static_cast<std::size_t>(i), value);
                    }

                    patterns.resize(
                        static_cast<std::size_t>(pattern_length)
                    );
                    const SEXP* pattern_values = STRING_PTR_RO(pattern);
                    for (R_len_t i = 0; i < pattern_length; ++i) {
                        const shared::StringView value =
                            shared::normalize_utf8_preserve_bom(
                                io::as_shared_view(pattern_values[i]),
                                pattern_converter, pattern_storage
                            );
                        patterns.set(static_cast<std::size_t>(i), value);
                    }
                    empty_pattern_warnings = count_empty_patterns(patterns);
                }

                result = entry_protections.reprotect_one(
                    Rf_allocVector(LGLSXP, vectorize_length), result_index
                );
                int* output = LOGICAL(result);
                if (vectorize_length > 0) {
                    match_normalized(
                        subjects, patterns, INTEGER_RO(from),
                        subject_length, pattern_length, position_length,
                        vectorize_length, collator_owner.get(), true,
                        negate_1, matcher, output
                    );
                }

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
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    pattern = entry_protections.protect_one(ci__prepare_arg_string_r(pattern, "pattern"));
    to = entry_protections.protect_one(ci__prepare_arg_integer_r(to, "to"));
    const shared::CollatorOptions options =
        collator::prepare_options(opts_collator);

    const R_len_t subject_length = LENGTH(str);
    const R_len_t pattern_length = LENGTH(pattern);
    const R_len_t position_length = LENGTH(to);
    bool recycling_needed = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, position_length,
        recycling_needed
    );

    bool root_fallback_warning = false;
    bool recycling_warning = false;
    R_len_t empty_pattern_warnings = 0;

    try {
        shared::Collator collator_owner;
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        shared::CollationInputs subjects;
        shared::CollationInputs patterns;
        shared::CollationMatcher matcher;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const shared::CollatorOpenResult opened =
                    collator_owner.reset(options);
                root_fallback_warning = opened.root_fallback;
                require_icu_success(opened.status);
                recycling_warning = recycling_needed;

                if (vectorize_length > 0) {
                    subjects.resize(
                        static_cast<std::size_t>(subject_length)
                    );
                    const SEXP* subject_values = STRING_PTR_RO(str);
                    for (R_len_t i = 0; i < subject_length; ++i) {
                        const shared::StringView value =
                            shared::normalize_utf8_preserve_bom(
                                io::as_shared_view(subject_values[i]),
                                subject_converter, subject_storage
                            );
                        subjects.set(static_cast<std::size_t>(i), value);
                    }

                    patterns.resize(
                        static_cast<std::size_t>(pattern_length)
                    );
                    const SEXP* pattern_values = STRING_PTR_RO(pattern);
                    for (R_len_t i = 0; i < pattern_length; ++i) {
                        const shared::StringView value =
                            shared::normalize_utf8_preserve_bom(
                                io::as_shared_view(pattern_values[i]),
                                pattern_converter, pattern_storage
                            );
                        patterns.set(static_cast<std::size_t>(i), value);
                    }
                    empty_pattern_warnings = count_empty_patterns(patterns);
                }

                result = entry_protections.reprotect_one(
                    Rf_allocVector(LGLSXP, vectorize_length), result_index
                );
                int* output = LOGICAL(result);
                if (vectorize_length > 0) {
                    match_normalized(
                        subjects, patterns, INTEGER_RO(to),
                        subject_length, pattern_length, position_length,
                        vectorize_length, collator_owner.get(), false,
                        negate_1, matcher, output
                    );
                }

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

} } // namespace charr::base_backend
