
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
        switch (value.enc.value) {
        case CETYPE_EXT_ASCII.value:
        case CETYPE_EXT_UTF8.value:
        case CETYPE_EXT_ASCII_OR_UTF8.value:
            break;
        case CETYPE_EXT_NATIVE.value:
        case CETYPE_EXT_LATIN1.value:
            return false;
        case CETYPE_EXT_BYTES.value:
            throw StriException(MSG__BYTESENC);
        case CETYPE_EXT_NA.value:
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
    if ((value.enc == CETYPE_EXT_UTF8 ||
            value.enc == CETYPE_EXT_ASCII_OR_UTF8) &&
            length >= 3 &&
            static_cast<unsigned char>(data[0]) == 0xefU &&
            static_cast<unsigned char>(data[1]) == 0xbbU &&
            static_cast<unsigned char>(data[2]) == 0xbfU) {
        data += 3;
        length -= 3;
    }

    const bool ascii = value.enc == CETYPE_EXT_ASCII ||
        (value.enc == CETYPE_EXT_ASCII_OR_UTF8 &&
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
    return ascii ? CETYPE_EXT_ASCII : CETYPE_EXT_UTF8;
}


class MissingBody final : public ParallelBody {
public:
    CHARR_NEUTRAL_HELPER explicit MissingBody(
        io::ParallelOutputBuilder& builder
    ) noexcept : builder_(builder) {}

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        while (context.next_chunk()) {
            for (R_xlen_t i = context.begin; i < context.end; ++i)
                builder_.set_na(context.worker, i);
        }
    }

private:
    io::ParallelOutputBuilder& builder_;
};


class DirectPairBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER DirectPairBody(
        const vector<charport::StrViews>& views,
        const vector<R_len_t>& lengths, R_len_t rows,
        io::ParallelOutputBuilder& builder
    ) noexcept
        : views_(views), lengths_(lengths), rows_(rows), builder_(builder)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        const R_len_t first_length = lengths_[0];
        const R_len_t second_length = lengths_[1];
        const bool first_scalar = first_length == 1;
        const bool second_scalar = second_length == 1;
        const shared::StringView first_scalar_value = first_scalar
            ? direct_input(views_[0][0])
            : shared::StringView{
                nullptr, 0, shared::StringEncoding::missing
            };
        const shared::StringView second_scalar_value = second_scalar
            ? direct_input(views_[1][0])
            : shared::StringView{
                nullptr, 0, shared::StringEncoding::missing
            };
        while (context.next_chunk()) {
            for (R_xlen_t task = context.begin; task < context.end; ++task) {
                const R_len_t row = static_cast<R_len_t>(task);
                const shared::StringView first = first_scalar
                    ? first_scalar_value
                    : direct_input(views_[0][
                        first_length == rows_ ? row : row % first_length
                    ]);
                const shared::StringView second = second_scalar
                    ? second_scalar_value
                    : direct_input(views_[1][
                        second_length == rows_ ? row : row % second_length
                    ]);
                if (first.is_na() || second.is_na()) {
                    builder_.set_na(context.worker, row);
                    continue;
                }
                const size_t first_bytes = static_cast<size_t>(first.len);
                const size_t second_bytes = static_cast<size_t>(second.len);
                if (first_bytes > static_cast<size_t>(POW_2_31_M_1)-
                        second_bytes) {
                    throw StriException(MSG__CHARSXP_2147483647);
                }
                char* destination = builder_.reserve(
                    context.worker, row, first_bytes+second_bytes,
                    output_encoding(
                        first.enc == shared::StringEncoding::ascii &&
                        second.enc == shared::StringEncoding::ascii
                    )
                );
                if (first_bytes > 0)
                    memcpy(destination, first.ptr, first_bytes);
                if (second_bytes > 0)
                    memcpy(destination+first_bytes, second.ptr, second_bytes);
            }
        }
    }

private:
    const vector<charport::StrViews>& views_;
    const vector<R_len_t>& lengths_;
    R_len_t rows_;
    io::ParallelOutputBuilder& builder_;
};


class RowsBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER RowsBody(
        const vector<shared::join::Column>& columns,
        const shared::StringView& separator, R_len_t rows,
        io::ParallelOutputBuilder& builder
    ) noexcept
        : columns_(columns), separator_(separator), rows_(rows),
          builder_(builder)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        while (context.next_chunk()) {
            for (R_xlen_t task = context.begin; task < context.end; ++task) {
                const R_len_t row = static_cast<R_len_t>(task);
                shared::join::FlattenPlan plan;
                shared::join::plan_join_row(
                    columns_.data(), columns_.size(),
                    static_cast<size_t>(row), static_cast<size_t>(rows_),
                    separator_, plan
                );
                if (plan.has_na) {
                    builder_.set_na(context.worker, row);
                    continue;
                }
                if (plan.too_large)
                    throw StriException(MSG__CHARSXP_2147483647);
                char* destination = builder_.reserve(
                    context.worker, row, plan.bytes,
                    output_encoding(plan.ascii)
                );
                shared::join::write_join_row(
                    columns_.data(), columns_.size(),
                    static_cast<size_t>(row), static_cast<size_t>(rows_),
                    separator_, destination
                );
            }
        }
    }

private:
    const vector<shared::join::Column>& columns_;
    shared::StringView separator_;
    R_len_t rows_;
    io::ParallelOutputBuilder& builder_;
};


class CollapsePlanBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER CollapsePlanBody(
        const vector<shared::join::Column>& columns,
        const shared::StringView& separator, R_len_t rows,
        vector<shared::join::FlattenPlan>& plans
    ) noexcept
        : columns_(columns), separator_(separator), rows_(rows), plans_(plans)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        while (context.next_chunk()) {
            for (R_xlen_t task = context.begin; task < context.end; ++task) {
                const size_t row = static_cast<size_t>(task);
                shared::join::plan_join_row(
                    columns_.data(), columns_.size(), row,
                    static_cast<size_t>(rows_), separator_, plans_[row]
                );
            }
        }
    }

private:
    const vector<shared::join::Column>& columns_;
    shared::StringView separator_;
    R_len_t rows_;
    vector<shared::join::FlattenPlan>& plans_;
};


class CollapseWriteBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER CollapseWriteBody(
        const vector<shared::join::Column>& columns,
        const shared::StringView& separator,
        const shared::StringView& collapse, R_len_t rows,
        const vector<size_t>& offsets, char* output
    ) noexcept
        : columns_(columns), separator_(separator), collapse_(collapse),
          rows_(rows), offsets_(offsets), output_(output)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        while (context.next_chunk()) {
            for (R_xlen_t task = context.begin; task < context.end; ++task) {
                const size_t row = static_cast<size_t>(task);
                char* destination = output_;
                if (offsets_[row] > 0)
                    destination += offsets_[row];
                if (row > 0 && collapse_.len > 0) {
                    const size_t collapse_bytes =
                        static_cast<size_t>(collapse_.len);
                    memcpy(destination, collapse_.ptr, collapse_bytes);
                    destination += collapse_bytes;
                }
                shared::join::write_join_row(
                    columns_.data(), columns_.size(), row,
                    static_cast<size_t>(rows_), separator_, destination
                );
            }
        }
    }

private:
    const vector<shared::join::Column>& columns_;
    shared::StringView separator_;
    shared::StringView collapse_;
    R_len_t rows_;
    const vector<size_t>& offsets_;
    char* output_;
};

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
        vector<size_t> row_offsets;
        io::OutputBuilder builder(0);
        io::ParallelOutputBuilder parallel_builder;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                do {
                if (column_count <= 0) {
                    builder.reset(collapse_output ? 1 : 0);
                    if (collapse_output) {
                        builder.set(
                            0, "", 0, CETYPE_EXT_ASCII
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
                    const shared::ParallelPlan parallel_plan =
                        shared::parallel_plan(true, row_count);
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
                        if (parallel_plan.workers > 1) {
                            parallel_builder.reset(
                                row_count, parallel_plan.workers
                            );
                            join_frame::MissingBody body(parallel_builder);
                            shared::run_parallel(
                                parallel_plan, row_count, body
                            );
                            result = entry_protections.reprotect_one(
                                parallel_builder.to_sexp(), result_index
                            );
                            break;
                        }
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
                        if (parallel_plan.workers > 1) {
                            parallel_builder.reset(
                                row_count, parallel_plan.workers
                            );
                            join_frame::DirectPairBody body(
                                row_views, lengths, row_count,
                                parallel_builder
                            );
                            shared::run_parallel(
                                parallel_plan, row_count, body
                            );
                            result = entry_protections.reprotect_one(
                                parallel_builder.to_sexp(), result_index
                            );
                            break;
                        }
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

                    if (parallel_plan.workers > 1) {
                        parallel_builder.reset(
                            row_count, parallel_plan.workers
                        );
                        join_frame::RowsBody body(
                            columns, separator, row_count,
                            parallel_builder
                        );
                        shared::run_parallel(
                            parallel_plan, row_count, body
                        );
                        result = entry_protections.reprotect_one(
                            parallel_builder.to_sexp(), result_index
                        );
                        break;
                    }

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
                            0, "", 0, CETYPE_EXT_ASCII
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
                            0, "", 0, CETYPE_EXT_ASCII
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
                const shared::ParallelPlan collapse_plan =
                    shared::parallel_plan(true, row_count);
                if (collapse_plan.workers > 1) {
                    row_plans.resize(static_cast<size_t>(row_count));
                    join_frame::CollapsePlanBody plan_body(
                        columns, separator, row_count, row_plans
                    );
                    shared::run_parallel(
                        collapse_plan, row_count, plan_body
                    );

                    bool has_na = false;
                    for (R_len_t row = 0; row < row_count; ++row) {
                        if (row_plans[static_cast<size_t>(row)].has_na) {
                            has_na = true;
                            break;
                        }
                    }
                    if (has_na) {
                        builder.reset(1);
                        builder.set_na(0);
                        result = entry_protections.reprotect_one(
                            builder.to_sexp(), result_index
                        );
                        break;
                    }

                    const size_t limit =
                        static_cast<size_t>(POW_2_31_M_1);
                    const size_t collapse_bytes =
                        static_cast<size_t>(collapse_value.len);
                    const bool collapse_ascii =
                        collapse_value.enc ==
                            shared::StringEncoding::ascii ||
                        (collapse_value.enc ==
                            shared::StringEncoding::ascii_or_utf8 &&
                            join_frame::is_ascii(
                                collapse_value.ptr, collapse_value.len
                            ));
                    size_t total_bytes = 0;
                    bool too_large = false;
                    bool ascii = true;
                    row_offsets.assign(
                        static_cast<size_t>(row_count), 0
                    );
                    for (R_len_t row = 0; row < row_count; ++row) {
                        const size_t index = static_cast<size_t>(row);
                        const shared::join::FlattenPlan& row_plan =
                            row_plans[index];
                        row_offsets[index] = total_bytes;
                        const size_t leading_bytes = row > 0
                            ? collapse_bytes : 0;
                        if (row_plan.too_large ||
                                leading_bytes > limit-total_bytes ||
                                row_plan.bytes >
                                    limit-total_bytes-leading_bytes) {
                            too_large = true;
                            break;
                        }
                        total_bytes += leading_bytes+row_plan.bytes;
                        ascii = ascii && row_plan.ascii &&
                            (row == 0 || collapse_ascii);
                    }
                    if (too_large)
                        throw StriException(MSG__CHARSXP_2147483647);

                    builder.reset(1);
                    char* destination = builder.reserve(
                        0, total_bytes,
                        join_frame::output_encoding(ascii)
                    );
                    join_frame::CollapseWriteBody write_body(
                        columns, separator, collapse_value, row_count,
                        row_offsets, destination
                    );
                    shared::run_parallel(
                        collapse_plan, row_count, write_body
                    );
                    result = entry_protections.reprotect_one(
                        builder.to_sexp(), result_index
                    );
                    break;
                }

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
                        0, 0, CETYPE_EXT_ASCII
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
                                ? CETYPE_EXT_ASCII
                                : CETYPE_EXT_UTF8
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
