
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
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "ci_stringi.h"
#include "ci_parallel.h"
#include "io/reader_utils.h"
#include "boundary/options_r.h"
#include "io/string_view.h"
#include "../shared/boundary_iterator.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/r_matrix.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <charport.h>

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <vector>

namespace charr { namespace altrep_backend {

namespace search_boundaries_locate {

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
    output.reserve(static_cast<std::size_t>(source.size()));
    for (R_xlen_t i = 0; i < source.size(); ++i) {
        const shared::StringView value = io::as_shared_view(source[i]);
        if (value.enc == shared::StringEncoding::bytes)
            throw StriException(MSG__BYTESENC);
        output.push_back(
            shared::normalize_utf8(value, converter, storage)
        );
    }
}


CHARR_CXX_HELPER bool normalize_first_input(
    const charport::StrViews& source,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    bool ascii_word_first,
    std::vector<shared::StringView>& output
)
{
    bool needs_iterator = false;
    output.reserve(static_cast<std::size_t>(source.size()));
    for (R_xlen_t i = 0; i < source.size(); ++i) {
        const shared::StringView input = io::as_shared_view(source[i]);
        if (input.enc == shared::StringEncoding::bytes)
            throw StriException(MSG__BYTESENC);
        const shared::StringView value = shared::normalize_utf8(
            input, converter, storage
        );
        output.push_back(value);

        int ascii_word_end = 0;
        needs_iterator = needs_iterator ||
            (!value.is_na() && value.len > 0 &&
             !(ascii_word_first &&
               shared::boundary_ascii_initial_word(
                   value.ptr, value.len, ascii_word_end
               )));
    }
    return needs_iterator;
}


class FirstBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER FirstBody(
        const std::vector<shared::StringView>& values,
        const shared::BoundaryOptions& options,
        bool return_length, bool ascii_word_first, bool needs_iterator,
        R_len_t length, int* output, bool& root_fallback
    ) noexcept
        : values_(values), options_(options), return_length_(return_length),
          ascii_word_first_(ascii_word_first),
          needs_iterator_(needs_iterator), length_(length), output_(output),
          root_fallback_(root_fallback)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        shared::BoundaryIterator iterator;
        if (needs_iterator_) {
            const shared::BoundaryOpenResult opened = iterator.reset(options_);
            if (context.worker == 0)
                root_fallback_ = opened.root_fallback;
            require_icu_success(opened.status);
        }

        while (context.next_chunk()) {
            const R_len_t begin = static_cast<R_len_t>(context.begin);
            const R_len_t end = static_cast<R_len_t>(context.end);
            for (R_len_t i = begin; i < end; ++i) {
                output_[i] = NA_INTEGER;
                output_[i+length_] = NA_INTEGER;

                const shared::StringView& value = values_[
                    static_cast<std::size_t>(i)
                ];
                if (value.is_na())
                    continue;

                if (return_length_) {
                    output_[i] = -1;
                    output_[i+length_] = -1;
                }
                if (value.len == 0)
                    continue;

                int ascii_word_end = 0;
                if (ascii_word_first_ &&
                        shared::boundary_ascii_initial_word(
                            value.ptr, value.len, ascii_word_end
                        )) {
                    output_[i] = 1;
                    output_[i+length_] = ascii_word_end;
                    continue;
                }

                require_icu_success(iterator.set_text(value));
                iterator.first();
                shared::BoundaryRange range{0, 0};
                if (!iterator.next(range))
                    continue;

                shared::Utf8PositionCursor cursor(value);
                const int start = cursor.at_byte(range.start) + 1;
                const int finish = cursor.at_byte(range.end);
                output_[i] = start;
                output_[i+length_] = return_length_
                    ? finish - start + 1
                    : finish;
            }
        }
    }

private:
    const std::vector<shared::StringView>& values_;
    const shared::BoundaryOptions& options_;
    bool return_length_;
    bool ascii_word_first_;
    bool needs_iterator_;
    R_len_t length_;
    int* output_;
    bool& root_fallback_;
};


class CHARR_OWNER_TYPE AllBoundaryRows {
public:
    CHARR_CXX_HELPER AllBoundaryRows() noexcept
        : rows_(nullptr) {}

    CHARR_CXX_HELPER ~AllBoundaryRows() noexcept
    {
        delete[] rows_;
    }

    AllBoundaryRows(const AllBoundaryRows&) = delete;
    AllBoundaryRows& operator=(const AllBoundaryRows&) = delete;
    AllBoundaryRows(AllBoundaryRows&&) = delete;
    AllBoundaryRows& operator=(AllBoundaryRows&&) = delete;

    CHARR_CXX_HELPER void reset(std::size_t size)
    {
        std::vector<shared::BoundaryRange>* replacement = size == 0
            ? nullptr : new std::vector<shared::BoundaryRange>[size];
        delete[] rows_;
        rows_ = replacement;
    }

    CHARR_CXX_HELPER void copy_from(
        std::size_t index,
        std::vector<shared::BoundaryRange>& source
    )
    {
        std::vector<shared::BoundaryRange>& output = rows_[index];
        output.clear();
        for (std::size_t i = 0; i < source.size(); ++i)
            output.push_back(source[i]);
    }

    CHARR_NEUTRAL_HELPER std::size_t count(
        std::size_t index
    ) noexcept
    {
        return rows_[index].size();
    }

    CHARR_NEUTRAL_HELPER shared::BoundaryRange& range(
        std::size_t index, std::size_t range_index
    ) noexcept
    {
        return rows_[index][range_index];
    }

private:
    std::vector<shared::BoundaryRange>* rows_;
};


class AllBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER AllBody(
        const std::vector<shared::StringView>& normalized,
        const shared::BoundaryOptions& options,
        bool return_length,
        std::vector<int>& missing,
        AllBoundaryRows& rows,
        std::vector<unsigned char>& fallback,
        std::vector<int>& failures
    ) noexcept
        : normalized_(normalized), options_(options),
          return_length_(return_length), missing_(missing), rows_(rows),
          fallback_(fallback), failures_(failures)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        shared::BoundaryIterator iterator;
        std::vector<shared::BoundaryRange> occurrences;
        bool opened = false;
        bool root_fallback = false;
        try {
            while (context.next_chunk()) {
                for (R_xlen_t task = context.begin;
                        task < context.end; ++task) {
                    const std::size_t index = static_cast<std::size_t>(task);
                    const shared::StringView& value = normalized_[index];
                    if (value.is_na()) {
                        missing_[index] = 1;
                        continue;
                    }
                    missing_[index] = 0;

                    ensure_iterator(
                        iterator, options_, opened, root_fallback
                    );
                    require_icu_success(iterator.set_text(value));
                    iterator.first();
                    occurrences.clear();
                    shared::Utf8PositionCursor cursor(value);
                    shared::BoundaryRange range{0, 0};
                    while (iterator.next(range)) {
                        const int start = cursor.at_byte(range.start)+1;
                        const int end = cursor.at_byte(range.end);
                        const shared::BoundaryRange occurrence{
                            start,
                            return_length_ ? end-start+1 : end
                        };
                        occurrences.push_back(occurrence);
                    }
                    rows_.copy_from(index, occurrences);
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
    }

private:
    const std::vector<shared::StringView>& normalized_;
    const shared::BoundaryOptions& options_;
    bool return_length_;
    std::vector<int>& missing_;
    AllBoundaryRows& rows_;
    std::vector<unsigned char>& fallback_;
    std::vector<int>& failures_;
};


CHARR_R_HELPER void emit_fallback_warning_r() noexcept
{
    Rf_warning(
        "%s", ICUError::getICUerrorName(U_USING_DEFAULT_WARNING)
    );
}

} // namespace search_boundaries_locate

using namespace search_boundaries_locate;


/**
 * Locate the first boundary.
 *
 * @param str character vector
 * @param opts_brkiter list
 * @param get_length logical
 * @return integer matrix (2 columns)
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-05)
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29)
 *     get_length
 */
CHARR_ENTRYPOINT SEXP ci_locate_first_boundaries(
    SEXP str, SEXP opts_brkiter, SEXP get_length
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool return_length = ci__prepare_arg_logical_1_notNA_r(
        get_length, "get_length"
    );
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    const shared::BoundaryOptions options =
        boundary::prepare_options_r(opts_brkiter, UBRK_LINE);


    bool root_fallback_warning = false;

    try {
        charport::Reader reader;
        charport::StrViews source_views;
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        std::vector<shared::StringView> normalized;
        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                result = entry_protections.reprotect_one(
                    Rf_allocMatrix(INTSXP, length, 2), result_index
                );
                int* output = INTEGER(result);

                reader.reset(str);
                if (reader.size() != length) {
                    throw std::runtime_error(
                        "Reader length changed during boundary location"
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
                const bool ascii_word_first =
                    shared::boundary_ascii_word_first(options);
                const bool needs_iterator = normalize_first_input(
                    source_views, converter, storage, ascii_word_first,
                    normalized
                );
                const shared::ParallelPlan plan = shared::parallel_plan(
                    true, length
                );
                FirstBody body(
                    normalized, options, return_length, ascii_word_first,
                    needs_iterator, length, output, root_fallback_warning
                );
                shared::run_parallel(plan, length, body);

                ci__locate_set_dimnames_matrix(result, return_length);
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        if (root_fallback_warning)
            emit_fallback_warning_r();
    );
}


/** Locate all BreakIterator boundaries
 *
 * @param str character vector
 * @param omit_no_match logical
 * @param opts_brkiter named list
 * @param get_length logical
 * @return list
 *
 * @version 0.2-2 (Marek Gagolewski, 2014-04-22)
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-28)
 *          new args: omit_no_match
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29)
 *     get_length
 */
CHARR_ENTRYPOINT SEXP ci_locate_all_boundaries(
    SEXP str, SEXP omit_no_match, SEXP opts_brkiter, SEXP get_length
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool omit = ci__prepare_arg_logical_1_notNA_r(
        omit_no_match, "omit_no_match"
    );
    const bool return_length = ci__prepare_arg_logical_1_notNA_r(
        get_length, "get_length"
    );
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
        std::vector<shared::BoundaryRange> occurrences;
        shared::BoundaryIterator iterator;
        std::vector<int> staged_missing;
        AllBoundaryRows staged_occurrences;
        std::vector<unsigned char> fallback;
        std::vector<int> failures;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                result = entry_protections.reprotect_one(
                    Rf_allocVector(VECSXP, length), result_index
                );
                SEXP current = R_NilValue;
                PROTECT_INDEX current_index;
                callback_protections.protect_with_index(current, &current_index);

                reader.reset(str);
                if (reader.size() != length) {
                    throw std::runtime_error(
                        "Reader length changed during boundary location"
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

                const shared::ParallelPlan parallel_plan =
                    shared::parallel_plan(true, length);
                if (parallel_plan.workers == 1) {
                for (R_len_t i = 0; i < length; ++i) {
                    const shared::StringView& value = normalized[
                        static_cast<std::size_t>(i)
                    ];
                    if (value.is_na()) {
                        current = callback_protections.reprotect_slot(
                            shared::filled_integer_matrix_r(1, 2), current_index
                        );
                        SET_VECTOR_ELT(result, i, current);
                        continue;
                    }

                    ensure_iterator(
                        iterator, options, iterator_open,
                        root_fallback_warning
                    );
                    require_icu_success(iterator.set_text(value));
                    iterator.first();

                    occurrences.clear();
                    shared::Utf8PositionCursor cursor(value);
                    shared::BoundaryRange range{0, 0};
                    while (iterator.next(range)) {
                        const int start = cursor.at_byte(range.start) + 1;
                        const int end = cursor.at_byte(range.end);
                        const shared::BoundaryRange occurrence{
                            start,
                            return_length ? end - start + 1 : end
                        };
                        occurrences.push_back(occurrence);
                    }

                    const int count = static_cast<int>(occurrences.size());
                    if (count == 0) {
                        current = callback_protections.reprotect_slot(
                            shared::filled_integer_matrix_r(
                                omit ? 0 : 1, 2,
                                return_length ? -1 : NA_INTEGER
                            ),
                            current_index
                        );
                        SET_VECTOR_ELT(result, i, current);
                        continue;
                    }

                    current = callback_protections.reprotect_slot(
                        Rf_allocMatrix(INTSXP, count, 2),
                        current_index
                    );
                    int* output = INTEGER(current);
                    for (int j = 0; j < count; ++j) {
                        const shared::BoundaryRange& occurrence =
                            occurrences[static_cast<std::size_t>(j)];
                        output[j] = occurrence.start;
                        output[j+count] = occurrence.end;
                    }
                    SET_VECTOR_ELT(result, i, current);
                }
                }
                else {
                    staged_missing.resize(
                        static_cast<std::size_t>(length)
                    );
                    staged_occurrences.reset(
                        static_cast<std::size_t>(length)
                    );
                    fallback.assign(parallel_plan.workers, 0);
                    failures.assign(parallel_plan.workers, 0);
                    AllBody body(
                        normalized, options, return_length,
                        staged_missing, staged_occurrences,
                        fallback, failures
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
                    }

                    for (R_len_t i = 0; i < length; ++i) {
                        const std::size_t index =
                            static_cast<std::size_t>(i);
                        if (staged_missing[index] != 0) {
                            current = callback_protections.reprotect_slot(
                                shared::filled_integer_matrix_r(1, 2),
                                current_index
                            );
                            SET_VECTOR_ELT(result, i, current);
                            continue;
                        }
                        const int count = static_cast<int>(
                            staged_occurrences.count(index)
                        );
                        if (count == 0) {
                            current = callback_protections.reprotect_slot(
                                shared::filled_integer_matrix_r(
                                    omit ? 0 : 1, 2,
                                    return_length ? -1 : NA_INTEGER
                                ),
                                current_index
                            );
                            SET_VECTOR_ELT(result, i, current);
                            continue;
                        }
                        current = callback_protections.reprotect_slot(
                            Rf_allocMatrix(INTSXP, count, 2),
                            current_index
                        );
                        int* output = INTEGER(current);
                        for (int j = 0; j < count; ++j) {
                            shared::BoundaryRange& occurrence =
                                staged_occurrences.range(
                                    index, static_cast<std::size_t>(j)
                                );
                            output[j] = occurrence.start;
                            output[j+count] = occurrence.end;
                        }
                        SET_VECTOR_ELT(result, i, current);
                    }
                }

                ci__locate_set_dimnames_list(result, return_length);
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
