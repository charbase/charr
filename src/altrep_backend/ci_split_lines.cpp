
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
#include "../shared/line_split.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <charport.h>

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

namespace charr { namespace altrep_backend {


namespace split_lines {


CHARR_NEUTRAL_HELPER R_len_t recycling_length(
    R_len_t first, R_len_t second, bool& warning
) noexcept
{
    warning = false;
    if (first <= 0 || second <= 0)
        return 0;

    const R_len_t result = first > second ? first : second;
    warning = result % first != 0 || result % second != 0;
    return result;
}


CHARR_NEUTRAL_HELPER R_len_t vectorize_next(
    R_len_t index, R_len_t source_length, R_len_t output_length
) noexcept
{
    if (index == output_length - 1 - (output_length % source_length))
        return output_length;
    index += source_length;
    return index >= output_length ? (index % source_length) + 1 : index;
}


CHARR_CXX_HELPER void normalize_inputs(
    const charport::StrViews& views,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    std::vector<shared::StringView>& output
)
{
    output.resize(static_cast<std::size_t>(views.size()));
    for (R_xlen_t i = 0; i < views.size(); ++i) {
        const shared::StringView value = io::as_shared_view(views[i]);
        if (value.enc == shared::StringEncoding::bytes)
            throw StriException(MSG__BYTESENC);
        output[static_cast<std::size_t>(i)] = shared::normalize_utf8(
            value, converter, storage
        );
    }
}


CHARR_NEUTRAL_HELPER io::OutputRecord line_record(
    const shared::StringView& value,
    const shared::line_split::LineSlice& line
) noexcept
{
    return charport::StrView{
        value.ptr + line.begin,
        line.length,
        line.ascii
            ? CETYPE_EXT_ASCII
            : CETYPE_EXT_UTF8
    };
}


CHARR_CXX_HELPER void build_store(
    const shared::StringView& value,
    bool omit_empty,
    bool keep_trailing_empty,
    shared::line_split::ScanResult& scan,
    io::OutputBuilder& builder,
    io::OutputStore& output
)
{
    shared::line_split::scan_utf8(
        value.ptr, value.len, omit_empty, keep_trailing_empty, scan
    );

    const R_xlen_t size = static_cast<R_xlen_t>(scan.lines.size());
    if (size == 1) {
        output = io::scalar_store(line_record(value, scan.lines[0]));
        return;
    }
    if (size <= 0) {
        output = io::OutputStore();
        return;
    }

    builder.reset(size);
    for (R_xlen_t i = 0; i < size; ++i) {
        builder.set_validated(
            i, line_record(
                value, scan.lines[static_cast<std::size_t>(i)]
            )
        );
    }
    output = builder.release_store();
}


class Body final : public ParallelBody {
public:
    CHARR_CXX_HELPER Body(
        const std::vector<shared::StringView>& normalized,
        const int* omit_values,
        R_len_t source_length,
        R_len_t omit_length,
        R_len_t vectorize_length,
        std::vector<io::OutputStore>& stores
    ) noexcept
        : normalized_(normalized), omit_values_(omit_values),
          source_length_(source_length), omit_length_(omit_length),
          vectorize_length_(vectorize_length), stores_(stores)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        shared::line_split::ScanResult scan;
        io::OutputBuilder builder(0);

        while (context.next_chunk()) {
            const R_len_t begin = static_cast<R_len_t>(context.begin);
            const R_len_t end = static_cast<R_len_t>(context.end);

            if (source_length_ == 1) {
                const shared::StringView& value = normalized_[0];
                for (R_len_t i = begin; i < end; ++i) {
                    split_one(i, value, scan, builder);
                }
                continue;
            }

            for (R_len_t lane = begin; lane < end; ++lane) {
                const shared::StringView& value = normalized_[
                    static_cast<std::size_t>(lane)
                ];
                for (R_len_t i = lane; i < vectorize_length_;
                        i += source_length_) {
                    split_one(i, value, scan, builder);
                }
            }
        }
    }

private:
    CHARR_CXX_HELPER void split_one(
        R_len_t i,
        const shared::StringView& value,
        shared::line_split::ScanResult& scan,
        io::OutputBuilder& builder
    )
    {
        io::OutputStore& output = stores_[static_cast<std::size_t>(i)];
        if (value.is_na()) {
            output = io::scalar_store(io::missing_output_record());
            return;
        }

        build_store(
            value,
            omit_values_[i % omit_length_] != 0,
            true,
            scan,
            builder,
            output
        );
    }

    const std::vector<shared::StringView>& normalized_;
    const int* omit_values_;
    R_len_t source_length_;
    R_len_t omit_length_;
    R_len_t vectorize_length_;
    std::vector<io::OutputStore>& stores_;
};


} // namespace split_lines

using namespace split_lines;


/**
 * Split one string into text lines.
 *
 * @param str character vector
 * @return character vector
 */
CHARR_ENTRYPOINT SEXP ci_split_lines1(SEXP str) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(
        ci__prepare_arg_string_1_r(str, "str")
    );
    const R_len_t source_length = LENGTH(str);

    try {
        charport::Reader reader;
        charport::StrViews views;
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        std::vector<shared::StringView> normalized;
        shared::line_split::ScanResult scan;
        io::OutputBuilder builder(0);
        io::OutputStore output;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                reader.reset(str);
                if (reader.size() != source_length) {
                    throw std::runtime_error(
                        "Reader length changed during line splitting"
                    );
                }
                views.resize(source_length);
                reader.views(
                    0, source_length,
                    views.ptrs(), views.lengths(), views.encodings()
                );
                normalize_inputs(views, converter, storage, normalized);

                const shared::StringView& value = normalized[0];
                if (value.is_na()) {
                    result = entry_protections.reprotect_one(str, result_index);
                }
                else {
                    build_store(
                        value, false, false, scan, builder, output
                    );
                    result = entry_protections.reprotect_one(
                        io::finalize(std::move(output)), result_index
                    );
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}


/**
 * Split strings into text lines.
 *
 * @param str character vector
 * @param omit_empty logical vector
 * @return list of character vectors
 */
CHARR_ENTRYPOINT SEXP ci_split_lines(
    SEXP str, SEXP omit_empty
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    omit_empty = entry_protections.protect_one(
        ci__prepare_arg_logical_r(
            omit_empty, "omit_empty"
        )
    );

    const R_len_t source_length = LENGTH(str);
    const R_len_t omit_length = LENGTH(omit_empty);
    bool recycling_warning = false;
    const R_len_t vectorize_length = recycling_length(
        source_length, omit_length, recycling_warning
    );
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);

    SEXP temporary = R_NilValue;
    PROTECT_INDEX temporary_index;

    try {
        charport::Reader reader;
        charport::StrViews views;
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        std::vector<shared::StringView> normalized;
        shared::line_split::ScanResult scan;
        io::OutputBuilder builder(0);
        std::vector<io::OutputStore> stores;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                // String normalization precedes logical-vector access in
                // stringi, including errors from a later bytes-marked record.
                if (vectorize_length > 0) {
                    reader.reset(str);
                    if (reader.size() != source_length) {
                        throw std::runtime_error(
                            "Reader length changed during line splitting"
                        );
                    }
                    views.resize(source_length);
                    reader.views(
                        0, source_length,
                        views.ptrs(), views.lengths(), views.encodings()
                    );
                    normalize_inputs(
                        views, converter, storage, normalized
                    );
                }

                const int* omit_values = LOGICAL_RO(omit_empty);
                stores.reserve(static_cast<std::size_t>(vectorize_length));
                for (R_len_t i = 0; i < vectorize_length; ++i)
                    stores.emplace_back(0, 0);

                const R_len_t tasks = vectorize_length == 0
                    ? 0 : source_length == 1
                        ? vectorize_length : source_length;
                const shared::ParallelPlan plan = shared::parallel_plan(
                    true, tasks
                );
                if (plan.workers == 1) {
                    // Visit recycled outputs source by source, matching the
                    // original container's traversal and allocation order.
                    for (R_len_t i = 0; i < vectorize_length;
                            i = vectorize_next(
                                i, source_length, vectorize_length
                            )) {
                        io::OutputStore& output = stores[
                            static_cast<std::size_t>(i)
                        ];
                        const shared::StringView& value = normalized[
                            static_cast<std::size_t>(i % source_length)
                        ];
                        if (value.is_na()) {
                            output = io::scalar_store(
                                io::missing_output_record()
                            );
                            continue;
                        }

                        build_store(
                            value,
                            omit_values[i % omit_length] != 0,
                            true,
                            scan,
                            builder,
                            output
                        );
                    }
                }
                else {
                    Body body(
                        normalized, omit_values,
                        source_length, omit_length, vectorize_length,
                        stores
                    );
                    shared::run_parallel(plan, tasks, body);
                }

                // All child Stores are complete before list allocation and
                // wrapping begin; result assembly is R-only from here.
                result = entry_protections.reprotect_one(
                    Rf_allocVector(VECSXP, vectorize_length), result_index
                );
                if (vectorize_length > 0) {
                    callback_protections.protect_with_index(
                        temporary, &temporary_index
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

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}


} } // namespace charr::altrep_backend
