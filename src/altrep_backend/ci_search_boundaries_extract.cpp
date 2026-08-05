
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

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

namespace charr { namespace altrep_backend {

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
    return io::as_charport_view(shared::StringView{
        value.ptr + range.start,
        range.end - range.start,
        value.enc
    });
}


CHARR_R_HELPER void emit_fallback_warning_r() noexcept
{
    Rf_warning(
        "%s", ICUError::getICUerrorName(U_USING_DEFAULT_WARNING)
    );
}


class FirstBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER FirstBody(
        const std::vector<shared::StringView>& normalized,
        const shared::BoundaryOptions& options,
        std::vector<unsigned char>& fallback,
        std::vector<int>& failures,
        io::ParallelOutputBuilder& builder
    ) noexcept
        : normalized_(normalized), options_(options),
          fallback_(fallback), failures_(failures), builder_(builder)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        shared::BoundaryIterator iterator;
        bool opened = false;
        bool root_fallback = false;
        const bool ascii_word_first =
            shared::boundary_ascii_word_first(options_);
        try {
            while (context.next_chunk()) {
                for (R_xlen_t i = context.begin; i < context.end; ++i) {
                    const shared::StringView& value = normalized_[
                        static_cast<std::size_t>(i)
                    ];
                    if (value.is_na() || value.len == 0) {
                        builder_.set_na(context.worker, i);
                        continue;
                    }

                    int ascii_word_end = 0;
                    if (ascii_word_first &&
                            shared::boundary_ascii_initial_word(
                                value.ptr, value.len, ascii_word_end
                            )) {
                        builder_.set(
                            context.worker, i, value.ptr,
                            static_cast<std::size_t>(ascii_word_end),
                            CETYPE_EXT_ASCII
                        );
                        continue;
                    }

                    ensure_iterator(
                        iterator, options_, opened, root_fallback
                    );
                    require_icu_success(iterator.set_text(value));
                    iterator.first();
                    shared::BoundaryRange range{0, 0};
                    if (!iterator.next(range))
                        builder_.set_na(context.worker, i);
                    else
                        builder_.set(
                            context.worker, i, boundary_record(value, range)
                        );
                }
            }
        }
        catch (...) {
            fallback_[context.worker] =
                static_cast<unsigned char>(root_fallback);
            failures_[context.worker] = 1;
            throw;
        }
        fallback_[context.worker] = static_cast<unsigned char>(root_fallback);
    }

private:
    const std::vector<shared::StringView>& normalized_;
    const shared::BoundaryOptions& options_;
    std::vector<unsigned char>& fallback_;
    std::vector<int>& failures_;
    io::ParallelOutputBuilder& builder_;
};


class AllBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER AllBody(
        const std::vector<shared::StringView>& normalized,
        const shared::BoundaryOptions& options,
        bool omit,
        std::vector<io::OutputStore>& stores,
        std::vector<R_len_t>& max_columns,
        std::vector<unsigned char>& fallback,
        std::vector<int>& failures
    ) noexcept
        : normalized_(normalized), options_(options), omit_(omit),
          stores_(stores), max_columns_(max_columns),
          fallback_(fallback), failures_(failures)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        shared::BoundaryIterator iterator;
        io::GrowableOutputBuilder builder;
        bool opened = false;
        bool root_fallback = false;
        // The column count is a maximum over every task this worker runs,
        // whichever chunks those came from, so it stays outside the chunk
        // loop and is published once at the end.
        R_len_t local_max_columns = 0;
        try {
            while (context.next_chunk()) {
                for (R_xlen_t task = context.begin;
                        task < context.end; ++task) {
                    const std::size_t index =
                        static_cast<std::size_t>(task);
                    io::OutputStore& output = stores_[index];
                    const shared::StringView& value = normalized_[index];
                    if (value.is_na()) {
                        output = io::scalar_store(
                            io::missing_output_record()
                        );
                        if (local_max_columns < 1)
                            local_max_columns = 1;
                        continue;
                    }

                    ensure_iterator(
                        iterator, options_, opened, root_fallback
                    );
                    require_icu_success(iterator.set_text(value));
                    iterator.first();
                    builder.reset();
                    R_len_t range_count = 0;
                    shared::BoundaryRange range{0, 0};
                    while (iterator.next(range)) {
                        builder.append(boundary_record(value, range));
                        ++range_count;
                    }
                    if (range_count == 0) {
                        if (!omit_) {
                            output = io::scalar_store(
                                io::missing_output_record()
                            );
                            if (local_max_columns < 1)
                                local_max_columns = 1;
                        }
                        continue;
                    }
                    output = builder.release_store();
                    if (local_max_columns < range_count)
                        local_max_columns = range_count;
                }
            }
        }
        catch (...) {
            fallback_[context.worker] =
                static_cast<unsigned char>(root_fallback);
            failures_[context.worker] = 1;
            throw;
        }
        fallback_[context.worker] =
            static_cast<unsigned char>(root_fallback);
        max_columns_[context.worker] = local_max_columns;
    }

private:
    const std::vector<shared::StringView>& normalized_;
    const shared::BoundaryOptions& options_;
    bool omit_;
    std::vector<io::OutputStore>& stores_;
    std::vector<R_len_t>& max_columns_;
    std::vector<unsigned char>& fallback_;
    std::vector<int>& failures_;
};

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


    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    const shared::BoundaryOptions options =
        boundary::prepare_options_r(opts_brkiter, UBRK_LINE);


    bool iterator_open = false;
    bool root_fallback_warning = false;

    try {
        charport::Reader reader;
        charport::StrViews source_views;
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        std::vector<shared::StringView> normalized;
        shared::BoundaryIterator iterator;
        io::OutputBuilder builder(0);
        io::ParallelOutputBuilder parallel_builder;
        std::vector<unsigned char> fallback;
        std::vector<int> failures;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                const shared::ParallelPlan plan = shared::parallel_plan(
                    true, length
                );
                reader.reset(str);
                if (reader.size() != length) {
                    throw std::runtime_error(
                        "Reader length changed during boundary extraction"
                    );
                }
                source_views.resize(length);
                if (length > 0) {
                    reader.views(
                        0, length,
                        source_views.ptrs(), source_views.lengths(),
                        source_views.encodings()
                    );
                }
                normalize_input(
                    source_views, converter, storage, normalized
                );

                if (plan.workers > 1) {
                    fallback.assign(plan.workers, 0);
                    failures.assign(plan.workers, 0);
                    parallel_builder.reset(length, plan.workers);
                    FirstBody body(
                        normalized, options, fallback, failures,
                        parallel_builder
                    );
                    try {
                        shared::run_parallel(plan, length, body);
                    }
                    catch (...) {
                        unsigned limit = 0;
                        while (limit < plan.workers && failures[limit] == 0)
                            ++limit;
                        if (limit < plan.workers)
                            ++limit;
                        for (unsigned worker = 0; worker < limit; ++worker) {
                            root_fallback_warning = root_fallback_warning ||
                                fallback[worker] != 0;
                        }
                        throw;
                    }
                    for (unsigned worker = 0;
                            worker < plan.workers; ++worker) {
                        root_fallback_warning = root_fallback_warning ||
                            fallback[worker] != 0;
                    }
                    result = entry_protections.reprotect_one(
                        parallel_builder.to_sexp(), result_index
                    );
                }
                else {
                    builder.reset(length);
                    const bool ascii_word_first =
                        shared::boundary_ascii_word_first(options);
                    for (R_len_t i = 0; i < length; ++i) {
                        const shared::StringView& value = normalized[
                            static_cast<std::size_t>(i)
                        ];
                        if (value.is_na() || value.len == 0) {
                            builder.set_na(i);
                            continue;
                        }

                        int ascii_word_end = 0;
                        if (ascii_word_first &&
                                shared::boundary_ascii_initial_word(
                                    value.ptr, value.len, ascii_word_end
                                )) {
                            builder.set(
                                i, value.ptr,
                                static_cast<std::size_t>(ascii_word_end),
                                CETYPE_EXT_ASCII
                            );
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
                            builder.set_na(i);
                            continue;
                        }
                        builder.set(i, boundary_record(value, range));
                    }

                    result = entry_protections.reprotect_one(
                        builder.to_sexp(), result_index
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
    simplify = entry_protections.protect_one(
        ci__prepare_arg_logical_1_r(
            simplify, "simplify"
        )
    );
    const int simplify_value = LOGICAL_RO(simplify)[0];
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    const shared::BoundaryOptions options =
        boundary::prepare_options_r(opts_brkiter, UBRK_LINE);

    SEXP temporary = R_NilValue;
    PROTECT_INDEX temporary_index;

    bool iterator_open = false;
    bool root_fallback_warning = false;
    R_len_t max_columns = 0;

    try {
        charport::Reader reader;
        charport::StrViews source_views;
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        std::vector<shared::StringView> normalized;
        shared::BoundaryIterator iterator;
        std::vector<io::OutputStore> stores;
        io::GrowableOutputBuilder child_builder;
        io::OutputBuilder matrix_builder(0);
        std::vector<R_len_t> worker_max_columns;
        std::vector<unsigned char> fallback;
        std::vector<int> failures;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );

                reader.reset(str);
                if (reader.size() != length) {
                    throw std::runtime_error(
                        "Reader length changed during boundary extraction"
                    );
                }
                source_views.resize(length);
                if (length > 0) {
                    reader.views(
                        0, length,
                        source_views.ptrs(), source_views.lengths(),
                        source_views.encodings()
                    );
                }
                normalize_input(
                    source_views, converter, storage, normalized
                );

                stores.reserve(static_cast<std::size_t>(length));
                for (R_len_t i = 0; i < length; ++i)
                    stores.emplace_back(0, 0);

                const shared::ParallelPlan parallel_plan =
                    shared::parallel_plan(true, length);
                if (parallel_plan.workers == 1) {
                for (R_len_t i = 0; i < length; ++i) {
                    io::OutputStore& output = stores[
                        static_cast<std::size_t>(i)
                    ];
                    const shared::StringView& value = normalized[
                        static_cast<std::size_t>(i)
                    ];
                    if (value.is_na()) {
                        output = io::scalar_store(
                            io::missing_output_record()
                        );
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
                    child_builder.reset();
                    R_len_t range_count = 0;
                    shared::BoundaryRange range{0, 0};
                    while (iterator.next(range)) {
                        child_builder.append(
                            boundary_record(value, range)
                        );
                        ++range_count;
                    }
                    if (range_count == 0) {
                        if (!omit) {
                            output = io::scalar_store(
                                io::missing_output_record()
                            );
                            if (max_columns < 1)
                                max_columns = 1;
                        }
                        continue;
                    }

                    output = child_builder.release_store();
                    if (max_columns < range_count)
                        max_columns = range_count;
                }
                }
                else {
                    worker_max_columns.assign(
                        parallel_plan.workers, 0
                    );
                    fallback.assign(parallel_plan.workers, 0);
                    failures.assign(parallel_plan.workers, 0);
                    AllBody body(
                        normalized, options, omit, stores,
                        worker_max_columns, fallback, failures
                    );
                    try {
                        shared::run_parallel(
                            parallel_plan, length, body
                        );
                    }
                    catch (...) {
                        unsigned limit = 0;
                        while (limit < parallel_plan.workers &&
                                failures[limit] == 0) {
                            ++limit;
                        }
                        if (limit < parallel_plan.workers)
                            ++limit;
                        for (unsigned worker = 0;
                                worker < limit; ++worker) {
                            root_fallback_warning = root_fallback_warning ||
                                fallback[worker] != 0;
                        }
                        throw;
                    }
                    for (unsigned worker = 0;
                            worker < parallel_plan.workers; ++worker) {
                        root_fallback_warning = root_fallback_warning ||
                            fallback[worker] != 0;
                        if (max_columns < worker_max_columns[worker])
                            max_columns = worker_max_columns[worker];
                    }
                }

                callback_protections.protect_with_index(
                    temporary, &temporary_index
                );
                if (simplify_value != NA_LOGICAL && !simplify_value) {
                    result = entry_protections.reprotect_one(
                        Rf_allocVector(VECSXP, length), result_index
                    );
                    for (R_len_t i = 0; i < length; ++i) {
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
                    const R_xlen_t rows = length;
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
                    INTEGER(temporary)[0] = length;
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
        if (root_fallback_warning)
            emit_fallback_warning_r();
    );
}

} } // namespace charr::altrep_backend
