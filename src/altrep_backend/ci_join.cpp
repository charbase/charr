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
#include "io/reader_utils.h"
#include "io/string_view.h"
#include "io/utf8_output.h"
#include "../shared/entrypoint.h"
#include "../shared/join.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

namespace charr { namespace altrep_backend {
using namespace std;


namespace join {


CHARR_CXX_HELPER void normalize_flatten_inputs(
    const charport::StrViews& input,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    vector<shared::StringView>& output
)
{
    output.resize(static_cast<size_t>(input.size()));
    for (R_xlen_t i = 0; i < input.size(); ++i) {
        const shared::StringView value = io::as_shared_view(input[i]);
        if (value.enc == shared::StringEncoding::bytes)
            throw StriException(MSG__BYTESENC);
        output[static_cast<size_t>(i)] = shared::normalize_utf8(
            value, converter, storage
        );
    }
}


CHARR_CXX_HELPER shared::StringView normalize_flatten_separator(
    const charport::StrView& input,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage
)
{
    const shared::StringView value = io::as_shared_view(input);
    if (value.enc == shared::StringEncoding::bytes)
        throw StriException(MSG__BYTESENC);
    return shared::normalize_utf8(value, converter, storage);
}


} // namespace join

using namespace join;


namespace join_frame {

CHARR_R_HELPER SEXP remove_empty_inputs_r(
    SEXP input, bool remove
) noexcept
{
    if (!remove)
        return input;

    const R_len_t size = LENGTH(input);
    R_len_t output_size = 0;
    for (R_len_t i = 0; i < size; ++i) {
        if (XLENGTH(VECTOR_ELT(input, i)) > 0)
            ++output_size;
    }

    SEXP output = PROTECT(Rf_allocVector(VECSXP, output_size));
    for (R_len_t i = 0, j = 0; i < size; ++i) {
        const SEXP value = VECTOR_ELT(input, i);
        if (XLENGTH(value) > 0)
            SET_VECTOR_ELT(output, j++, value);
    }
    UNPROTECT(1);
    return output;
}


CHARR_NEUTRAL_HELPER R_len_t recycling_length(
    const vector<R_len_t>& lengths,
    bool& warning
) noexcept
{
    warning = false;
    R_len_t output = 0;
    for (size_t i = 0; i < lengths.size(); ++i) {
        if (lengths[i] <= 0)
            return 0;
        if (lengths[i] > output)
            output = lengths[i];
    }
    for (size_t i = 0; i < lengths.size(); ++i) {
        if (output % lengths[i] != 0) {
            warning = true;
            break;
        }
    }
    return output;
}


CHARR_NEUTRAL_HELPER bool is_ascii(
    const char* data, int length
) noexcept
{
    for (int i = 0; i < length; ++i) {
        if (static_cast<unsigned char>(data[i]) > 0x7fU)
            return false;
    }
    return true;
}


CHARR_CXX_HELPER bool are_direct_inputs(
    const charport::StrViews& values
)
{
    const R_xlen_t size = values.size();
    for (R_xlen_t i = 0; i < size; ++i) {
        const charport::StrView value = values[i];
        if (value.is_na())
            continue;
        switch (value.enc) {
        case cetype_ext_t::CE_ASCII:
        case cetype_ext_t::CE_UTF8:
        case cetype_ext_t::CE_ASCII_OR_UTF8:
            break;
        case cetype_ext_t::CE_NATIVE:
        case cetype_ext_t::CE_LATIN1:
            return false;
        case cetype_ext_t::CE_BYTES:
            throw StriException(MSG__BYTESENC);
        case cetype_ext_t::CE_NA:
            break;
        default:
            throw StriException("unknown charport string encoding");
        }
    }
    return true;
}


CHARR_NEUTRAL_HELPER shared::StringView direct_input(
    const charport::StrView& value
) noexcept
{
    if (value.is_na()) {
        return shared::StringView{
            nullptr, shared::missing_string_length,
            shared::StringEncoding::missing
        };
    }

    const char* data = value.ptr;
    int length = value.len;
    if ((value.enc == cetype_ext_t::CE_UTF8 ||
            value.enc == cetype_ext_t::CE_ASCII_OR_UTF8) &&
            length >= 3 &&
            static_cast<unsigned char>(data[0]) == 0xefU &&
            static_cast<unsigned char>(data[1]) == 0xbbU &&
            static_cast<unsigned char>(data[2]) == 0xbfU) {
        data += 3;
        length -= 3;
    }

    const bool ascii = value.enc == cetype_ext_t::CE_ASCII ||
        (value.enc == cetype_ext_t::CE_ASCII_OR_UTF8 &&
            is_ascii(data, length));
    return shared::StringView{
        data, length,
        ascii
            ? shared::StringEncoding::ascii
            : shared::StringEncoding::utf8
    };
}


CHARR_CXX_HELPER shared::StringView normalize_input(
    const shared::StringView& input,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage
)
{
    if (input.enc == shared::StringEncoding::bytes)
        throw StriException(MSG__BYTESENC);
    return shared::normalize_utf8(input, converter, storage);
}


CHARR_NEUTRAL_HELPER void point_columns(
    const vector<vector<shared::StringView>>& inputs,
    vector<shared::join::Column>& columns
) noexcept
{
    for (size_t i = 0; i < inputs.size(); ++i) {
        columns[i] = shared::join::Column{
            inputs[i].data(), inputs[i].size()
        };
    }
}


CHARR_NEUTRAL_HELPER cetype_ext_t output_encoding(
    bool ascii
) noexcept
{
    return ascii ? cetype_ext_t::CE_ASCII : cetype_ext_t::CE_UTF8;
}

} // namespace join_frame
CHARR_ENTRYPOINT SEXP ci_join(
    SEXP strlist, SEXP sep, SEXP collapse, SEXP ignore_null
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool collapse_output = !Rf_isNull(collapse);
    const bool remove_empty = ci__prepare_arg_logical_1_notNA_r(
        ignore_null, "ignore_null"
    );
    strlist = entry_protections.protect_one(
        ci__prepare_arg_list_string_r(strlist, "...")
    );
    strlist = entry_protections.protect_one(
        join_frame::remove_empty_inputs_r(
                strlist, remove_empty
            )
    );
    const R_len_t column_count = LENGTH(strlist);

    try {
        vector<R_len_t> lengths(static_cast<size_t>(column_count), 0);
        vector<charport::Reader> row_readers;
        vector<charport::StrViews> row_views;
        vector<charport::Reader> collapsed_readers;
        vector<charport::StrViews> collapsed_views;
        charport::Reader single_reader;
        charport::StrViews single_views;
        charport::Reader row_separator_reader;
        charport::Reader collapsed_separator_reader;
        charport::Reader single_collapse_reader;
        charport::Reader collapsed_collapse_reader;
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        shared::NativeToUtf8 separator_converter;
        shared::SliceArena separator_storage;
        shared::NativeToUtf8 collapse_converter;
        shared::SliceArena collapse_storage;
        vector<vector<shared::StringView>> inputs(
            static_cast<size_t>(column_count)
        );
        vector<shared::join::Column> columns(
            static_cast<size_t>(column_count)
        );
        vector<shared::join::FlattenPlan> row_plans;
        io::OutputBuilder builder(0);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                do {
                if (column_count <= 0) {
                    builder.reset(collapse_output ? 1 : 0);
                    if (collapse_output) {
                        builder.set(
                            0, "", 0, cetype_ext_t::CE_ASCII
                        );
                    }
                    result = entry_protections.reprotect_one(
                        builder.to_sexp(), result_index
                    );
                    break;
                }

                if (!collapse_output) {
                    for (R_len_t column = 0;
                            column < column_count; ++column) {
                        lengths[static_cast<size_t>(column)] =
                            io::checked_r_len(
                                XLENGTH(VECTOR_ELT(strlist, column)),
                                "character vectors"
                            );
                    }
                    bool recycling_warning = false;
                    const R_len_t row_count =
                        join_frame::recycling_length(
                            lengths, recycling_warning
                        );
                    if (row_count <= 0) {
                        builder.reset(0);
                        result = entry_protections.reprotect_one(
                            builder.to_sexp(), result_index
                        );
                        break;
                    }

                    sep = callback_protections.protect_one(
                        ci__prepare_arg_string_1_r(sep, "sep")
                    );
                    row_separator_reader.reset(sep);
                    if (row_separator_reader.size() != 1) {
                        throw std::runtime_error(
                            "separator length changed during string joining"
                        );
                    }
                    const charport::StrView separator_raw =
                        row_separator_reader.view(0);
                    if (separator_raw.is_na()) {
                        builder.reset(row_count);
                        for (R_len_t row = 0; row < row_count; ++row)
                            builder.set_na(row);
                        result = entry_protections.reprotect_one(
                            builder.to_sexp(), result_index
                        );
                        break;
                    }

                    const shared::StringView separator =
                        join_frame::normalize_input(
                            io::as_shared_view(separator_raw),
                            separator_converter, separator_storage
                        );
                    if (recycling_warning)
                        Rf_warning(MSG__WARN_RECYCLING_RULE);

                    row_readers.resize(
                        static_cast<size_t>(column_count)
                    );
                    row_views.resize(
                        static_cast<size_t>(column_count)
                    );
                    for (R_len_t column = 0;
                            column < column_count; ++column) {
                        const size_t index = static_cast<size_t>(column);
                        row_readers[index].reset(
                            VECTOR_ELT(strlist, column)
                        );
                        if (row_readers[index].size() != lengths[index]) {
                            throw std::runtime_error(
                                "Reader length changed during string joining"
                            );
                        }
                        row_views[index].resize(lengths[index]);
                        row_readers[index].views(
                            0, lengths[index],
                            row_views[index].ptrs(),
                            row_views[index].lengths(),
                            row_views[index].encodings()
                        );
                    }

                    const bool direct_pair = separator.len == 0 &&
                        column_count == 2 &&
                        join_frame::are_direct_inputs(row_views[0]) &&
                        join_frame::are_direct_inputs(row_views[1]);
                    if (direct_pair) {
                        const R_len_t first_length = lengths[0];
                        const R_len_t second_length = lengths[1];
                        const bool first_scalar = first_length == 1;
                        const bool second_scalar = second_length == 1;
                        const bool first_aligned =
                            first_length == row_count;
                        const bool second_aligned =
                            second_length == row_count;
                        const shared::StringView first_scalar_value =
                            first_scalar
                                ? join_frame::direct_input(row_views[0][0])
                                : shared::StringView{
                                    nullptr, 0,
                                    shared::StringEncoding::missing
                                };
                        const shared::StringView second_scalar_value =
                            second_scalar
                                ? join_frame::direct_input(row_views[1][0])
                                : shared::StringView{
                                    nullptr, 0,
                                    shared::StringEncoding::missing
                                };

                        builder.reset(row_count);
                        for (R_len_t row = 0; row < row_count; ++row) {
                            const shared::StringView first_value =
                                first_scalar
                                    ? first_scalar_value
                                    : join_frame::direct_input(
                                        row_views[0][
                                            first_aligned
                                                ? row
                                                : row % first_length
                                        ]
                                    );
                            const shared::StringView second_value =
                                second_scalar
                                    ? second_scalar_value
                                    : join_frame::direct_input(
                                        row_views[1][
                                            second_aligned
                                                ? row
                                                : row % second_length
                                        ]
                                    );
                            if (first_value.is_na() ||
                                    second_value.is_na()) {
                                builder.set_na(row);
                                continue;
                            }

                            const size_t first_bytes =
                                static_cast<size_t>(first_value.len);
                            const size_t second_bytes =
                                static_cast<size_t>(second_value.len);
                            if (first_bytes >
                                    static_cast<size_t>(POW_2_31_M_1) -
                                        second_bytes) {
                                throw StriException(
                                    MSG__CHARSXP_2147483647
                                );
                            }
                            const size_t total =
                                first_bytes + second_bytes;
                            char* destination = builder.reserve(
                                row, total,
                                join_frame::output_encoding(
                                    first_value.enc ==
                                        shared::StringEncoding::ascii &&
                                    second_value.enc ==
                                        shared::StringEncoding::ascii
                                )
                            );
                            if (first_bytes > 0) {
                                memcpy(
                                    destination,
                                    first_value.ptr, first_bytes
                                );
                            }
                            if (second_bytes > 0) {
                                memcpy(
                                    destination + first_bytes,
                                    second_value.ptr, second_bytes
                                );
                            }
                        }
                        result = entry_protections.reprotect_one(
                            builder.to_sexp(), result_index
                        );
                        break;
                    }

                    for (R_len_t column = 0;
                            column < column_count; ++column) {
                        const size_t index = static_cast<size_t>(column);
                        vector<shared::StringView>& current = inputs[index];
                        current.resize(static_cast<size_t>(lengths[index]));
                        for (R_len_t i = 0; i < lengths[index]; ++i) {
                            current[static_cast<size_t>(i)] =
                                join_frame::normalize_input(
                                    io::as_shared_view(
                                        row_views[index][i]
                                    ),
                                    converter, storage
                                );
                        }
                    }
                    join_frame::point_columns(inputs, columns);

                    row_plans.resize(static_cast<size_t>(row_count));
                    for (R_len_t row = 0; row < row_count; ++row) {
                        shared::join::FlattenPlan& plan =
                            row_plans[static_cast<size_t>(row)];
                        shared::join::plan_join_row(
                            columns.data(), columns.size(),
                            static_cast<size_t>(row),
                            static_cast<size_t>(row_count),
                            separator, plan
                        );
                        if (!plan.has_na && plan.too_large) {
                            throw StriException(
                                MSG__CHARSXP_2147483647
                            );
                        }
                    }

                    builder.reset(row_count);
                    for (R_len_t row = 0; row < row_count; ++row) {
                        const shared::join::FlattenPlan& plan =
                            row_plans[static_cast<size_t>(row)];
                        if (plan.has_na) {
                            builder.set_na(row);
                            continue;
                        }
                        char* destination = builder.reserve(
                            row, plan.bytes,
                            join_frame::output_encoding(plan.ascii)
                        );
                        shared::join::write_join_row(
                            columns.data(), columns.size(),
                            static_cast<size_t>(row),
                            static_cast<size_t>(row_count),
                            separator, destination
                        );
                    }
                    result = entry_protections.reprotect_one(
                        builder.to_sexp(), result_index
                    );
                    break;
                }

                shared::StringView separator{
                    "", 0, shared::StringEncoding::ascii
                };
                shared::StringView collapse_value{
                    "", 0, shared::StringEncoding::ascii
                };
                R_len_t row_count = 0;

                if (column_count == 1) {
                    collapse = callback_protections.protect_one(
                        ci__prepare_arg_string_1_r(
                            collapse, "collapse"
                        )
                    );
                    single_collapse_reader.reset(collapse);
                    if (single_collapse_reader.size() != 1) {
                        throw std::runtime_error(
                            "collapse length changed during string joining"
                        );
                    }
                    const charport::StrView collapse_raw =
                        single_collapse_reader.view(0);
                    if (collapse_raw.is_na()) {
                        builder.reset(1);
                        builder.set_na(0);
                        result = entry_protections.reprotect_one(
                            builder.to_sexp(), result_index
                        );
                        break;
                    }

                    lengths[0] = io::checked_r_len(
                        XLENGTH(VECTOR_ELT(strlist, 0)),
                        "character vectors"
                    );
                    row_count = lengths[0];
                    if (row_count <= 0) {
                        builder.reset(1);
                        builder.set(
                            0, "", 0, cetype_ext_t::CE_ASCII
                        );
                        result = entry_protections.reprotect_one(
                            builder.to_sexp(), result_index
                        );
                        break;
                    }

                    single_reader.reset(VECTOR_ELT(strlist, 0));
                    if (single_reader.size() != row_count) {
                        throw std::runtime_error(
                            "Reader length changed during string joining"
                        );
                    }
                    single_views.resize(row_count);
                    single_reader.views(
                        0, row_count,
                        single_views.ptrs(), single_views.lengths(),
                        single_views.encodings()
                    );
                    inputs[0].resize(static_cast<size_t>(row_count));
                    for (R_len_t i = 0; i < row_count; ++i) {
                        inputs[0][static_cast<size_t>(i)] =
                            join_frame::normalize_input(
                                io::as_shared_view(single_views[i]),
                                converter, storage
                            );
                    }
                    collapse_value = join_frame::normalize_input(
                        io::as_shared_view(collapse_raw),
                        collapse_converter, collapse_storage
                    );
                }
                else {
                    sep = callback_protections.protect_one(
                        ci__prepare_arg_string_1_r(sep, "sep")
                    );
                    collapse = callback_protections.protect_one(
                        ci__prepare_arg_string_1_r(
                            collapse, "collapse"
                        )
                    );

                    collapsed_separator_reader.reset(sep);
                    if (collapsed_separator_reader.size() != 1) {
                        throw std::runtime_error(
                            "separator length changed during string joining"
                        );
                    }
                    const charport::StrView separator_raw =
                        collapsed_separator_reader.view(0);
                    if (separator_raw.is_na()) {
                        builder.reset(1);
                        builder.set_na(0);
                        result = entry_protections.reprotect_one(
                            builder.to_sexp(), result_index
                        );
                        break;
                    }

                    collapsed_collapse_reader.reset(collapse);
                    if (collapsed_collapse_reader.size() != 1) {
                        throw std::runtime_error(
                            "collapse length changed during string joining"
                        );
                    }
                    const charport::StrView collapse_raw =
                        collapsed_collapse_reader.view(0);
                    if (collapse_raw.is_na()) {
                        builder.reset(1);
                        builder.set_na(0);
                        result = entry_protections.reprotect_one(
                            builder.to_sexp(), result_index
                        );
                        break;
                    }

                    for (R_len_t column = 0;
                            column < column_count; ++column) {
                        lengths[static_cast<size_t>(column)] =
                            io::checked_r_len(
                                XLENGTH(VECTOR_ELT(strlist, column)),
                                "character vectors"
                            );
                    }
                    bool recycling_warning = false;
                    row_count = join_frame::recycling_length(
                        lengths, recycling_warning
                    );
                    if (row_count <= 0) {
                        builder.reset(1);
                        builder.set(
                            0, "", 0, cetype_ext_t::CE_ASCII
                        );
                        result = entry_protections.reprotect_one(
                            builder.to_sexp(), result_index
                        );
                        break;
                    }
                    if (recycling_warning)
                        Rf_warning(MSG__WARN_RECYCLING_RULE);

                    collapsed_readers.resize(
                        static_cast<size_t>(column_count)
                    );
                    collapsed_views.resize(
                        static_cast<size_t>(column_count)
                    );
                    for (R_len_t column = 0;
                            column < column_count; ++column) {
                        const size_t index = static_cast<size_t>(column);
                        collapsed_readers[index].reset(
                            VECTOR_ELT(strlist, column)
                        );
                        if (collapsed_readers[index].size() != lengths[index]) {
                            throw std::runtime_error(
                                "Reader length changed during string joining"
                            );
                        }
                        collapsed_views[index].resize(lengths[index]);
                        collapsed_readers[index].views(
                            0, lengths[index],
                            collapsed_views[index].ptrs(),
                            collapsed_views[index].lengths(),
                            collapsed_views[index].encodings()
                        );
                        inputs[index].resize(
                            static_cast<size_t>(lengths[index])
                        );
                        for (R_len_t i = 0; i < lengths[index]; ++i) {
                            inputs[index][static_cast<size_t>(i)] =
                                join_frame::normalize_input(
                                    io::as_shared_view(
                                        collapsed_views[index][i]
                                    ),
                                    converter, storage
                                );
                        }
                    }
                    separator = join_frame::normalize_input(
                        io::as_shared_view(separator_raw),
                        separator_converter, separator_storage
                    );
                    collapse_value = join_frame::normalize_input(
                        io::as_shared_view(collapse_raw),
                        collapse_converter, collapse_storage
                    );
                }

                join_frame::point_columns(inputs, columns);
                shared::join::FlattenPlan plan{
                    0, 0, false, false, true
                };
                shared::join::plan_join_all(
                    columns.data(), columns.size(),
                    static_cast<size_t>(row_count),
                    separator, collapse_value, plan
                );

                builder.reset(1);
                if (plan.has_na) {
                    builder.set_na(0);
                }
                else {
                    if (plan.too_large)
                        throw StriException(MSG__CHARSXP_2147483647);
                    char* destination = builder.reserve(
                        0, plan.bytes,
                        join_frame::output_encoding(plan.ascii)
                    );
                    shared::join::write_join_all(
                        columns.data(), columns.size(),
                        static_cast<size_t>(row_count),
                        separator, collapse_value, destination
                    );
                }
                result = entry_protections.reprotect_one(
                    builder.to_sexp(), result_index
                );

                } while (false);
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}


/** String vector flatten, with no separator (i.e., empty) between each string
 *
 *  if any of s is NA, the result will be NA_character_
 *
 *  @param s character vector
 *  @return if s is not empty, then a character vector of length 1
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException friendly
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-18)
 *          This function hasn't been used at all before (strange, isn't it?);
 *          From now on it's being called by ci_flatten_withressep
 *          (a small performance gain)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.2.1 (Marek Gagolewski, 2018-04-20)
 *    na_empty arg added
 *
 * @version 1.6.2 (Marek Gagolewski, 2021-05-10)
 *    #428 na_empty=NA support
 */
/** String vector flatten, with separator between each string
 *
 *  if any of str is NA, the result will be NA_character_
 *
 *  @param str character vector
 *  @param collapse a single string
 *  @return if s is not empty, then a character vector of length 1
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Bartek Tartanus)
 *          collapse arg added (1 sep supported)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException friendly
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-18)
 *          Call ci_flatten_noressep if needed
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.2.1 (Marek Gagolewski, 2018-04-20)
 *    na_empty, omit_empty arg added
 *
 * @version 1.6.2 (Marek Gagolewski, 2021-05-10)
 *    #428 na_empty=NA support
 *
 */
CHARR_ENTRYPOINT SEXP ci_flatten(
    SEXP str, SEXP collapse, SEXP na_empty, SEXP omit_empty
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    collapse = entry_protections.protect_one(
        ci__prepare_arg_string_1_r(
            collapse, "collapse"
        )
    );
    const int na_empty_value = ci__prepare_arg_logical_1_NA_r(
        na_empty, "na_empty"
    );
    const bool omit_empty_value = ci__prepare_arg_logical_1_notNA_r(
        omit_empty, "omit_empty"
    );


    R_len_t str_length = 0;

    try {
        charport::Reader separator_reader;
        charport::Reader value_reader;
        charport::StrViews raw_values;
        shared::NativeToUtf8 value_converter;
        shared::NativeToUtf8 separator_converter;
        shared::SliceArena value_storage;
        shared::SliceArena separator_storage;
        vector<shared::StringView> values;
        shared::join::FlattenPlan plan{0, 0, false, false, true};
        io::OutputBuilder output(1);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                separator_reader.reset(collapse);
                if (separator_reader.size() != 1) {
                    throw std::runtime_error(
                        "Reader length changed during flatten"
                    );
                }
                const charport::StrView raw_separator =
                    separator_reader.view(0);
                const bool missing_separator = raw_separator.is_na();
                const bool empty_separator =
                    !missing_separator && raw_separator.len == 0;

                if (!missing_separator) {
                    str = callback_protections.protect_one(
                        ci__prepare_arg_string_r(str, "str")
                    );
                    str_length = LENGTH(str);
                }

                if (missing_separator) {
                    output.set_na(0);
                }
                else if (str_length <= 0) {
                    (void)output.reserve(
                        0, 0, cetype_ext_t::CE_ASCII
                    );
                }
                else {
                    value_reader.reset(str);
                    if (value_reader.size() != str_length) {
                        throw std::runtime_error(
                            "Reader length changed during flatten"
                        );
                    }
                    raw_values.resize(str_length);
                    value_reader.views(
                        0, str_length,
                        raw_values.ptrs(), raw_values.lengths(),
                        raw_values.encodings()
                    );
                    normalize_flatten_inputs(
                        raw_values, value_converter, value_storage, values
                    );

                    shared::StringView separator{
                        nullptr, 0, shared::StringEncoding::ascii
                    };
                    const shared::StringView* separator_ptr = nullptr;
                    if (!empty_separator) {
                        separator = normalize_flatten_separator(
                            raw_separator, separator_converter,
                            separator_storage
                        );
                        separator_ptr = &separator;
                    }

                    shared::join::plan_flatten(
                        values.data(), values.size(), separator_ptr,
                        na_empty_value, omit_empty_value, plan
                    );
                    if (plan.too_large)
                        throw StriException(MSG__CHARSXP_2147483647);
                    if (plan.has_na) {
                        output.set_na(0);
                    }
                    else {
                        char* destination = output.reserve(
                            0, plan.bytes,
                            plan.ascii
                                ? cetype_ext_t::CE_ASCII
                                : cetype_ext_t::CE_UTF8
                        );
                        shared::join::write_flatten(
                            values.data(), values.size(), separator_ptr,
                            na_empty_value, omit_empty_value, destination
                        );
                    }
                }

                result = entry_protections.reprotect_one(
                    output.to_sexp(), result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}

} } // namespace charr::altrep_backend
