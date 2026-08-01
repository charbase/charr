// Derived from stringi 19e9586ba39b3320df49355e32bd18d74ed6098f.
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
#include "regex/options_r.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/regex_search.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <charport.h>

#include <climits>
#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>


namespace charr { namespace altrep_backend {

namespace search_regex_split {

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


CHARR_CXX_HELPER void normalize_subjects(
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


CHARR_CXX_HELPER void normalize_patterns(
    const charport::StrViews& source,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    shared::RegexPatterns& output
)
{
    output.resize(static_cast<std::size_t>(source.size()));
    for (R_xlen_t i = 0; i < source.size(); ++i) {
        const shared::StringView value = io::as_shared_view(source[i]);
        if (value.enc == shared::StringEncoding::bytes)
            throw StriException(MSG__BYTESENC);
        output.set(
            static_cast<std::size_t>(i),
            shared::normalize_utf8_preserve_bom(
                value, converter, storage
            )
        );
    }
}


CHARR_CXX_HELPER [[noreturn]] void throw_regex_error(
    UErrorCode status,
    bool pattern_compile_error,
    const shared::RegexPatterns& patterns,
    std::size_t pattern_index
)
{
    if (pattern_compile_error) {
        std::string context;
        patterns.context(pattern_index, context);
        throw StriException(status, context.c_str());
    }
    throw StriException(status);
}


CHARR_CXX_HELPER void bind_pattern(
    shared::RegexMatcher& matcher,
    const shared::RegexInput& pattern,
    const shared::RegexPatterns& patterns,
    std::size_t pattern_index
)
{
    UErrorCode status = U_ZERO_ERROR;
    bool pattern_compile_error = false;
    if (!matcher.bind(pattern, status, pattern_compile_error) ||
            U_FAILURE(status)) {
        throw_regex_error(
            status, pattern_compile_error, patterns, pattern_index
        );
    }
}


CHARR_NEUTRAL_HELPER CHARR_ALWAYS_INLINE cetype_ext_t field_encoding(
    bool ascii
) noexcept
{
    return ascii
        ? cetype_ext_t::CE_ASCII
        : cetype_ext_t::CE_ASCII_OR_UTF8;
}


CHARR_CXX_HELPER void set_scalar_missing(io::OutputStore& output)
{
    output = io::OutputStore::scalar(
        nullptr, 0, cetype_ext_t::CE_NA
    );
}


CHARR_CXX_HELPER void set_scalar_empty(io::OutputStore& output)
{
    output = io::OutputStore::scalar(
        "", 0, cetype_ext_t::CE_ASCII
    );
}


CHARR_CXX_HELPER CHARR_ALWAYS_INLINE void build_store(
    const shared::StringView& subject,
    const std::vector<shared::RegexRange>& fields,
    bool empty_is_missing,
    bool ascii,
    io::OutputBuilder& builder,
    io::OutputStore& output
)
{
    const R_xlen_t size = static_cast<R_xlen_t>(fields.size());
    if (size <= 0)
        return;

    const cetype_ext_t encoding = field_encoding(ascii);
    if (size == 1) {
        const shared::RegexRange& field = fields[0];
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
        const shared::RegexRange& field = fields[
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


CHARR_R_HELPER void emit_empty_pattern_warnings_r(int count) noexcept
{
    for (int i = 0; i < count; ++i)
        Rf_warning(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
}

} // namespace search_regex_split

using namespace search_regex_split;


/** Split strings around regular-expression matches. */
CHARR_ENTRYPOINT SEXP ci_split_regex(
    SEXP str,
    SEXP pattern,
    SEXP n,
    SEXP omit_empty,
    SEXP tokens_only,
    SEXP simplify,
    SEXP opts_regex
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
    const bool simplifying =
        simplify_value == NA_LOGICAL || simplify_value != 0;
    const R_xlen_t subject_xlength = XLENGTH(str);
    const R_xlen_t pattern_xlength = XLENGTH(pattern);
    const R_xlen_t n_xlength = XLENGTH(n);
    const R_xlen_t omit_empty_xlength = XLENGTH(omit_empty);
    if (subject_xlength > R_LEN_T_MAX ||
            pattern_xlength > R_LEN_T_MAX ||
            n_xlength > R_LEN_T_MAX ||
            omit_empty_xlength > R_LEN_T_MAX) {
        Rf_error("long vectors are not supported by regex splitting");
    }

    const R_len_t subject_length = static_cast<R_len_t>(subject_xlength);
    const R_len_t pattern_length = static_cast<R_len_t>(pattern_xlength);
    const R_len_t n_length = static_cast<R_len_t>(n_xlength);
    const R_len_t omit_empty_length =
        static_cast<R_len_t>(omit_empty_xlength);
    bool recycling_warning = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, n_length,
        omit_empty_length, recycling_warning
    );
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);

    const bool scalar_default =
        vectorize_length > 0 &&
        pattern_length == 1 &&
        n_length == 1 &&
        omit_empty_length == 1 &&
        INTEGER_RO(n)[0] != NA_INTEGER &&
        INTEGER_RO(n)[0] < 0 &&
        LOGICAL_RO(omit_empty)[0] == FALSE &&
        !tokens_only_value &&
        simplify_value == FALSE;

    const shared::RegexOptions options =
        regex::prepare_options(opts_regex);

    SEXP temporary = R_NilValue;
    PROTECT_INDEX temporary_index;

    int empty_pattern_warnings = 0;
    R_len_t max_columns = 0;

    try {
        charport::Reader subject_reader;
        charport::Reader pattern_reader;
        charport::StrViews subject_views;
        charport::StrViews pattern_views;
        std::vector<shared::StringView> subjects;
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        shared::RegexPatterns patterns;
        shared::RegexMatcher matcher(options);
        std::vector<shared::RegexRange> fields;
        std::vector<io::OutputStore> stores;
        io::OutputBuilder child_builder(0);
        io::OutputBuilder matrix_builder(0);

        fields.reserve(16);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                if (vectorize_length > 0) {
                    subject_reader.reset(str);
                    if (subject_reader.size() != subject_length) {
                        throw std::runtime_error(
                            "Reader length changed during regex splitting"
                        );
                    }
                    subject_views.resize(subject_length);
                    subject_reader.views(
                        0, subject_length,
                        subject_views.ptrs(), subject_views.lengths(),
                        subject_views.encodings()
                    );
                    normalize_subjects(
                        subject_views, subject_converter, subject_storage,
                        subjects
                    );

                    pattern_reader.reset(pattern);
                    if (pattern_reader.size() != pattern_length) {
                        throw std::runtime_error(
                            "Reader length changed during regex splitting"
                        );
                    }
                    pattern_views.resize(pattern_length);
                    pattern_reader.views(
                        0, pattern_length,
                        pattern_views.ptrs(), pattern_views.lengths(),
                        pattern_views.encodings()
                    );
                    normalize_patterns(
                        pattern_views, pattern_converter, pattern_storage,
                        patterns
                    );
                    empty_pattern_warnings = patterns.empty_count();
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

                if (scalar_default) {
                    const std::size_t pattern_index = 0;
                    const shared::RegexInput prepared_pattern =
                        patterns.get(0);
                    const bool pattern_unusable =
                        prepared_pattern.missing ||
                        prepared_pattern.length <= 0;
                    bool matcher_bound = false;
                    for (R_len_t i = 0; i < vectorize_length; ++i) {
                        io::OutputStore& output = stores[
                            static_cast<std::size_t>(i)
                        ];
                        const std::size_t subject_index =
                            static_cast<std::size_t>(i);
                        const shared::StringView subject =
                            subjects[subject_index];

                        if (subject.is_na() || pattern_unusable) {
                            set_scalar_missing(output);
                        }
                        else if (subject.len <= 0) {
                            set_scalar_empty(output);
                        }
                        else {
                            if (!matcher_bound) {
                                bind_pattern(
                                    matcher, prepared_pattern, patterns,
                                    pattern_index
                                );
                                matcher_bound = true;
                            }

                            UErrorCode status = U_ZERO_ERROR;
                            matcher.split_default(
                                subject, &subjects[subject_index],
                                fields, status
                            );
                            if (U_FAILURE(status))
                                throw StriException(status);

                            build_store(
                                subject, fields, false,
                                matcher.subject_is_ascii(),
                                child_builder, output
                            );
                        }
                    }
                }
                else {
                    for (R_len_t lane = 0;
                            lane < (vectorize_length > 0
                                ? pattern_length : 0);
                            ++lane) {
                        const std::size_t pattern_index =
                            static_cast<std::size_t>(lane);
                        const shared::RegexInput prepared_pattern =
                            patterns.get(pattern_index);
                        const bool pattern_unusable =
                            prepared_pattern.missing ||
                            prepared_pattern.length <= 0;
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
                            const std::size_t subject_index =
                                static_cast<std::size_t>(
                                    i % subject_length
                                );
                            const shared::StringView subject =
                                subjects[subject_index];

                            if (raw_n == NA_INTEGER || subject.is_na() ||
                                    pattern_unusable) {
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
                            else if (raw_n == 0) {
                                // The slot was preinitialized as an empty Store.
                            }
                            else {
                                if (raw_n >= INT_MAX-1) {
                                    throw StriException(
                                        MSG__INCORRECT_NAMED_ARG "; "
                                        MSG__EXPECTED_SMALLER,
                                        "n"
                                    );
                                }

                                bind_pattern(
                                    matcher, prepared_pattern, patterns,
                                    pattern_index
                                );
                                UErrorCode status = U_ZERO_ERROR;
                                const shared::RegexSplitResult split =
                                    matcher.split(
                                        subject, &subjects[subject_index],
                                        raw_n, omit, tokens_only_value,
                                        fields, status
                                    );
                                if (split == shared::RegexSplitResult::
                                        limit_too_large) {
                                    throw StriException(
                                        MSG__INCORRECT_NAMED_ARG "; "
                                        MSG__EXPECTED_SMALLER,
                                        "n"
                                    );
                                }
                                if (U_FAILURE(status))
                                    throw StriException(status);

                                build_store(
                                    subject, fields, omit_missing,
                                    matcher.subject_is_ascii(),
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
                                    cetype_ext_t::CE_ASCII
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
        emit_empty_pattern_warnings_r(empty_pattern_warnings);
    );
}

} } // namespace charr::altrep_backend
