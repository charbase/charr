
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
#include "collator/options.h"
#include "io/string_view.h"
#include "io/utf8_output.h"
#include "../shared/collation_search.h"
#include "../shared/collator.h"
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

namespace search_coll_split {

CHARR_NEUTRAL_HELPER R_len_t recycling_length(
    R_len_t subject_length,
    R_len_t pattern_length,
    R_len_t n_length,
    R_len_t omit_empty_length,
    bool& warning
) noexcept
{
    warning = false;
    if (subject_length <= 0 || pattern_length <= 0 ||
            n_length <= 0 || omit_empty_length <= 0) {
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


CHARR_CXX_HELPER void require_icu_success(UErrorCode status)
{
    if (U_FAILURE(status))
        throw StriException(status);
}


CHARR_CXX_HELPER void stage_utf16(
    const charport::StrViews& source,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    shared::CollationInputs& output
)
{
    output.resize(static_cast<std::size_t>(source.size()));
    for (R_xlen_t i = 0; i < source.size(); ++i) {
        const shared::StringView value = io::as_shared_view(source[i]);
        if (value.enc == shared::StringEncoding::bytes)
            throw StriException(MSG__BYTESENC);
        const shared::StringView normalized =
            shared::normalize_utf8_preserve_bom(
                value, converter, storage
            );
        output.set(
            static_cast<std::size_t>(i),
            normalized
        );
    }
}


CHARR_CXX_HELPER void normalize_views(
    charport::StrViews& source,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage
)
{
    for (R_xlen_t i = 0; i < source.size(); ++i) {
        const shared::StringView value = io::as_shared_view(source[i]);
        if (value.enc == shared::StringEncoding::bytes)
            throw StriException(MSG__BYTESENC);
        const shared::StringView normalized =
            shared::normalize_utf8_preserve_bom(
                value, converter, storage
            );
        const charport::StrView prepared = io::as_charport_view(normalized);
        source.ptrs()[i] = prepared.ptr;
        source.lengths()[i] = prepared.len;
        source.encodings()[i] = prepared.enc;
    }
}


CHARR_CXX_HELPER R_len_t count_empty_patterns(
    const shared::CollationInputs& patterns
)
{
    R_len_t result = 0;
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        const shared::CollationInput pattern = patterns.get(i);
        if (!pattern.missing && pattern.length <= 0)
            ++result;
    }
    return result;
}


CHARR_CXX_HELPER void set_scalar_missing(
    io::OutputStore& output
)
{
    output = io::scalar_store(io::missing_output_record());
}


CHARR_CXX_HELPER void set_scalar_empty(
    io::OutputStore& output
)
{
    output = io::scalar_store(
        "", 0, CETYPE_EXT_ASCII
    );
}


CHARR_CXX_HELPER io::OutputRecord field_record(
    const shared::StringView& source,
    const shared::CollationInput& subject,
    const shared::CollationRange& field,
    std::vector<char>& utf8_buffer
)
{
    if (source.enc == shared::StringEncoding::ascii) {
        const int length = field.end-field.start;
        return io::OutputRecord{
            length == 0 ? "" : source.ptr+field.start,
            length, CETYPE_EXT_ASCII
        };
    }

    UErrorCode status = U_ZERO_ERROR;
    const shared::CollationUtf8Slice value =
        shared::collation_utf8_slice(
            subject, field, utf8_buffer, status
        );
    require_icu_success(status);
    return io::OutputRecord{
        value.data, value.length,
        value.ascii ? CETYPE_EXT_ASCII : CETYPE_EXT_UTF8
    };
}


CHARR_CXX_HELPER void build_store(
    const shared::StringView& source,
    const shared::CollationInput& subject,
    const std::vector<shared::CollationRange>& fields,
    bool empty_is_missing,
    std::vector<char>& utf8_buffer,
    io::OutputBuilder& builder,
    io::OutputStore& output
)
{
    const R_xlen_t size = static_cast<R_xlen_t>(fields.size());
    if (size <= 0)
        return;

    if (size == 1) {
        const shared::CollationRange field = fields[0];
        if (empty_is_missing && field.start == field.end) {
            set_scalar_missing(output);
        }
        else {
            output = io::scalar_store(
                field_record(source, subject, field, utf8_buffer)
            );
        }
        return;
    }

    builder.reset(size);
    for (R_xlen_t i = 0; i < size; ++i) {
        const shared::CollationRange field = fields[
            static_cast<std::size_t>(i)
        ];
        if (empty_is_missing && field.start == field.end) {
            builder.set_na(i);
        }
        else {
            builder.set_validated(
                i, field_record(source, subject, field, utf8_buffer)
            );
        }
    }
    output = builder.release_store();
}


CHARR_NEUTRAL_HELPER R_len_t requested_columns(
    const int* n, R_len_t size
) noexcept
{
    R_len_t result = 0;
    for (R_len_t i = 0; i < size; ++i) {
        if (n[i] != NA_INTEGER && n[i] > result)
            result = n[i];
    }
    return result;
}


CHARR_R_HELPER void emit_warnings(
    bool root_fallback_warning,
    bool recycling_warning,
    R_len_t empty_pattern_warnings
) noexcept
{
    if (root_fallback_warning) {
        Rf_warning(
            "%s", ICUError::getICUerrorName(U_USING_DEFAULT_WARNING)
        );
    }
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);
    for (R_len_t i = 0; i < empty_pattern_warnings; ++i)
        Rf_warning(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
}

} // namespace search_coll_split

using namespace search_coll_split;


/** Split strings around collation-aware pattern matches. */
CHARR_ENTRYPOINT SEXP ci_split_coll(
    SEXP str,
    SEXP pattern,
    SEXP n,
    SEXP omit_empty,
    SEXP tokens_only,
    SEXP simplify,
    SEXP opts_collator
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool tokens_only_value =
        ci__prepare_arg_logical_1_notNA_r(
            tokens_only, "tokens_only"
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
    simplify = entry_protections.protect_one(
        ci__prepare_arg_logical_1_r(
            simplify, "simplify"
        )
    );
    const int simplify_value = LOGICAL_RO(simplify)[0];
    const shared::CollatorOptions options =
        collator::prepare_options(opts_collator);

    SEXP temporary = R_NilValue;
    PROTECT_INDEX temporary_index;

    bool root_fallback_warning = false;
    bool recycling_warning = false;
    R_len_t empty_pattern_warnings = 0;
    R_len_t vectorize_length = 0;
    R_len_t max_columns = 0;

    try {
        shared::Collator collator_owner;
        charport::Reader subject_reader;
        charport::Reader pattern_reader;
        charport::StrViews subject_views;
        charport::StrViews pattern_views;
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        shared::CollationInputs patterns;
        shared::CollationCursor subject_cursor;
        shared::CollationMatcher matcher;
        std::vector<shared::CollationRange> fields;
        std::vector<char> utf8_buffer;
        std::vector<io::OutputStore> stores;
        io::OutputBuilder child_builder(0);
        io::OutputBuilder matrix_builder(0);

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

                bool pending_recycling_warning = false;
                vectorize_length = recycling_length(
                    subject_length, pattern_length,
                    n_length, omit_empty_length,
                    pending_recycling_warning
                );

                const shared::CollatorOpenResult opened =
                    collator_owner.reset(options);
                root_fallback_warning = opened.root_fallback;
                require_icu_success(opened.status);
                recycling_warning = pending_recycling_warning;

                if (vectorize_length > 0) {
                    subject_reader.reset(str);
                    if (subject_reader.size() != subject_length) {
                        throw std::runtime_error(
                            "Reader length changed during collation splitting"
                        );
                    }
                    subject_views.resize(subject_length);
                    subject_reader.views(
                        0, subject_length,
                        subject_views.ptrs(), subject_views.lengths(),
                        subject_views.encodings()
                    );
                    normalize_views(
                        subject_views, subject_converter, subject_storage
                    );

                    pattern_reader.reset(pattern);
                    if (pattern_reader.size() != pattern_length) {
                        throw std::runtime_error(
                            "Reader length changed during collation splitting"
                        );
                    }
                    pattern_views.resize(pattern_length);
                    pattern_reader.views(
                        0, pattern_length,
                        pattern_views.ptrs(), pattern_views.lengths(),
                        pattern_views.encodings()
                    );
                    stage_utf16(
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
                if (simplify_value == NA_LOGICAL || simplify_value) {
                    max_columns = requested_columns(
                        n_values, n_length
                    );
                }

                for (R_len_t lane = 0;
                        lane < (vectorize_length > 0 ? pattern_length : 0);
                        ++lane) {
                    const shared::CollationInput prepared_pattern =
                        patterns.get(static_cast<std::size_t>(lane));
                    R_len_t i = lane;
                    for (;;) {
                        io::OutputStore& output = stores[
                            static_cast<std::size_t>(i)
                        ];
                        const int n_current = n_values[i % n_length];
                        const int omit_current =
                            omit_empty_values[i % omit_empty_length];
                        const bool omit_missing =
                            omit_current == NA_LOGICAL;
                        const bool omit =
                            !omit_missing && omit_current != 0;

                        if (n_current == NA_INTEGER) {
                            set_scalar_missing(output);
                        }
                        else {
                            const R_len_t subject_index =
                                i % subject_length;
                            const shared::StringView source =
                                io::as_shared_view(
                                    subject_views[subject_index]
                                );
                            if (source.is_na() ||
                                    prepared_pattern.missing ||
                                    prepared_pattern.length <= 0) {
                                set_scalar_missing(output);
                            }
                            else {
                                const shared::CollationInput subject =
                                    subject_cursor.get(
                                        static_cast<const void*>(
                                            subject_views.ptrs() +
                                                subject_index
                                        ),
                                        source
                                    );
                                if (subject.length <= 0) {
                                    if (omit_missing) {
                                        set_scalar_missing(output);
                                    }
                                    else if (!(omit || n_current == 0)) {
                                        set_scalar_empty(output);
                                    }
                                }
                                else {
                                    UErrorCode status = U_ZERO_ERROR;
                                    const shared::CollationSplitResult split =
                                        matcher.split(
                                            collator_owner.get(),
                                            subject, prepared_pattern,
                                            n_current, omit,
                                            tokens_only_value,
                                            fields, status
                                        );
                                    require_icu_success(status);
                                    if (split ==
                                            shared::CollationSplitResult::
                                                limit_too_large) {
                                        throw StriException(
                                            MSG__INCORRECT_NAMED_ARG "; "
                                            MSG__EXPECTED_SMALLER,
                                            "n"
                                        );
                                    }
                                    build_store(
                                        source, subject, fields, omit_missing,
                                        utf8_buffer, child_builder, output
                                    );
                                }
                            }
                        }

                        if (simplify_value == NA_LOGICAL || simplify_value) {
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
                        for (; j < current_size; ++j)
                            matrix_builder.set_validated(
                                i+j*rows, current.view(j)
                            );
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
        emit_warnings(
            root_fallback_warning,
            recycling_warning,
            empty_pattern_warnings
        );
    );
}

} } // namespace charr::altrep_backend
