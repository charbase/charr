
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
#include "boundary/options_r.h"
#include "io/string_view.h"
#include "io/utf8_output.h"
#include "../shared/boundary_iterator.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <charport.h>

#include <climits>
#include <cstddef>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

namespace charr { namespace altrep_backend {


namespace search_boundaries_split {

CHARR_NEUTRAL_HELPER R_len_t recycling_length(
    R_len_t first, R_len_t second, bool& warning
) noexcept {
    warning = false;
    if (first <= 0 || second <= 0)
        return 0;

    const R_len_t result = first > second ? first : second;
    warning = result % first != 0 || result % second != 0;
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


CHARR_CXX_HELPER void normalize_input(
    const charport::StrViews& source,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    std::vector<shared::StringView>& output
)
{
    output.resize(static_cast<std::size_t>(source.size()));
    for (R_xlen_t i = 0; i < source.size(); ++i) {
        const shared::StringView value = io::as_shared_view(source[i]);
        if (value.enc == shared::StringEncoding::bytes)
            throw StriException(MSG__BYTESENC);
        output[static_cast<std::size_t>(i)] = shared::normalize_utf8(
            value, converter, storage
        );
    }
}


CHARR_NEUTRAL_HELPER io::OutputRecord boundary_record(
    const shared::StringView& value,
    const shared::BoundaryRange& range
) noexcept {
    const char* data = value.ptr + range.start;
    const int length = range.end - range.start;
    const cetype_ext_t encoding = io::is_ascii(
        data, static_cast<std::size_t>(length)
    ) ? CETYPE_EXT_ASCII : CETYPE_EXT_UTF8;
    return charport::StrView{data, length, encoding};
}


CHARR_R_HELPER void emit_recycling_warning_r() noexcept
{
    Rf_warning(MSG__WARN_RECYCLING_RULE);
}


CHARR_R_HELPER void emit_fallback_warning_r() noexcept
{
    Rf_warning(
        "%s", ICUError::getICUerrorName(U_USING_DEFAULT_WARNING)
    );
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
    simplify = entry_protections.protect_one(
        ci__prepare_arg_logical_1_r(
            simplify, "simplify"
        )
    );
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    n = entry_protections.protect_one(
        ci__prepare_arg_integer_r(n, "n")
    );
    const shared::BoundaryOptions options =
        boundary::prepare_options_r(opts_brkiter, UBRK_LINE);

    SEXP temporary = R_NilValue;
    PROTECT_INDEX temporary_index;

    bool recycling_warning = false;
    bool root_fallback_warning = false;
    bool iterator_open = false;
    bool scalar_unlimited_n = false;
    R_len_t vectorize_length = 0;
    R_len_t max_columns = 0;

    try {
        charport::Reader reader;
        charport::StrViews source_views;
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        std::vector<shared::StringView> normalized;
        std::vector<shared::BoundaryRange> ranges;
        shared::BoundaryIterator iterator;
        std::vector<io::OutputStore> stores;
        io::OutputBuilder child_builder(0);
        io::OutputBuilder matrix_builder(0);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const int simplify_value = LOGICAL_RO(simplify)[0];
                const R_len_t source_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                const R_len_t n_length = io::checked_r_len(
                    XLENGTH(n), "integer vectors"
                );
                vectorize_length = recycling_length(
                    source_length, n_length, recycling_warning
                );

                const int* n_values = INTEGER_RO(n);
                scalar_unlimited_n = n_length == 1 &&
                    n_values[0] != NA_INTEGER && n_values[0] < 0;
                if (simplify_value == NA_LOGICAL || simplify_value) {
                    for (R_len_t i = 0; i < n_length; ++i) {
                        const int value = n_values[i];
                        if (value != NA_INTEGER && value > max_columns)
                            max_columns = value;
                    }
                }

                if (vectorize_length > 0) {
                    reader.reset(str);
                    if (reader.size() != source_length) {
                        throw std::runtime_error(
                            "Reader length changed during boundary splitting"
                        );
                    }
                    source_views.resize(source_length);
                    reader.views(
                        0, source_length,
                        source_views.ptrs(), source_views.lengths(),
                        source_views.encodings()
                    );
                    normalize_input(
                        source_views, converter, storage, normalized
                    );
                }

                stores.reserve(
                    static_cast<std::size_t>(vectorize_length)
                );
                for (R_len_t i = 0; i < vectorize_length; ++i)
                    stores.emplace_back(0, 0);

                R_len_t source_index = 0;
                R_len_t n_index = 0;
                for (R_len_t i = 0; i < vectorize_length; ++i) {
                    io::OutputStore& output = stores[
                        static_cast<std::size_t>(i)
                    ];
                    int n_current;
                    if (scalar_unlimited_n) {
                        n_current = INT_MAX;
                    }
                    else {
                        n_current = n_values[n_index];
                        if (++n_index == n_length)
                            n_index = 0;
                    }
                    const R_len_t current_source_index = source_index;
                    if (++source_index == source_length)
                        source_index = 0;
                    if (n_current == NA_INTEGER) {
                        output = io::scalar_store(
                            io::missing_output_record()
                        );
                        continue;
                    }

                    const shared::StringView& value = normalized[
                        static_cast<std::size_t>(current_source_index)
                    ];
                    if (value.is_na()) {
                        output = io::scalar_store(
                            io::missing_output_record()
                        );
                        continue;
                    }

                    if (!scalar_unlimited_n && n_current >= INT_MAX-1) {
                        throw StriException(
                            MSG__INCORRECT_NAMED_ARG "; "
                            MSG__EXPECTED_SMALLER, "n"
                        );
                    }
                    if (n_current < 0)
                        n_current = INT_MAX;
                    if (n_current == 0)
                        continue;

                    ensure_iterator(
                        iterator, options, iterator_open,
                        root_fallback_warning
                    );
                    require_icu_success(iterator.set_text(value));
                    iterator.first();
                    ranges.clear();
                    shared::BoundaryRange range{0, 0};
                    if (scalar_unlimited_n) {
                        while (iterator.next(range))
                            ranges.push_back(range);
                    }
                    else {
                        while (ranges.size() <
                                static_cast<std::size_t>(n_current) &&
                                iterator.next(range)) {
                            ranges.push_back(range);
                        }
                    }

                    const R_len_t range_count = io::checked_r_len(
                        static_cast<R_xlen_t>(ranges.size()),
                        "split results"
                    );
                    if (range_count <= 0)
                        continue;
                    if (!scalar_unlimited_n &&
                            range_count == n_current &&
                            !tokens_only_value) {
                        ranges[static_cast<std::size_t>(range_count-1)].end =
                            value.len;
                    }

                    if (range_count == 1) {
                        output = io::scalar_store(boundary_record(
                            value, ranges[0]
                        ));
                        continue;
                    }

                    child_builder.reset(range_count);
                    for (R_len_t j = 0; j < range_count; ++j) {
                        child_builder.set_validated(
                            j, boundary_record(
                                value,
                                ranges[static_cast<std::size_t>(j)]
                            )
                        );
                    }
                    output = child_builder.release_store();
                }

                callback_protections.protect_with_index(
                    temporary, &temporary_index
                );
                if (simplify_value != NA_LOGICAL && !simplify_value) {
                    result = entry_protections.reprotect_one(
                        Rf_allocVector(VECSXP, vectorize_length),
                        result_index
                    );
                    for (R_len_t i = 0; i < vectorize_length; ++i) {
                        temporary = callback_protections.reprotect_slot(
                            io::finalize(std::move(stores[
                                static_cast<std::size_t>(i)
                            ])),
                            temporary_index
                        );
                        SET_VECTOR_ELT(result, i, temporary);
                    }
                }
                else {
                    for (R_len_t i = 0; i < vectorize_length; ++i) {
                        const R_len_t current_size = io::checked_r_len(
                            static_cast<R_xlen_t>(stores[
                                static_cast<std::size_t>(i)
                            ].size()),
                            "split results"
                        );
                        if (max_columns < current_size)
                            max_columns = current_size;
                    }

                    const R_xlen_t rows = vectorize_length;
                    const R_xlen_t columns = max_columns;
                    if (rows > 0 && columns > R_XLEN_T_MAX / rows) {
                        throw std::length_error(
                            "matrix length exceeds R's vector limit"
                        );
                    }

                    matrix_builder.reset(rows * columns);
                    for (R_xlen_t i = 0; i < rows; ++i) {
                        const io::OutputStore& current = stores[
                            static_cast<std::size_t>(i)
                        ];
                        const R_xlen_t current_size =
                            static_cast<R_xlen_t>(current.size());
                        R_xlen_t j = 0;
                        for (; j < current_size; ++j) {
                            matrix_builder.set(
                                i+j*rows, current.view(j)
                            );
                        }
                        for (; j < columns; ++j) {
                            if (simplify_value == NA_LOGICAL) {
                                matrix_builder.set_na(i+j*rows);
                            }
                            else {
                                matrix_builder.set(
                                    i+j*rows, "", 0,
                                    CETYPE_EXT_ASCII
                                );
                            }
                        }
                    }

                    result = entry_protections.reprotect_one(
                        matrix_builder.to_sexp(), result_index
                    );
                    temporary = callback_protections.reprotect_slot(
                        Rf_allocVector(INTSXP, 2), temporary_index
                    );
                    INTEGER(temporary)[0] = vectorize_length;
                    INTEGER(temporary)[1] = max_columns;
                    result = entry_protections.reprotect_one(
                        Rf_setAttrib(result, R_DimSymbol, temporary),
                        result_index
                    );
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        if (recycling_warning)
            emit_recycling_warning_r();
        if (root_fallback_warning)
            emit_fallback_warning_r();
    );
}

} } // namespace charr::altrep_backend
