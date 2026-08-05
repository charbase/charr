// Derived from stringi.
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
#include "fixed/options.h"
#include "io/string_view.h"
#include "io/utf8_output.h"
#include "../shared/entrypoint.h"
#include "../shared/fixed_search.h"
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

namespace search_fixed_split {

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


CHARR_NEUTRAL_HELPER R_len_t requested_columns(
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


CHARR_CXX_HELPER void normalize_views(
    const charport::StrViews& source,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    std::vector<shared::StringView>& output
)
{
    output.clear();
    output.reserve(static_cast<std::size_t>(source.size()));
    for (R_xlen_t i = 0; i < source.size(); ++i) {
        const shared::StringView value = io::as_shared_view(source[i]);
        if (value.enc == shared::StringEncoding::bytes)
            throw StriException(MSG__BYTESENC);
        output.push_back(shared::normalize_utf8(
            value, converter, storage
        ));
    }
}


CHARR_CXX_HELPER R_len_t count_empty_patterns(
    const std::vector<shared::StringView>& patterns
)
{
    R_len_t result = 0;
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        const shared::StringView& pattern = patterns[i];
        if (!pattern.is_na() && pattern.len <= 0)
            ++result;
    }
    return result;
}


CHARR_NEUTRAL_HELPER CHARR_ALWAYS_INLINE cetype_ext_t field_encoding(
    const shared::StringView& subject
) noexcept
{
    return subject.enc == shared::StringEncoding::ascii
        ? CETYPE_EXT_ASCII
        : CETYPE_EXT_ASCII_OR_UTF8;
}


CHARR_CXX_HELPER void set_scalar_missing(io::OutputStore& output)
{
    output = io::OutputStore::scalar(
        nullptr, 0, CETYPE_EXT_NA
    );
}


CHARR_CXX_HELPER void set_scalar_empty(io::OutputStore& output)
{
    output = io::OutputStore::scalar(
        "", 0, CETYPE_EXT_ASCII
    );
}


CHARR_CXX_HELPER CHARR_ALWAYS_INLINE void build_store(
    const shared::StringView& subject,
    const std::vector<shared::FixedRange>& fields,
    bool empty_is_missing,
    io::OutputBuilder& builder,
    io::OutputStore& output
)
{
    const R_xlen_t size = static_cast<R_xlen_t>(fields.size());
    if (size <= 0)
        return;

    const cetype_ext_t encoding = field_encoding(subject);
    if (size == 1) {
        const shared::FixedRange& field = fields[0];
        if (empty_is_missing && field.start == field.end) {
            set_scalar_missing(output);
        }
        else {
            const int length = field.end-field.start;
            output = io::OutputStore::scalar(
                length == 0 ? "" : subject.ptr+field.start,
                static_cast<std::size_t>(length), encoding
            );
        }
        return;
    }

    builder.reset(size);
    for (R_xlen_t i = 0; i < size; ++i) {
        const shared::FixedRange& field = fields[
            static_cast<std::size_t>(i)
        ];
        if (empty_is_missing && field.start == field.end) {
            builder.set_na(i);
        }
        else {
            const int length = field.end-field.start;
            builder.set_validated(i, make_strview(
                length == 0 ? "" : subject.ptr+field.start,
                length, encoding
            ));
        }
    }
    output = builder.release_store();
}


class Body final : public ParallelBody {
public:
    CHARR_CXX_HELPER Body(
        const std::vector<shared::StringView>& subjects,
        const std::vector<shared::StringView>& patterns,
        const int* n_values, const int* omit_empty_values,
        R_len_t subject_length, R_len_t pattern_length,
        R_len_t n_length, R_len_t omit_empty_length,
        R_len_t vectorize_length,
        shared::FixedSearchOptions options,
        bool tokens_only, bool simplifying,
        std::vector<io::OutputStore>& stores,
        std::vector<R_len_t>& max_columns
    ) noexcept
        : subjects_(subjects), patterns_(patterns), n_values_(n_values),
          omit_empty_values_(omit_empty_values),
          subject_length_(subject_length), pattern_length_(pattern_length),
          n_length_(n_length), omit_empty_length_(omit_empty_length),
          vectorize_length_(vectorize_length), options_(options),
          tokens_only_(tokens_only), simplifying_(simplifying),
          stores_(stores), max_columns_(max_columns)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        shared::FixedMatcher matcher;
        std::vector<shared::FixedRange> fields;
        io::OutputBuilder child_builder(0);
        fields.reserve(16);
        // Runs over every chunk this worker draws, so the slot it reports
        // still holds the widest row the worker saw.
        R_len_t local_max_columns = 0;

        if (pattern_length_ == 1) {
            const shared::StringView prepared_pattern = patterns_[0];
            while (context.next_chunk()) {
                const R_len_t begin = static_cast<R_len_t>(context.begin);
                const R_len_t end = static_cast<R_len_t>(context.end);
                for (R_len_t i = begin; i < end; ++i) {
                    split_one(
                        i, prepared_pattern, matcher, fields, child_builder,
                        local_max_columns
                    );
                }
            }
        }
        else {
            while (context.next_chunk()) {
                const R_len_t begin = static_cast<R_len_t>(context.begin);
                const R_len_t end = static_cast<R_len_t>(context.end);
                for (R_len_t lane = begin; lane < end; ++lane) {
                    const shared::StringView prepared_pattern = patterns_[
                        static_cast<std::size_t>(lane)
                    ];
                    R_len_t i = lane;
                    for (;;) {
                        split_one(
                            i, prepared_pattern, matcher, fields,
                            child_builder, local_max_columns
                        );
                        if (pattern_length_ >= vectorize_length_-i)
                            break;
                        i += pattern_length_;
                    }
                }
            }
        }

        if (simplifying_)
            max_columns_[context.worker] = local_max_columns;
    }

private:
    CHARR_CXX_HELPER void split_one(
        R_len_t i,
        const shared::StringView& prepared_pattern,
        shared::FixedMatcher& matcher,
        std::vector<shared::FixedRange>& fields,
        io::OutputBuilder& child_builder,
        R_len_t& max_columns
    )
    {
        io::OutputStore& output = stores_[static_cast<std::size_t>(i)];
        const int raw_n = n_values_[i % n_length_];
        const int raw_omit = omit_empty_values_[i % omit_empty_length_];
        const bool omit_missing = raw_omit == NA_LOGICAL;
        const bool omit = !omit_missing && raw_omit != 0;
        const shared::StringView subject = subjects_[
            static_cast<std::size_t>(i % subject_length_)
        ];

        if (raw_n == NA_INTEGER || subject.is_na() ||
                prepared_pattern.is_na() || prepared_pattern.len <= 0) {
            set_scalar_missing(output);
        }
        else if (subject.len <= 0) {
            if (omit_missing) {
                set_scalar_missing(output);
            }
            else if (!(omit || raw_n == 0)) {
                set_scalar_empty(output);
            }
        }
        else {
            const shared::FixedSplitResult split =
                shared::split_fixed_fields(
                    matcher,
                    subject, prepared_pattern, options_,
                    raw_n, omit, tokens_only_, fields
                );
            if (split == shared::FixedSplitResult::limit_too_large) {
                throw StriException(
                    MSG__INCORRECT_NAMED_ARG "; "
                    MSG__EXPECTED_SMALLER,
                    "n"
                );
            }
            build_store(
                subject, fields, omit_missing, child_builder, output
            );
        }

        if (simplifying_) {
            const R_len_t current_size =
                static_cast<R_len_t>(output.size());
            if (max_columns < current_size)
                max_columns = current_size;
        }
    }

    const std::vector<shared::StringView>& subjects_;
    const std::vector<shared::StringView>& patterns_;
    const int* n_values_;
    const int* omit_empty_values_;
    R_len_t subject_length_;
    R_len_t pattern_length_;
    R_len_t n_length_;
    R_len_t omit_empty_length_;
    R_len_t vectorize_length_;
    shared::FixedSearchOptions options_;
    bool tokens_only_;
    bool simplifying_;
    std::vector<io::OutputStore>& stores_;
    std::vector<R_len_t>& max_columns_;
};


CHARR_R_HELPER void emit_warnings_r(
    bool recycling_warning,
    R_len_t empty_pattern_warnings
) noexcept
{
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);
    for (R_len_t i = 0; i < empty_pattern_warnings; ++i)
        Rf_warning(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
}

} // namespace search_fixed_split

using namespace search_fixed_split;


/** Split strings around fixed byte-pattern matches. */
CHARR_ENTRYPOINT SEXP ci_split_fixed(
    SEXP str,
    SEXP pattern,
    SEXP n,
    SEXP omit_empty,
    SEXP tokens_only,
    SEXP simplify,
    SEXP opts_fixed
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const shared::FixedSearchOptions options =
        fixed::prepare_options(opts_fixed);
    const bool tokens_only_value =
        ci__prepare_arg_logical_1_notNA_r(
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
    pattern = entry_protections.protect_one(
        ci__prepare_arg_string_r(pattern, "pattern")
    );
    n = entry_protections.protect_one(
        ci__prepare_arg_integer_r(n, "n")
    );
    omit_empty = entry_protections.protect_one(
        ci__prepare_arg_logical_r(
            omit_empty, "omit_empty"
        )
    );
    const int simplify_value = LOGICAL_RO(simplify)[0];
    const bool simplifying =
        simplify_value == NA_LOGICAL || simplify_value != 0;

    SEXP temporary = R_NilValue;
    PROTECT_INDEX temporary_index;

    bool recycling_warning = false;
    R_len_t empty_pattern_warnings = 0;
    R_len_t vectorize_length = 0;
    R_len_t max_columns = 0;

    try {
        charport::Reader subject_reader;
        charport::Reader pattern_reader;
        charport::StrViews subject_views;
        charport::StrViews pattern_views;
        std::vector<shared::StringView> subjects;
        std::vector<shared::StringView> patterns;
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        shared::FixedMatcher matcher;
        std::vector<shared::FixedRange> fields;
        std::vector<io::OutputStore> stores;
        io::OutputBuilder child_builder(0);
        io::OutputBuilder matrix_builder(0);
        std::vector<R_len_t> worker_max_columns;

        fields.reserve(16);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t subject_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                const R_len_t pattern_length = io::checked_r_len(
                    XLENGTH(pattern), "character vectors"
                );
                const R_len_t n_length = io::checked_r_len(
                    XLENGTH(n), "integer vectors"
                );
                const R_len_t omit_empty_length = io::checked_r_len(
                    XLENGTH(omit_empty), "logical vectors"
                );

                vectorize_length = recycling_length(
                    subject_length, pattern_length,
                    n_length, omit_empty_length,
                    recycling_warning
                );

                if (vectorize_length > 0) {
                    subject_reader.reset(str);
                    if (subject_reader.size() != subject_length) {
                        throw std::runtime_error(
                            "Reader length changed during fixed splitting"
                        );
                    }
                    subject_views.resize(subject_length);
                    subject_reader.views(
                        0, subject_length,
                        subject_views.ptrs(), subject_views.lengths(),
                        subject_views.encodings()
                    );
                    normalize_views(
                        subject_views, subject_converter, subject_storage,
                        subjects
                    );

                    pattern_reader.reset(pattern);
                    if (pattern_reader.size() != pattern_length) {
                        throw std::runtime_error(
                            "Reader length changed during fixed splitting"
                        );
                    }
                    pattern_views.resize(pattern_length);
                    pattern_reader.views(
                        0, pattern_length,
                        pattern_views.ptrs(), pattern_views.lengths(),
                        pattern_views.encodings()
                    );
                    normalize_views(
                        pattern_views, pattern_converter, pattern_storage,
                        patterns
                    );
                    empty_pattern_warnings =
                        count_empty_patterns(patterns);
                }

                stores.reserve(
                    static_cast<std::size_t>(vectorize_length)
                );
                for (R_len_t i = 0; i < vectorize_length; ++i)
                    stores.emplace_back(0, 0);

                const int* n_values = INTEGER_RO(n);
                const int* omit_empty_values = LOGICAL_RO(omit_empty);
                if (simplifying) {
                    max_columns = requested_columns(
                        n_values, n_length
                    );
                }

                const R_len_t tasks = vectorize_length == 0
                    ? 0 : pattern_length == 1
                        ? vectorize_length : pattern_length;
                const shared::ParallelPlan plan = shared::parallel_plan(
                    true, tasks
                );
                if (plan.workers == 1) {
                    for (R_len_t lane = 0;
                            lane < (vectorize_length > 0
                                ? pattern_length : 0);
                            ++lane) {
                        const shared::StringView prepared_pattern =
                            patterns[static_cast<std::size_t>(lane)];
                        R_len_t i = lane;
                        for (;;) {
                            io::OutputStore& output = stores[
                                static_cast<std::size_t>(i)
                            ];
                            const int raw_n = n_values[i % n_length];
                            const int raw_omit = omit_empty_values[
                                i % omit_empty_length
                            ];
                            const bool omit_missing =
                                raw_omit == NA_LOGICAL;
                            const bool omit =
                                !omit_missing && raw_omit != 0;
                            const shared::StringView subject = subjects[
                                static_cast<std::size_t>(i % subject_length)
                            ];

                            if (raw_n == NA_INTEGER || subject.is_na() ||
                                    prepared_pattern.is_na() ||
                                    prepared_pattern.len <= 0) {
                                set_scalar_missing(output);
                            }
                            else if (subject.len <= 0) {
                                if (omit_missing) {
                                    set_scalar_missing(output);
                                }
                                else if (!(omit || raw_n == 0)) {
                                    set_scalar_empty(output);
                                }
                            }
                            else {
                                const shared::FixedSplitResult split =
                                    shared::split_fixed_fields(
                                        matcher,
                                        subject, prepared_pattern, options,
                                        raw_n, omit, tokens_only_value, fields
                                    );
                                if (split == shared::FixedSplitResult::
                                        limit_too_large) {
                                    throw StriException(
                                        MSG__INCORRECT_NAMED_ARG "; "
                                        MSG__EXPECTED_SMALLER,
                                        "n"
                                    );
                                }
                                build_store(
                                    subject, fields, omit_missing,
                                    child_builder, output
                                );
                            }

                            if (simplifying) {
                                const R_len_t current_size =
                                    static_cast<R_len_t>(output.size());
                                if (max_columns < current_size)
                                    max_columns = current_size;
                            }

                            if (pattern_length >= vectorize_length-i)
                                break;
                            i += pattern_length;
                        }
                    }
                }
                else {
                    worker_max_columns.assign(
                        static_cast<std::size_t>(plan.workers), 0
                    );
                    Body body(
                        subjects, patterns, n_values, omit_empty_values,
                        subject_length, pattern_length,
                        n_length, omit_empty_length, vectorize_length,
                        options, tokens_only_value, simplifying,
                        stores, worker_max_columns
                    );
                    shared::run_parallel(plan, tasks, body);
                    if (simplifying) {
                        for (std::size_t i = 0;
                                i < worker_max_columns.size(); ++i) {
                            if (max_columns < worker_max_columns[i])
                                max_columns = worker_max_columns[i];
                        }
                    }
                }

                callback_protections.protect_with_index(
                    temporary, &temporary_index
                );
                if (!simplifying) {
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
                            matrix_builder.set_validated(
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
                        Rf_setAttrib(
                            result, R_DimSymbol, temporary
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
            recycling_warning, empty_pattern_warnings
        );
    );
}

} } // namespace charr::altrep_backend
