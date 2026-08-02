
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
#include "io/string_view.h"
#include "../shared/entrypoint.h"
#include "../shared/join.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"
#include <algorithm>
#include <exception>
#include <string>
#include <vector>
namespace charr { namespace base_backend {

using namespace std;


namespace join {


CHARR_CXX_HELPER CHARR_ALWAYS_INLINE void ci__flatten_append(
    string& output, const char* data, size_t length, bool& too_large
)
{
    if (too_large || length == 0)
        return;
    if (length > static_cast<size_t>(POW_2_31_M_1)-output.size()) {
        too_large = true;
        return;
    }
    try {
        output.append(data, length);
    }
    catch (const std::exception& error) {
        throw StriException("%s", error.what());
    }
}


CHARR_R_HELPER size_t ci__flatten_reserve_hint_r(
    const SEXP* values, R_len_t size,
    size_t separator_length
) noexcept
{
    // Capacity is only a hint: sample enough records to avoid ordinary
    // reallocations, but do not reintroduce a vector-wide sizing pass or a
    // large speculative allocation.
    const size_t reserve_limit =
        static_cast<size_t>(64)*1024U*1024U;
    const R_len_t sample_size = std::min<R_len_t>(size, 1024);
    size_t sample_bytes = 0;
    for (R_len_t i=0; i<sample_size; ++i) {
        if (values[i] == NA_STRING)
            continue;
        const size_t length = static_cast<size_t>(LENGTH(values[i]));
        if (length > reserve_limit-sample_bytes) {
            sample_bytes = reserve_limit;
            break;
        }
        sample_bytes += length;
    }

    const double mean_bytes = sample_size > 0
        ? static_cast<double>(sample_bytes) /
            static_cast<double>(sample_size)
        : 0.0;
    const double estimated = 1.125 * (
        mean_bytes*static_cast<double>(size) +
        separator_length*static_cast<double>(size > 0 ? size-1 : 0)
    );
    const size_t reserve_size = estimated >= static_cast<double>(reserve_limit)
        ? reserve_limit
        : static_cast<size_t>(estimated);
    return reserve_size;
}


CHARR_CXX_HELPER void ci__flatten_reserve(
    string& output, size_t reserve_size
)
{
    try {
        output.reserve(reserve_size);
    }
    catch (const std::exception& error) {
        throw StriException("%s", error.what());
    }
}


CHARR_CXX_HELPER CHARR_ALWAYS_INLINE void append_flatten_value(
    const shared::StringView& source,
    const shared::StringView& separator,
    shared::NativeToUtf8& converter,
    int na_empty,
    bool omit_empty,
    string& output,
    bool& started,
    bool& has_na,
    bool& too_large
)
{
    if (source.enc == shared::StringEncoding::bytes)
        throw StriException(MSG__BYTESENC);

    const shared::StringView value =
        shared::normalize_utf8_transient(source, converter);

    if (value.is_na() && na_empty != NA_LOGICAL && !na_empty) {
        has_na = true;
        return;
    }
    if (value.is_na() && na_empty == NA_LOGICAL)
        return;
    if (omit_empty && (value.is_na() || value.len == 0))
        return;

    if (!has_na && started) {
        ci__flatten_append(
            output, separator.ptr,
            static_cast<size_t>(separator.len), too_large
        );
    }
    else if (!has_na) {
        started = true;
    }
    if (!has_na && !value.is_na()) {
        ci__flatten_append(
            output, value.ptr,
            static_cast<size_t>(value.len), too_large
        );
    }
}


CHARR_CXX_HELPER void normalize_flatten_inputs(
    const vector<shared::StringView>& input,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    vector<shared::StringView>& output
)
{
    output.resize(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i].enc == shared::StringEncoding::bytes)
            throw StriException(MSG__BYTESENC);
        output[i] = shared::normalize_utf8(
            input[i], converter, storage
        );
    }
}


CHARR_CXX_HELPER shared::StringView normalize_flatten_separator(
    const shared::StringView& input,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage
)
{
    if (input.enc == shared::StringEncoding::bytes)
        throw StriException(MSG__BYTESENC);
    return shared::normalize_utf8(input, converter, storage);
}


CHARR_CXX_HELPER void build_flatten_bytes(
    const vector<shared::StringView>& values,
    const shared::StringView* separator,
    int na_empty,
    bool omit_empty,
    shared::join::FlattenPlan& plan,
    vector<char>& output
)
{
    shared::join::plan_flatten(
        values.data(), values.size(), separator,
        na_empty, omit_empty, plan
    );
    if (plan.too_large)
        throw StriException(MSG__CHARSXP_2147483647);
    if (plan.has_na)
        return;

    output.resize(plan.bytes);
    shared::join::write_flatten(
        values.data(), values.size(), separator,
        na_empty, omit_empty,
        output.empty() ? nullptr : output.data()
    );
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
        if (LENGTH(VECTOR_ELT(input, i)) > 0)
            ++output_size;
    }

    SEXP output = PROTECT(Rf_allocVector(VECSXP, output_size));
    for (R_len_t i = 0, j = 0; i < size; ++i) {
        const SEXP value = VECTOR_ELT(input, i);
        if (LENGTH(value) > 0)
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


CHARR_R_HELPER bool is_direct_source_r(SEXP source) noexcept
{
    if (ALTREP(source))
        return false;
    const SEXP* values = STRING_PTR_RO(source);
    const R_xlen_t size = XLENGTH(source);
    for (R_xlen_t i = 0; i < size; ++i) {
        const SEXP value = values[i];
        if (value != NA_STRING && !IS_ASCII(value) && !IS_UTF8(value))
            return false;
    }
    return true;
}


CHARR_R_HELPER shared::StringView direct_input_r(SEXP value) noexcept
{
    if (value == NA_STRING) {
        return shared::StringView{
            nullptr, shared::missing_string_length,
            shared::StringEncoding::missing
        };
    }

    const char* data = CHAR(value);
    int length = LENGTH(value);
    shared::StringEncoding encoding = IS_ASCII(value)
        ? shared::StringEncoding::ascii
        : shared::StringEncoding::utf8;
    if (encoding == shared::StringEncoding::utf8 && length >= 3 &&
            static_cast<unsigned char>(data[0]) == 0xefU &&
            static_cast<unsigned char>(data[1]) == 0xbbU &&
            static_cast<unsigned char>(data[2]) == 0xbfU) {
        data += 3;
        length -= 3;
    }
    return shared::StringView{data, length, encoding};
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
    strlist = entry_protections.protect_one(ci__prepare_arg_list_string_r(strlist, "..."));
    strlist = entry_protections.protect_one(join_frame::remove_empty_inputs_r(
        strlist, remove_empty
    ));
    const R_len_t column_count = LENGTH(strlist);

    try {
        vector<R_len_t> lengths(static_cast<size_t>(column_count), 0);
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
        vector<char> output_buffer;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                do {
                if (column_count <= 0) {
                    result = entry_protections.reprotect_one(
                        Rf_allocVector(
                            STRSXP, collapse_output ? 1 : 0
                        ),
                        result_index
                    );
                    if (collapse_output)
                        SET_STRING_ELT(result, 0, R_BlankString);
                    break;
                }

                if (!collapse_output) {
                    for (R_len_t column = 0;
                            column < column_count; ++column) {
                        lengths[static_cast<size_t>(column)] = LENGTH(
                            VECTOR_ELT(strlist, column)
                        );
                    }
                    bool recycling_warning = false;
                    const R_len_t row_count =
                        join_frame::recycling_length(
                            lengths, recycling_warning
                        );
                    if (row_count <= 0) {
                        result = entry_protections.reprotect_one(
                            Rf_allocVector(STRSXP, 0), result_index
                        );
                        break;
                    }

                    sep = callback_protections.protect_one(
                        ci__prepare_arg_string_1_r(sep, "sep")
                    );
                    const SEXP separator_sexp = STRING_ELT(sep, 0);
                    if (separator_sexp == NA_STRING) {
                        result = entry_protections.reprotect_one(
                            Rf_allocVector(STRSXP, row_count), result_index
                        );
                        for (R_len_t row = 0; row < row_count; ++row)
                            SET_STRING_ELT(result, row, NA_STRING);
                        break;
                    }

                    const shared::StringView separator =
                        join_frame::normalize_input(
                            io::as_shared_view(separator_sexp),
                            separator_converter, separator_storage
                        );
                    if (recycling_warning)
                        Rf_warning(MSG__WARN_RECYCLING_RULE);

                    const bool direct_pair = separator.len == 0 &&
                        column_count == 2 &&
                        join_frame::is_direct_source_r(
                            VECTOR_ELT(strlist, 0)
                        ) &&
                        join_frame::is_direct_source_r(
                            VECTOR_ELT(strlist, 1)
                        );
                    if (direct_pair) {
                        const SEXP first_source = VECTOR_ELT(strlist, 0);
                        const SEXP second_source = VECTOR_ELT(strlist, 1);
                        const SEXP* first = STRING_PTR_RO(first_source);
                        const SEXP* second = STRING_PTR_RO(second_source);
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
                                ? join_frame::direct_input_r(first[0])
                                : shared::StringView{
                                    nullptr, 0,
                                    shared::StringEncoding::missing
                                };
                        const shared::StringView second_scalar_value =
                            second_scalar
                                ? join_frame::direct_input_r(second[0])
                                : shared::StringView{
                                    nullptr, 0,
                                    shared::StringEncoding::missing
                                };

                        result = entry_protections.reprotect_one(
                            Rf_allocVector(STRSXP, row_count), result_index
                        );
                        for (R_len_t row = 0; row < row_count; ++row) {
                            const shared::StringView first_value =
                                first_scalar
                                    ? first_scalar_value
                                    : join_frame::direct_input_r(
                                        first[
                                            first_aligned
                                                ? row
                                                : row % first_length
                                        ]
                                    );
                            const shared::StringView second_value =
                                second_scalar
                                    ? second_scalar_value
                                    : join_frame::direct_input_r(
                                        second[
                                            second_aligned
                                                ? row
                                                : row % second_length
                                        ]
                                    );
                            if (first_value.is_na() ||
                                    second_value.is_na()) {
                                SET_STRING_ELT(result, row, NA_STRING);
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
                            if (total == 0) {
                                SET_STRING_ELT(
                                    result, row, R_BlankString
                                );
                                continue;
                            }
                            if (output_buffer.size() < total)
                                output_buffer.resize(total);
                            if (first_bytes > 0) {
                                memcpy(
                                    output_buffer.data(),
                                    first_value.ptr, first_bytes
                                );
                            }
                            if (second_bytes > 0) {
                                memcpy(
                                    output_buffer.data() + first_bytes,
                                    second_value.ptr, second_bytes
                                );
                            }
                            SET_STRING_ELT(
                                result, row,
                                Rf_mkCharLenCE(
                                    output_buffer.data(),
                                    static_cast<int>(total), CE_UTF8
                                )
                            );
                        }
                        break;
                    }

                    for (R_len_t column = 0;
                            column < column_count; ++column) {
                        const SEXP source = VECTOR_ELT(strlist, column);
                        const SEXP* values = STRING_PTR_RO(source);
                        vector<shared::StringView>& current =
                            inputs[static_cast<size_t>(column)];
                        current.resize(static_cast<size_t>(
                            lengths[static_cast<size_t>(column)]
                        ));
                        for (R_len_t i = 0;
                                i < lengths[static_cast<size_t>(column)];
                                ++i) {
                            current[static_cast<size_t>(i)] =
                                join_frame::normalize_input(
                                    io::as_shared_view(values[i]),
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

                    result = entry_protections.reprotect_one(
                        Rf_allocVector(STRSXP, row_count), result_index
                    );
                    for (R_len_t row = 0; row < row_count; ++row) {
                        const shared::join::FlattenPlan& plan =
                            row_plans[static_cast<size_t>(row)];
                        if (plan.has_na) {
                            SET_STRING_ELT(result, row, NA_STRING);
                            continue;
                        }
                        if (output_buffer.size() < plan.bytes)
                            output_buffer.resize(plan.bytes);
                        shared::join::write_join_row(
                            columns.data(), columns.size(),
                            static_cast<size_t>(row),
                            static_cast<size_t>(row_count),
                            separator,
                            plan.bytes == 0
                                ? nullptr
                                : output_buffer.data()
                        );
                        SET_STRING_ELT(
                            result, row,
                            plan.bytes == 0
                                ? R_BlankString
                                : Rf_mkCharLenCE(
                                    output_buffer.data(),
                                    static_cast<int>(plan.bytes), CE_UTF8
                                )
                        );
                    }
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
                    const SEXP collapse_sexp = STRING_ELT(collapse, 0);
                    if (collapse_sexp == NA_STRING) {
                        result = entry_protections.reprotect_one(
                            Rf_allocVector(STRSXP, 1), result_index
                        );
                        SET_STRING_ELT(result, 0, NA_STRING);
                        break;
                    }

                    const SEXP source = VECTOR_ELT(strlist, 0);
                    lengths[0] = LENGTH(source);
                    row_count = lengths[0];
                    if (row_count <= 0) {
                        result = entry_protections.reprotect_one(
                            Rf_allocVector(STRSXP, 1), result_index
                        );
                        SET_STRING_ELT(result, 0, R_BlankString);
                        break;
                    }

                    const SEXP* values = STRING_PTR_RO(source);
                    inputs[0].resize(static_cast<size_t>(lengths[0]));
                    for (R_len_t i = 0; i < lengths[0]; ++i) {
                        inputs[0][static_cast<size_t>(i)] =
                            join_frame::normalize_input(
                                io::as_shared_view(values[i]),
                                converter, storage
                            );
                    }
                    collapse_value = join_frame::normalize_input(
                        io::as_shared_view(collapse_sexp),
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
                    const SEXP separator_sexp = STRING_ELT(sep, 0);
                    if (separator_sexp == NA_STRING) {
                        result = entry_protections.reprotect_one(
                            Rf_allocVector(STRSXP, 1), result_index
                        );
                        SET_STRING_ELT(result, 0, NA_STRING);
                        break;
                    }
                    const SEXP collapse_sexp = STRING_ELT(collapse, 0);
                    if (collapse_sexp == NA_STRING) {
                        result = entry_protections.reprotect_one(
                            Rf_allocVector(STRSXP, 1), result_index
                        );
                        SET_STRING_ELT(result, 0, NA_STRING);
                        break;
                    }

                    for (R_len_t column = 0;
                            column < column_count; ++column) {
                        lengths[static_cast<size_t>(column)] = LENGTH(
                            VECTOR_ELT(strlist, column)
                        );
                    }
                    bool recycling_warning = false;
                    row_count = join_frame::recycling_length(
                        lengths, recycling_warning
                    );
                    if (row_count <= 0) {
                        result = entry_protections.reprotect_one(
                            Rf_allocVector(STRSXP, 1), result_index
                        );
                        SET_STRING_ELT(result, 0, R_BlankString);
                        break;
                    }
                    if (recycling_warning)
                        Rf_warning(MSG__WARN_RECYCLING_RULE);

                    for (R_len_t column = 0;
                            column < column_count; ++column) {
                        const SEXP source = VECTOR_ELT(strlist, column);
                        const SEXP* values = STRING_PTR_RO(source);
                        vector<shared::StringView>& current =
                            inputs[static_cast<size_t>(column)];
                        current.resize(static_cast<size_t>(
                            lengths[static_cast<size_t>(column)]
                        ));
                        for (R_len_t i = 0;
                                i < lengths[static_cast<size_t>(column)];
                                ++i) {
                            current[static_cast<size_t>(i)] =
                                join_frame::normalize_input(
                                    io::as_shared_view(values[i]),
                                    converter, storage
                                );
                        }
                    }
                    separator = join_frame::normalize_input(
                        io::as_shared_view(separator_sexp),
                        separator_converter, separator_storage
                    );
                    collapse_value = join_frame::normalize_input(
                        io::as_shared_view(collapse_sexp),
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

                result = entry_protections.reprotect_one(
                    Rf_allocVector(STRSXP, 1), result_index
                );
                if (plan.has_na) {
                    SET_STRING_ELT(result, 0, NA_STRING);
                }
                else {
                    if (plan.too_large)
                        throw StriException(MSG__CHARSXP_2147483647);
                    output_buffer.resize(plan.bytes);
                    shared::join::write_join_all(
                        columns.data(), columns.size(),
                        static_cast<size_t>(row_count),
                        separator, collapse_value,
                        plan.bytes == 0 ? nullptr : output_buffer.data()
                    );
                    SET_STRING_ELT(
                        result, 0,
                        plan.bytes == 0
                            ? R_BlankString
                            : Rf_mkCharLenCE(
                                output_buffer.data(),
                                static_cast<int>(plan.bytes), CE_UTF8
                            )
                    );
                }

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
 * @version 0.1-?? (Marek Gagolewski)
 *          UTF-8 input - any R encoding
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
 * @version 0.1-?? (Marek Gagolewski)
 *          UTF-8 input - any R encoding
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

    collapse = entry_protections.protect_one(ci__prepare_arg_string_1_r(
        collapse, "collapse"
    ));
    const int na_empty_value = ci__prepare_arg_logical_1_NA_r(
        na_empty, "na_empty"
    );
    const bool omit_empty_value = ci__prepare_arg_logical_1_notNA_r(
        omit_empty, "omit_empty"
    );

    R_len_t str_length = 0;

    try {
        shared::NativeToUtf8 value_converter;
        shared::NativeToUtf8 separator_converter;
        shared::SliceArena value_storage;
        shared::SliceArena separator_storage;
        vector<shared::StringView> raw_values;
        vector<shared::StringView> values;
        vector<char> output;
        string streaming_output;
        shared::join::FlattenPlan plan{0, 0, false, false, true};

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const SEXP separator_sexp = STRING_ELT(collapse, 0);
                const bool missing_separator =
                    separator_sexp == NA_STRING;
                const bool empty_separator =
                    !missing_separator && LENGTH(separator_sexp) == 0;

                if (!missing_separator) {
                    str = callback_protections.protect_one(
                        ci__prepare_arg_string_r(str, "str")
                    );
                    str_length = LENGTH(str);
                }

                result = entry_protections.reprotect_one(
                    Rf_allocVector(STRSXP, 1), result_index
                );
                if (missing_separator) {
                    SET_STRING_ELT(result, 0, NA_STRING);
                }
                else if (str_length <= 0) {
                    SET_STRING_ELT(result, 0, R_BlankString);
                }
                else if (IS_ASCII(separator_sexp) ||
                        IS_UTF8(separator_sexp)) {
                    const SEXP* source = STRING_PTR_RO(str);
                    const shared::StringView separator =
                        normalize_flatten_separator(
                            io::as_shared_view(separator_sexp),
                            separator_converter, separator_storage
                        );
                    ci__flatten_reserve(
                        streaming_output,
                        ci__flatten_reserve_hint_r(
                            source, str_length,
                            static_cast<size_t>(separator.len)
                        )
                    );

                    bool started = false;
                    bool has_na = false;
                    bool too_large = false;
                    for (R_len_t i = 0; i < str_length; ++i) {
                        const shared::StringView current =
                            io::as_shared_view(source[i]);
                        append_flatten_value(
                            current, separator, value_converter,
                            na_empty_value, omit_empty_value,
                            streaming_output, started, has_na, too_large
                        );
                    }

                    if (has_na) {
                        SET_STRING_ELT(result, 0, NA_STRING);
                    }
                    else {
                        if (too_large)
                            throw StriException(MSG__CHARSXP_2147483647);
                        SET_STRING_ELT(
                            result, 0,
                            Rf_mkCharLenCE(
                                streaming_output.empty()
                                    ? ""
                                    : streaming_output.data(),
                                static_cast<int>(streaming_output.size()),
                                CE_UTF8
                            )
                        );
                    }
                }
                else {
                    const SEXP* source = STRING_PTR_RO(str);
                    raw_values.resize(static_cast<size_t>(str_length));
                    for (R_len_t i = 0; i < str_length; ++i) {
                        raw_values[static_cast<size_t>(i)] =
                            io::as_shared_view(source[i]);
                    }
                    normalize_flatten_inputs(
                        raw_values, value_converter, value_storage, values
                    );

                    shared::StringView separator{
                        nullptr, 0, shared::StringEncoding::ascii
                    };
                    const shared::StringView* separator_ptr = nullptr;
                    if (!empty_separator) {
                        separator = normalize_flatten_separator(
                            io::as_shared_view(separator_sexp),
                            separator_converter, separator_storage
                        );
                        separator_ptr = &separator;
                    }

                    build_flatten_bytes(
                        values, separator_ptr, na_empty_value,
                        omit_empty_value, plan, output
                    );
                    if (plan.has_na) {
                        SET_STRING_ELT(result, 0, NA_STRING);
                    }
                    else {
                        SET_STRING_ELT(
                            result, 0,
                            Rf_mkCharLenCE(
                                output.empty() ? "" : output.data(),
                                static_cast<int>(output.size()), CE_UTF8
                            )
                        );
                    }
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}


} } // namespace charr::base_backend
