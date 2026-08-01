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
#include "boundary/options_r.h"
#include "io/string_view.h"
#include "../shared/boundary_iterator.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <climits>
#include <cstddef>
#include <exception>
#include <vector>


namespace charr { namespace base_backend {

namespace search_boundaries_split {

CHARR_NEUTRAL_HELPER R_len_t recycling_length(
    R_len_t string_length, R_len_t n_length, bool& warning
) noexcept {
    warning = false;
    if (string_length <= 0 || n_length <= 0)
        return 0;

    const R_len_t result = string_length > n_length
        ? string_length
        : n_length;
    warning = result % string_length != 0 || result % n_length != 0;
    return result;
}


CHARR_CXX_HELPER void require_icu_success(UErrorCode status)
{
    if (U_FAILURE(status))
        throw StriException(status);
}


CHARR_CXX_HELPER void ensure_iterator(
    shared::BoundaryIterator& iterator,
    const shared::BoundaryOptions& options,
    bool& opened,
    bool& root_fallback
)
{
    if (opened)
        return;
    const shared::BoundaryOpenResult result = iterator.reset(options);
    root_fallback = result.root_fallback;
    require_icu_success(result.status);
    opened = true;
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


CHARR_R_HELPER void emit_warnings_r(
    bool recycling_warning, bool root_fallback_warning
) noexcept {
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);
    if (root_fallback_warning) {
        Rf_warning(
            "%s", ICUError::getICUerrorName(U_USING_DEFAULT_WARNING)
        );
    }
}

} // namespace search_boundaries_split

using namespace search_boundaries_split;

/** Split a string at BreakIterator boundaries
 *
 * @param str character vector
 * @param n integer
 * @param tokens_only logical
 * @param simplify logical
 * @param opts_brkiter named list
 * @return list
 *
 * @version 0.2-2 (Marek Gagolewski, 2014-04-21)
 *
 * @version 0.2-2 (Marek Gagolewski, 2014-04-23)
 *          removed "title": For Unicode 4.0 and above title boundary
 *          iteration, please use Word Boundary iterator.
 *
 * @version 0.2-2 (Marek Gagolewski, 2014-04-25)
 *          use ci__split_or_locate_boundaries
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-10-29)
 *          use opts_brkiter
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-28)
 *          new args: n, tokens_only, simplify
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-02)
 *          use boundary::Utf8Iterator
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-04)
 *    allow `simplify=NA`; FR #126: pass n to ci_list2matrix
 */
CHARR_ENTRYPOINT SEXP ci_split_boundaries(
    SEXP str, SEXP n, SEXP tokens_only, SEXP simplify,
    SEXP opts_brkiter
) noexcept {
    CHARR_ENTRYPOINT_BEGIN();

    const bool tokens_only_value = ci__prepare_arg_logical_1_notNA_r(
        tokens_only, "tokens_only"
    );
    simplify = entry_protections.protect_one(ci__prepare_arg_logical_1_r(
        simplify, "simplify"
    ));
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    n = entry_protections.protect_one(ci__prepare_arg_integer_r(n, "n"));
    const shared::BoundaryOptions options =
        boundary::prepare_options_r(opts_brkiter, UBRK_LINE);

    const int simplify_value = LOGICAL_RO(simplify)[0];

    bool recycling_warning = false;
    bool root_fallback_warning = false;
    bool iterator_open = false;

    try {
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        std::vector<shared::StringView> normalized;
        std::vector<shared::BoundaryRange> ranges;
        shared::BoundaryIterator iterator;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t string_length = LENGTH(str);
                const R_len_t n_length = LENGTH(n);
                const R_len_t vectorize_length = recycling_length(
                    string_length, n_length, recycling_warning
                );

                result = entry_protections.reprotect_one(
                    Rf_allocVector(VECSXP, vectorize_length), result_index
                );
                SEXP child = R_NilValue;
                PROTECT_INDEX child_index;
                callback_protections.protect_with_index(child, &child_index);

                const int* n_values = INTEGER_RO(n);
                const bool scalar_unlimited_n = n_length == 1 &&
                    n_values[0] != NA_INTEGER && n_values[0] < 0;
                R_len_t max_columns = 0;
                if (simplify_value == NA_LOGICAL || simplify_value) {
                    for (R_len_t i = 0; i < n_length; ++i) {
                        const int value = n_values[i];
                        if (value != NA_INTEGER && value > max_columns)
                            max_columns = value;
                    }
                }

                if (vectorize_length > 0) {
                    normalized.resize(
                        static_cast<std::size_t>(string_length)
                    );
                    const SEXP* values = STRING_PTR_RO(str);
                    for (R_len_t i = 0; i < string_length; ++i) {
                        const shared::StringView source =
                            io::as_shared_view(values[i]);
                        if (source.enc == shared::StringEncoding::bytes)
                            throw StriException(MSG__BYTESENC);
                        normalized[static_cast<std::size_t>(i)] =
                            shared::normalize_utf8(
                                source, converter, storage
                            );
                    }

                    R_len_t string_index = 0;
                    R_len_t n_index = 0;
                    for (R_len_t i = 0; i < vectorize_length; ++i) {
                        int raw_limit;
                        if (scalar_unlimited_n) {
                            raw_limit = INT_MAX;
                        }
                        else {
                            raw_limit = n_values[n_index];
                            if (++n_index == n_length)
                                n_index = 0;
                        }
                        const shared::StringView& value = normalized[
                            static_cast<std::size_t>(string_index)
                        ];
                        if (++string_index == string_length)
                            string_index = 0;

                        if (!scalar_unlimited_n &&
                                raw_limit == NA_INTEGER) {
                            child = callback_protections.reprotect_slot(
                                Rf_allocVector(STRSXP, 1), child_index
                            );
                            SET_STRING_ELT(child, 0, NA_STRING);
                            SET_VECTOR_ELT(result, i, child);
                            if (max_columns < 1)
                                max_columns = 1;
                            continue;
                        }
                        if (value.is_na()) {
                            child = callback_protections.reprotect_slot(
                                Rf_allocVector(STRSXP, 1), child_index
                            );
                            SET_STRING_ELT(child, 0, NA_STRING);
                            SET_VECTOR_ELT(result, i, child);
                            if (max_columns < 1)
                                max_columns = 1;
                            continue;
                        }
                        if (!scalar_unlimited_n &&
                                raw_limit >= INT_MAX-1) {
                            throw StriException(
                                MSG__INCORRECT_NAMED_ARG "; "
                                MSG__EXPECTED_SMALLER,
                                "n"
                            );
                        }

                        const int limit = raw_limit < 0
                            ? INT_MAX
                            : raw_limit;
                        if (limit == 0) {
                            child = callback_protections.reprotect_slot(
                                Rf_allocVector(STRSXP, 0), child_index
                            );
                            SET_VECTOR_ELT(result, i, child);
                            continue;
                        }

                        ensure_iterator(
                            iterator, options, iterator_open,
                            root_fallback_warning
                        );
                        require_icu_success(iterator.set_text(value));
                        iterator.first();
                        ranges.clear();
                        shared::BoundaryRange range{0, 0};
                        while (ranges.size() <
                                static_cast<std::size_t>(limit) &&
                                iterator.next(range)) {
                            ranges.push_back(range);
                        }

                        if (ranges.size() ==
                                static_cast<std::size_t>(limit) &&
                                !tokens_only_value && ranges.size() > 0) {
                            ranges[ranges.size()-1].end = value.len;
                        }

                        const R_len_t range_count =
                            static_cast<R_len_t>(ranges.size());
                        child = callback_protections.reprotect_slot(
                            Rf_allocVector(STRSXP, range_count),
                            child_index
                        );
                        for (R_len_t j = 0; j < range_count; ++j) {
                            const shared::BoundaryRange& current = ranges[
                                static_cast<std::size_t>(j)
                            ];
                            SET_STRING_ELT(
                                child, j,
                                Rf_mkCharLenCE(
                                    value.ptr + current.start,
                                    current.end - current.start,
                                    CE_UTF8
                                )
                            );
                        }
                        SET_VECTOR_ELT(result, i, child);
                        if (max_columns < range_count)
                            max_columns = range_count;
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
        emit_warnings_r(recycling_warning, root_fallback_warning);
    );
}

} } // namespace charr::base_backend
