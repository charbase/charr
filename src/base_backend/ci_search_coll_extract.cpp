// Derived from stringi 19e9586ba39b3320df49355e32bd18d74ed6098f.
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
#include <vector>

namespace charr { namespace base_backend {

namespace search_coll_extract {

CHARR_NEUTRAL_HELPER R_len_t recycling_length(
    R_len_t subject_length, R_len_t pattern_length, bool& warning
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


CHARR_CXX_HELPER void require_icu_success(UErrorCode status)
{
    if (U_FAILURE(status))
        throw StriException(status);
}


CHARR_CXX_HELPER shared::StringView normalize_view(
    const shared::StringView& source,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage
)
{
    if (source.enc == shared::StringEncoding::bytes)
        throw StriException(MSG__BYTESENC);
    return shared::normalize_utf8_preserve_bom(
        source, converter, storage
    );
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


CHARR_R_HELPER SEXP missing_strings_r(R_len_t size) noexcept
{
    SEXP result = Rf_allocVector(STRSXP, size);
    for (R_len_t i = 0; i < size; ++i)
        SET_STRING_ELT(result, i, NA_STRING);
    return result;
}


CHARR_R_HELPER void set_utf8_slice_r(
    SEXP output, R_len_t index,
    const shared::CollationUtf8Slice& value
) noexcept {
    SET_STRING_ELT(
        output, index,
        Rf_mkCharLenCE(value.data, value.length, CE_UTF8)
    );
}


CHARR_R_HELPER SEXP simplify_result_r(
    SEXP input, R_len_t rows, R_len_t columns, bool pad_na
) noexcept {
    SEXP output = Rf_allocMatrix(STRSXP, rows, columns);
    const SEXP fill = pad_na ? NA_STRING : R_BlankString;
    for (R_len_t i = 0; i < rows; ++i) {
        const SEXP current = VECTOR_ELT(input, i);
        const R_len_t current_size = LENGTH(current);
        R_len_t j = 0;
        for (; j < current_size; ++j) {
            SET_STRING_ELT(
                output,
                static_cast<R_xlen_t>(i) +
                    static_cast<R_xlen_t>(j)*rows,
                STRING_ELT(current, j)
            );
        }
        for (; j < columns; ++j) {
            SET_STRING_ELT(
                output,
                static_cast<R_xlen_t>(i) +
                    static_cast<R_xlen_t>(j)*rows,
                fill
            );
        }
    }
    return output;
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

} // namespace search_coll_extract

using namespace search_coll_extract;


/** Extract the first collation-aware pattern occurrence. */
CHARR_ENTRYPOINT SEXP ci_extract_first_coll(
    SEXP str, SEXP pattern, SEXP opts_collator
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    pattern = entry_protections.protect_one(ci__prepare_arg_string_r(pattern, "pattern"));
    const shared::CollatorOptions options =
        collator::prepare_options(opts_collator);

    const R_len_t subject_length = LENGTH(str);
    const R_len_t pattern_length = LENGTH(pattern);
    bool recycling_needed = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, recycling_needed
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
        std::vector<char> utf8_buffer;

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
                        subjects.set(
                            static_cast<std::size_t>(i),
                            normalize_view(
                                io::as_shared_view(subject_values[i]),
                                subject_converter, subject_storage
                            )
                        );
                    }

                    patterns.resize(
                        static_cast<std::size_t>(pattern_length)
                    );
                    const SEXP* pattern_values = STRING_PTR_RO(pattern);
                    for (R_len_t i = 0; i < pattern_length; ++i) {
                        patterns.set(
                            static_cast<std::size_t>(i),
                            normalize_view(
                                io::as_shared_view(pattern_values[i]),
                                pattern_converter, pattern_storage
                            )
                        );
                    }
                    empty_pattern_warnings =
                        count_empty_patterns(patterns);
                }

                result = entry_protections.reprotect_one(
                    Rf_allocVector(STRSXP, vectorize_length),
                    result_index
                );
                for (R_len_t lane = 0;
                        lane < (vectorize_length > 0 ? pattern_length : 0);
                        ++lane) {
                    const shared::CollationInput prepared_pattern =
                        patterns.get(static_cast<std::size_t>(lane));
                    R_len_t i = lane;
                    for (;;) {
                        const shared::CollationInput subject = subjects.get(
                            static_cast<std::size_t>(i % subject_length)
                        );
                        if (subject.missing || subject.length <= 0 ||
                                prepared_pattern.missing ||
                                prepared_pattern.length <= 0) {
                            SET_STRING_ELT(result, i, NA_STRING);
                        }
                        else {
                            UErrorCode status = U_ZERO_ERROR;
                            shared::CollationRange match{0, 0};
                            const bool found = matcher.find_first(
                                collator_owner.get(), subject,
                                prepared_pattern, match, status
                            );
                            require_icu_success(status);
                            if (!found) {
                                SET_STRING_ELT(result, i, NA_STRING);
                            }
                            else {
                                const shared::CollationUtf8Slice slice =
                                    shared::collation_utf8_slice(
                                        subject, match, utf8_buffer, status
                                    );
                                require_icu_success(status);
                                set_utf8_slice_r(result, i, slice);
                            }
                        }

                        if (pattern_length >= vectorize_length-i)
                            break;
                        i += pattern_length;
                    }
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


/** Extract every collation-aware pattern occurrence. */
CHARR_ENTRYPOINT SEXP ci_extract_all_coll(
    SEXP str, SEXP pattern, SEXP simplify,
    SEXP omit_no_match, SEXP opts_collator
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool omit = ci__prepare_arg_logical_1_notNA_r(
        omit_no_match, "omit_no_match"
    );
    simplify = entry_protections.protect_one(ci__prepare_arg_logical_1_r(
        simplify, "simplify"
    ));
    const int simplify_value = LOGICAL_RO(simplify)[0];
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    pattern = entry_protections.protect_one(ci__prepare_arg_string_r(pattern, "pattern"));
    const shared::CollatorOptions options =
        collator::prepare_options(opts_collator);

    const R_len_t subject_length = LENGTH(str);
    const R_len_t pattern_length = LENGTH(pattern);
    bool recycling_needed = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, recycling_needed
    );

    bool root_fallback_warning = false;
    bool recycling_warning = false;
    R_len_t empty_pattern_warnings = 0;
    R_len_t max_columns = 0;

    try {
        shared::Collator collator_owner;
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        shared::CollationInputs subjects;
        shared::CollationInputs patterns;
        shared::CollationMatcher matcher;
        std::vector<shared::CollationRange> matches;
        std::vector<char> utf8_buffer;

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
                        subjects.set(
                            static_cast<std::size_t>(i),
                            normalize_view(
                                io::as_shared_view(subject_values[i]),
                                subject_converter, subject_storage
                            )
                        );
                    }

                    patterns.resize(
                        static_cast<std::size_t>(pattern_length)
                    );
                    const SEXP* pattern_values = STRING_PTR_RO(pattern);
                    for (R_len_t i = 0; i < pattern_length; ++i) {
                        patterns.set(
                            static_cast<std::size_t>(i),
                            normalize_view(
                                io::as_shared_view(pattern_values[i]),
                                pattern_converter, pattern_storage
                            )
                        );
                    }
                    empty_pattern_warnings =
                        count_empty_patterns(patterns);
                }

                result = entry_protections.reprotect_one(
                    Rf_allocVector(VECSXP, vectorize_length),
                    result_index
                );
                SEXP current = R_NilValue;
                PROTECT_INDEX current_index;
                callback_protections.protect_with_index(current, &current_index);

                for (R_len_t lane = 0;
                        lane < (vectorize_length > 0 ? pattern_length : 0);
                        ++lane) {
                    const shared::CollationInput prepared_pattern =
                        patterns.get(static_cast<std::size_t>(lane));
                    R_len_t i = lane;
                    for (;;) {
                        const shared::CollationInput subject = subjects.get(
                            static_cast<std::size_t>(i % subject_length)
                        );
                        if (subject.missing || prepared_pattern.missing ||
                                prepared_pattern.length <= 0) {
                            current = callback_protections.reprotect_slot(
                                missing_strings_r(1), current_index
                            );
                            if (max_columns < 1)
                                max_columns = 1;
                        }
                        else if (subject.length <= 0) {
                            const R_len_t child_size = omit ? 0 : 1;
                            current = callback_protections.reprotect_slot(
                                missing_strings_r(child_size), current_index
                            );
                            if (max_columns < child_size)
                                max_columns = child_size;
                        }
                        else {
                            UErrorCode status = U_ZERO_ERROR;
                            matcher.find_all(
                                collator_owner.get(), subject,
                                prepared_pattern, matches, status
                            );
                            require_icu_success(status);
                            const R_len_t match_count =
                                static_cast<R_len_t>(matches.size());
                            if (match_count == 0) {
                                const R_len_t child_size = omit ? 0 : 1;
                                current = callback_protections.reprotect_slot(
                                    missing_strings_r(child_size),
                                    current_index
                                );
                                if (max_columns < child_size)
                                    max_columns = child_size;
                            }
                            else {
                                current = callback_protections.reprotect_slot(
                                    Rf_allocVector(STRSXP, match_count),
                                    current_index
                                );
                                for (R_len_t j = 0; j < match_count; ++j) {
                                    const shared::CollationUtf8Slice slice =
                                        shared::collation_utf8_slice(
                                            subject,
                                            matches[static_cast<std::size_t>(j)],
                                            utf8_buffer, status
                                        );
                                    require_icu_success(status);
                                    set_utf8_slice_r(current, j, slice);
                                }
                                if (max_columns < match_count)
                                    max_columns = match_count;
                            }
                        }

                        SET_VECTOR_ELT(result, i, current);
                        if (pattern_length >= vectorize_length-i)
                            break;
                        i += pattern_length;
                    }
                }

                if (simplify_value == NA_LOGICAL || simplify_value) {
                    result = entry_protections.reprotect_one(
                        simplify_result_r(
                            result, vectorize_length, max_columns,
                            simplify_value == NA_LOGICAL
                        ),
                        result_index
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
