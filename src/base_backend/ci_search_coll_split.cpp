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

namespace search_coll_split {

CHARR_NEUTRAL_HELPER R_len_t recycling_length(
    R_len_t subject_length,
    R_len_t pattern_length,
    R_len_t n_length,
    R_len_t omit_empty_length,
    bool& warning
) noexcept
{
    warning = false;
    if (subject_length <= 0 || pattern_length <= 0 || n_length <= 0 ||
            omit_empty_length <= 0) {
        return 0;
    }

    R_len_t result = subject_length;
    if (pattern_length > result)
        result = pattern_length;
    if (n_length > result)
        result = n_length;
    if (omit_empty_length > result)
        result = omit_empty_length;
    warning = result % subject_length != 0 ||
        result % pattern_length != 0 ||
        result % n_length != 0 ||
        result % omit_empty_length != 0;
    return result;
}


CHARR_NEUTRAL_HELPER R_len_t minimum_columns(
    const int* values, R_len_t size
) noexcept
{
    R_len_t result = 0;
    for (R_len_t i = 0; i < size; ++i) {
        if (values[i] != NA_INTEGER && values[i] > result)
            result = values[i];
    }
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


CHARR_R_HELPER SEXP empty_strings_r(R_len_t size) noexcept
{
    SEXP result = Rf_allocVector(STRSXP, size);
    for (R_len_t i = 0; i < size; ++i)
        SET_STRING_ELT(result, i, R_BlankString);
    return result;
}


CHARR_R_HELPER SEXP strings_r(R_len_t size) noexcept
{
    return Rf_allocVector(STRSXP, size);
}


CHARR_R_HELPER void set_utf8_slice_r(
    SEXP output, R_len_t index,
    const shared::CollationUtf8Slice& value
) noexcept
{
    SET_STRING_ELT(
        output, index,
        Rf_mkCharLenCE(value.data, value.length, CE_UTF8)
    );
}


CHARR_R_HELPER SEXP simplify_result_r(
    SEXP input, R_len_t rows, R_len_t columns, bool pad_na
) noexcept
{
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


CHARR_R_HELPER void emit_warnings_r(
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

} // namespace search_coll_split

using namespace search_coll_split;


/** Split strings around collation-aware pattern occurrences. */
CHARR_ENTRYPOINT SEXP ci_split_coll(
    SEXP str, SEXP pattern, SEXP n, SEXP omit_empty,
    SEXP tokens_only, SEXP simplify, SEXP opts_collator
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool tokens_only_value = ci__prepare_arg_logical_1_notNA_r(
        tokens_only, "tokens_only"
    );
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    pattern = entry_protections.protect_one(ci__prepare_arg_string_r(pattern, "pattern"));
    n = entry_protections.protect_one(ci__prepare_arg_integer_r(n, "n"));
    omit_empty = entry_protections.protect_one(ci__prepare_arg_logical_r(
        omit_empty, "omit_empty"
    ));
    simplify = entry_protections.protect_one(ci__prepare_arg_logical_1_r(
        simplify, "simplify"
    ));
    const shared::CollatorOptions options =
        collator::prepare_options(opts_collator);

    const R_len_t subject_length = LENGTH(str);
    const R_len_t pattern_length = LENGTH(pattern);
    const R_len_t n_length = LENGTH(n);
    const R_len_t omit_empty_length = LENGTH(omit_empty);
    bool recycling_needed = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, n_length,
        omit_empty_length, recycling_needed
    );
    const int simplify_value = LOGICAL_RO(simplify)[0];
    const int* n_values = nullptr;
    const int* omit_empty_values = nullptr;
    R_len_t max_columns = 0;

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
        std::vector<shared::CollationRange> fields;
        std::vector<char> utf8_buffer;

        fields.reserve(4);

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
                    const SEXP* values = STRING_PTR_RO(str);
                    for (R_len_t i = 0; i < subject_length; ++i) {
                        subjects.set(
                            static_cast<std::size_t>(i),
                            normalize_view(
                                io::as_shared_view(values[i]),
                                subject_converter, subject_storage
                            )
                        );
                    }

                    patterns.resize(
                        static_cast<std::size_t>(pattern_length)
                    );
                    values = STRING_PTR_RO(pattern);
                    for (R_len_t i = 0; i < pattern_length; ++i) {
                        patterns.set(
                            static_cast<std::size_t>(i),
                            normalize_view(
                                io::as_shared_view(values[i]),
                                pattern_converter, pattern_storage
                            )
                        );
                    }
                    empty_pattern_warnings =
                        count_empty_patterns(patterns);
                }

                n_values = INTEGER_RO(n);
                omit_empty_values = LOGICAL_RO(omit_empty);
                max_columns = minimum_columns(n_values, n_length);

                result = entry_protections.reprotect_one(
                    Rf_allocVector(VECSXP, vectorize_length),
                    result_index
                );
                SEXP child = R_NilValue;
                PROTECT_INDEX child_index;
                callback_protections.protect_with_index(child, &child_index);

                for (R_len_t lane = 0;
                        lane < (vectorize_length > 0
                            ? pattern_length : 0);
                        ++lane) {
                    const shared::CollationInput prepared_pattern =
                        patterns.get(static_cast<std::size_t>(lane));
                    R_len_t i = lane;
                    for (;;) {
                        const int raw_n = n_values[i % n_length];
                        const int raw_omit = omit_empty_values[
                            i % omit_empty_length
                        ];
                        const bool omit = raw_omit != NA_LOGICAL &&
                            raw_omit != 0;
                        const shared::CollationInput subject = subjects.get(
                            static_cast<std::size_t>(i % subject_length)
                        );

                        if (raw_n == NA_INTEGER || subject.missing ||
                                prepared_pattern.missing ||
                                prepared_pattern.length <= 0) {
                            child = callback_protections.reprotect_slot(
                                missing_strings_r(1), child_index
                            );
                        }
                        else if (subject.length <= 0) {
                            if (raw_omit == NA_LOGICAL) {
                                child = callback_protections.reprotect_slot(
                                    missing_strings_r(1), child_index
                                );
                            }
                            else {
                                const R_len_t child_size =
                                    omit || raw_n == 0 ? 0 : 1;
                                child = callback_protections.reprotect_slot(
                                    empty_strings_r(child_size), child_index
                                );
                            }
                        }
                        else {
                            UErrorCode status = U_ZERO_ERROR;
                            const shared::CollationSplitResult split_result =
                                matcher.split(
                                    collator_owner.get(), subject,
                                    prepared_pattern, raw_n, omit,
                                    tokens_only_value, fields, status
                                );
                            require_icu_success(status);
                            if (split_result ==
                                    shared::CollationSplitResult::
                                        limit_too_large) {
                                throw StriException(
                                    MSG__INCORRECT_NAMED_ARG "; "
                                    MSG__EXPECTED_SMALLER,
                                    "n"
                                );
                            }

                            const R_len_t child_size =
                                static_cast<R_len_t>(fields.size());
                            child = callback_protections.reprotect_slot(
                                strings_r(child_size), child_index
                            );
                            for (R_len_t j = 0; j < child_size; ++j) {
                                const shared::CollationRange& field = fields[
                                    static_cast<std::size_t>(j)
                                ];
                                if (raw_omit == NA_LOGICAL &&
                                        field.start == field.end) {
                                    SET_STRING_ELT(child, j, NA_STRING);
                                }
                                else {
                                    status = U_ZERO_ERROR;
                                    const shared::CollationUtf8Slice value =
                                        shared::collation_utf8_slice(
                                            subject, field,
                                            utf8_buffer, status
                                        );
                                    require_icu_success(status);
                                    set_utf8_slice_r(child, j, value);
                                }
                            }
                        }

                        SET_VECTOR_ELT(result, i, child);
                        const R_len_t child_size = LENGTH(child);
                        if (max_columns < child_size)
                            max_columns = child_size;

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
        emit_warnings_r(
            root_fallback_warning,
            recycling_warning,
            empty_pattern_warnings
        );
    );
}

} } // namespace charr::base_backend
