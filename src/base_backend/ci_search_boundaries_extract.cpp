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

#include <cstddef>
#include <exception>
#include <vector>

namespace charr { namespace base_backend {

namespace search_boundaries_extract {

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


CHARR_R_HELPER SEXP missing_strings_r(R_len_t size) noexcept
{
    SEXP result = Rf_allocVector(STRSXP, size);
    for (R_len_t i = 0; i < size; ++i)
        SET_STRING_ELT(result, i, NA_STRING);
    return result;
}


CHARR_R_HELPER void set_slice_r(
    SEXP output, R_len_t index,
    const shared::StringView& value,
    const shared::BoundaryRange& range
) noexcept {
    SET_STRING_ELT(
        output, index,
        Rf_mkCharLenCE(
            value.ptr + range.start,
            range.end - range.start,
            CE_UTF8
        )
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


CHARR_R_HELPER void emit_fallback_warning_r() noexcept
{
    Rf_warning(
        "%s", ICUError::getICUerrorName(U_USING_DEFAULT_WARNING)
    );
}

} // namespace search_boundaries_extract

using namespace search_boundaries_extract;


/**
 * Extract first text between boundaries
 *
 * @param str character vector
 * @param opts_brkiter list
 * @return character vector
 *
 * @version 0.5-1 (Marek Gagolewski, 2014-12-19)
 */
CHARR_ENTRYPOINT SEXP ci_extract_first_boundaries(
    SEXP str, SEXP opts_brkiter
) noexcept {
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    const shared::BoundaryOptions options =
        boundary::prepare_options_r(opts_brkiter, UBRK_LINE);

    bool iterator_open = false;
    bool root_fallback_warning = false;

    try {
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        std::vector<shared::StringView> normalized;
        shared::BoundaryIterator iterator;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t length = LENGTH(str);
                result = entry_protections.reprotect_one(
                    Rf_allocVector(STRSXP, length), result_index
                );

                normalized.resize(static_cast<std::size_t>(length));
                const SEXP* values = length > 0
                    ? STRING_PTR_RO(str)
                    : nullptr;
                for (R_len_t i = 0; i < length; ++i) {
                    const shared::StringView source =
                        io::as_shared_view(values[i]);
                    if (source.enc == shared::StringEncoding::bytes)
                        throw StriException(MSG__BYTESENC);
                    normalized[static_cast<std::size_t>(i)] =
                        shared::normalize_utf8(source, converter, storage);
                }

                const bool ascii_word_first =
                    shared::boundary_ascii_word_first(options);
                for (R_len_t i = 0; i < length; ++i) {
                    const shared::StringView& value = normalized[
                        static_cast<std::size_t>(i)
                    ];
                    if (value.is_na() || value.len == 0) {
                        SET_STRING_ELT(result, i, NA_STRING);
                        continue;
                    }

                    int ascii_word_end = 0;
                    if (ascii_word_first &&
                            shared::boundary_ascii_initial_word(
                                value.ptr, value.len, ascii_word_end
                            )) {
                        const shared::BoundaryRange range{
                            0, ascii_word_end
                        };
                        set_slice_r(result, i, value, range);
                        continue;
                    }

                    ensure_iterator(
                        iterator, options, iterator_open,
                        root_fallback_warning
                    );
                    require_icu_success(iterator.set_text(value));
                    iterator.first();
                    shared::BoundaryRange range{0, 0};
                    if (!iterator.next(range)) {
                        SET_STRING_ELT(result, i, NA_STRING);
                        continue;
                    }
                    set_slice_r(result, i, value, range);
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        if (root_fallback_warning)
            emit_fallback_warning_r();
    );
}


/** Extract all text between boundaries
 *
 * @param str character vector
 * @param simplify logical
 * @param omit_no_match logical
 * @param opts_brkiter named list
 * @return list or matrix
 *
 * @version 0.5-1 (Marek Gagolewski, 2014-12-19)
 */
CHARR_ENTRYPOINT SEXP ci_extract_all_boundaries(
    SEXP str, SEXP simplify, SEXP omit_no_match, SEXP opts_brkiter
) noexcept {
    CHARR_ENTRYPOINT_BEGIN();

    const bool omit = ci__prepare_arg_logical_1_notNA_r(
        omit_no_match, "omit_no_match"
    );
    simplify = entry_protections.protect_one(ci__prepare_arg_logical_1_r(
        simplify, "simplify"
    ));
    const int simplify_value = LOGICAL_RO(simplify)[0];
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    const shared::BoundaryOptions options =
        boundary::prepare_options_r(opts_brkiter, UBRK_LINE);

    bool iterator_open = false;
    bool root_fallback_warning = false;
    R_len_t max_columns = 0;

    try {
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        std::vector<shared::StringView> normalized;
        std::vector<shared::BoundaryRange> ranges;
        shared::BoundaryIterator iterator;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t length = LENGTH(str);
                result = entry_protections.reprotect_one(
                    Rf_allocVector(VECSXP, length), result_index
                );
                SEXP current = R_NilValue;
                PROTECT_INDEX current_index;
                callback_protections.protect_with_index(current, &current_index);

                normalized.resize(static_cast<std::size_t>(length));
                const SEXP* values = length > 0
                    ? STRING_PTR_RO(str)
                    : nullptr;
                for (R_len_t i = 0; i < length; ++i) {
                    const shared::StringView source =
                        io::as_shared_view(values[i]);
                    if (source.enc == shared::StringEncoding::bytes)
                        throw StriException(MSG__BYTESENC);
                    normalized[static_cast<std::size_t>(i)] =
                        shared::normalize_utf8(source, converter, storage);
                }

                for (R_len_t i = 0; i < length; ++i) {
                    const shared::StringView& value = normalized[
                        static_cast<std::size_t>(i)
                    ];
                    if (value.is_na()) {
                        current = callback_protections.reprotect_slot(
                            missing_strings_r(1), current_index
                        );
                        SET_VECTOR_ELT(result, i, current);
                        if (max_columns < 1)
                            max_columns = 1;
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
                    while (iterator.next(range))
                        ranges.push_back(range);

                    const R_len_t range_count = static_cast<R_len_t>(
                        ranges.size()
                    );
                    if (range_count == 0) {
                        const R_len_t child_size = omit ? 0 : 1;
                        current = callback_protections.reprotect_slot(
                            missing_strings_r(child_size), current_index
                        );
                        SET_VECTOR_ELT(result, i, current);
                        if (max_columns < child_size)
                            max_columns = child_size;
                        continue;
                    }

                    current = callback_protections.reprotect_slot(
                        Rf_allocVector(STRSXP, range_count), current_index
                    );
                    for (R_len_t j = 0; j < range_count; ++j) {
                        set_slice_r(
                            current, j, value,
                            ranges[static_cast<std::size_t>(j)]
                        );
                    }
                    SET_VECTOR_ELT(result, i, current);
                    if (max_columns < range_count)
                        max_columns = range_count;
                }

                if (simplify_value == NA_LOGICAL || simplify_value) {
                    result = entry_protections.reprotect_one(
                        simplify_result_r(
                            result, length, max_columns,
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
        if (root_fallback_warning)
            emit_fallback_warning_r();
    );
}

} } // namespace charr::base_backend
