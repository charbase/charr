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
#include "io/string_view.h"
#include "regex/options_r.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/regex_search.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <cstddef>
#include <exception>
#include <string>
#include <vector>


namespace charr { namespace base_backend {

namespace search_regex_extract {

CHARR_NEUTRAL_HELPER R_len_t recycling_length(
    R_len_t subject_length,
    R_len_t pattern_length,
    bool& warning
) noexcept
{
    warning = false;
    if (subject_length <= 0 || pattern_length <= 0)
        return 0;

    const R_len_t result = subject_length > pattern_length
        ? subject_length
        : pattern_length;
    warning = result % subject_length != 0 ||
        result % pattern_length != 0;
    return result;
}


CHARR_CXX_HELPER shared::StringView normalize_subject(
    const shared::StringView& source,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage
)
{
    if (source.enc == shared::StringEncoding::bytes)
        throw StriException(MSG__BYTESENC);
    return shared::normalize_utf8(source, converter, storage);
}


CHARR_CXX_HELPER shared::StringView normalize_pattern(
    const shared::StringView& source,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage
)
{
    if (source.enc == shared::StringEncoding::bytes)
        throw StriException(MSG__BYTESENC);
    return shared::normalize_utf8_preserve_bom(
        source, converter, storage
    );
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


CHARR_R_HELPER SEXP missing_strings_r(R_len_t size) noexcept
{
    SEXP result = Rf_allocVector(STRSXP, size);
    for (R_len_t i = 0; i < size; ++i)
        SET_STRING_ELT(result, i, NA_STRING);
    return result;
}


CHARR_R_HELPER SEXP strings_r(R_len_t size) noexcept
{
    return Rf_allocVector(STRSXP, size);
}


CHARR_R_HELPER CHARR_ALWAYS_INLINE void set_match_r(
    SEXP output,
    R_len_t index,
    const shared::StringView& subject,
    const shared::RegexRange& match
) noexcept
{
    const int length = match.end-match.start;
    SET_STRING_ELT(
        output, index,
        Rf_mkCharLenCE(
            length == 0 ? "" : subject.ptr+match.start,
            length, CE_UTF8
        )
    );
}


CHARR_R_HELPER SEXP simplify_result_r(
    SEXP input, R_len_t rows, R_len_t columns, bool pad_na
) noexcept
{
    SEXP output = Rf_allocMatrix(STRSXP, rows, columns);
    const SEXP fill = pad_na ? NA_STRING : R_BlankString;
    for (R_len_t i = 0; i < rows; ++i) {
        const SEXP current = VECTOR_ELT(input, i);
        const R_len_t current_size = LENGTH(current);
        R_len_t j = 0;
        for (; j < current_size; ++j) {
            SET_STRING_ELT(
                output,
                static_cast<R_xlen_t>(i) +
                    static_cast<R_xlen_t>(j)*rows,
                STRING_ELT(current, j)
            );
        }
        for (; j < columns; ++j) {
            SET_STRING_ELT(
                output,
                static_cast<R_xlen_t>(i) +
                    static_cast<R_xlen_t>(j)*rows,
                fill
            );
        }
    }
    return output;
}


CHARR_R_HELPER void emit_empty_pattern_warnings_r(int count) noexcept
{
    for (int i = 0; i < count; ++i)
        Rf_warning(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
}

} // namespace search_regex_extract

using namespace search_regex_extract;


/** Extract the first regular-expression match from each string. */
CHARR_ENTRYPOINT SEXP ci_extract_first_regex(
    SEXP str, SEXP pattern, SEXP opts_regex
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    pattern = entry_protections.protect_one(ci__prepare_arg_string_r(pattern, "pattern"));

    const R_len_t subject_length = LENGTH(str);
    const R_len_t pattern_length = LENGTH(pattern);
    bool recycling_warning = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, recycling_warning
    );
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);

    const shared::RegexOptions options = regex::prepare_options(opts_regex);

    int empty_pattern_warnings = 0;

    try {
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        std::vector<shared::StringView> subjects;
        shared::RegexPatterns patterns;
        shared::RegexMatcher matcher(options);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                if (vectorize_length > 0) {
                    subjects.resize(
                        static_cast<std::size_t>(subject_length)
                    );
                    const SEXP* values = STRING_PTR_RO(str);
                    for (R_len_t i = 0; i < subject_length; ++i) {
                        subjects[static_cast<std::size_t>(i)] =
                            normalize_subject(
                                io::as_shared_view(values[i]),
                                subject_converter, subject_storage
                            );
                    }

                    patterns.resize(
                        static_cast<std::size_t>(pattern_length)
                    );
                    values = STRING_PTR_RO(pattern);
                    for (R_len_t i = 0; i < pattern_length; ++i) {
                        patterns.set(
                            static_cast<std::size_t>(i),
                            normalize_pattern(
                                io::as_shared_view(values[i]),
                                pattern_converter, pattern_storage
                            )
                        );
                    }
                    empty_pattern_warnings = patterns.empty_count();
                }

                result = entry_protections.reprotect_one(
                    Rf_allocVector(STRSXP, vectorize_length),
                    result_index
                );

                if (pattern_length == 1 && vectorize_length > 0) {
                    const std::size_t pattern_index = 0;
                    const shared::RegexInput current_pattern =
                        patterns.get(pattern_index);
                    const bool pattern_unusable =
                        current_pattern.missing ||
                        current_pattern.length <= 0;
                    bool pattern_bound = false;

                    for (R_len_t i = 0; i < vectorize_length; ++i) {
                        const shared::StringView& subject = subjects[
                            static_cast<std::size_t>(i)
                        ];
                        if (subject.is_na() || pattern_unusable) {
                            SET_STRING_ELT(result, i, NA_STRING);
                            continue;
                        }
                        if (!pattern_bound) {
                            bind_pattern(
                                matcher, current_pattern, patterns,
                                pattern_index
                            );
                            pattern_bound = true;
                        }

                        shared::RegexRange match{0, 0};
                        UErrorCode status = U_ZERO_ERROR;
                        const bool found = matcher.find_first(
                            subject, &subject, match, status
                        );
                        if (U_FAILURE(status)) {
                            throw_regex_error(
                                status, false, patterns, pattern_index
                            );
                        }
                        if (found)
                            set_match_r(result, i, subject, match);
                        else
                            SET_STRING_ELT(result, i, NA_STRING);
                    }
                }
                else {
                    for (R_len_t lane = 0;
                            lane < (vectorize_length > 0
                                ? pattern_length : 0);
                            ++lane) {
                        const std::size_t pattern_index =
                            static_cast<std::size_t>(lane);
                        const shared::RegexInput current_pattern =
                            patterns.get(pattern_index);
                        const bool pattern_unusable =
                            current_pattern.missing ||
                            current_pattern.length <= 0;
                        bool pattern_bound = false;

                        R_len_t i = lane;
                        for (;;) {
                            const std::size_t subject_index =
                                static_cast<std::size_t>(
                                    i % subject_length
                                );
                            const shared::StringView& subject =
                                subjects[subject_index];
                            if (subject.is_na() || pattern_unusable) {
                                SET_STRING_ELT(result, i, NA_STRING);
                            }
                            else {
                                if (!pattern_bound) {
                                    bind_pattern(
                                        matcher, current_pattern,
                                        patterns, pattern_index
                                    );
                                    pattern_bound = true;
                                }

                                shared::RegexRange match{0, 0};
                                UErrorCode status = U_ZERO_ERROR;
                                const bool found = matcher.find_first(
                                    subject, &subjects[subject_index],
                                    match, status
                                );
                                if (U_FAILURE(status)) {
                                    throw_regex_error(
                                        status, false, patterns,
                                        pattern_index
                                    );
                                }
                                if (found)
                                    set_match_r(result, i, subject, match);
                                else
                                    SET_STRING_ELT(result, i, NA_STRING);
                            }

                            if (pattern_length >= vectorize_length-i)
                                break;
                            i += pattern_length;
                        }
                    }
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_empty_pattern_warnings_r(empty_pattern_warnings);
    );
}


/** Extract every regular-expression match from each string. */
CHARR_ENTRYPOINT SEXP ci_extract_all_regex(
    SEXP str,
    SEXP pattern,
    SEXP simplify,
    SEXP omit_no_match,
    SEXP opts_regex
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const shared::RegexOptions options = regex::prepare_options(opts_regex);
    const bool omit_no_match_value =
        ci__prepare_arg_logical_1_notNA_r(
            omit_no_match, "omit_no_match"
        );
    simplify = entry_protections.protect_one(ci__prepare_arg_logical_1_r(
        simplify, "simplify"
    ));
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    pattern = entry_protections.protect_one(ci__prepare_arg_string_r(pattern, "pattern"));

    const int simplify_value = LOGICAL_RO(simplify)[0];
    const bool simplifying =
        simplify_value == NA_LOGICAL || simplify_value != 0;
    const R_len_t subject_length = LENGTH(str);
    const R_len_t pattern_length = LENGTH(pattern);
    bool recycling_warning = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, recycling_warning
    );
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);

    int empty_pattern_warnings = 0;
    R_len_t max_columns = 0;

    try {
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        std::vector<shared::StringView> subjects;
        shared::RegexPatterns patterns;
        shared::RegexMatcher matcher(options);
        std::vector<shared::RegexRange> matches;

        matches.reserve(8);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                if (vectorize_length > 0) {
                    subjects.resize(
                        static_cast<std::size_t>(subject_length)
                    );
                    const SEXP* values = STRING_PTR_RO(str);
                    for (R_len_t i = 0; i < subject_length; ++i) {
                        subjects[static_cast<std::size_t>(i)] =
                            normalize_subject(
                                io::as_shared_view(values[i]),
                                subject_converter, subject_storage
                            );
                    }

                    patterns.resize(
                        static_cast<std::size_t>(pattern_length)
                    );
                    values = STRING_PTR_RO(pattern);
                    for (R_len_t i = 0; i < pattern_length; ++i) {
                        patterns.set(
                            static_cast<std::size_t>(i),
                            normalize_pattern(
                                io::as_shared_view(values[i]),
                                pattern_converter, pattern_storage
                            )
                        );
                    }
                    empty_pattern_warnings = patterns.empty_count();
                }

                result = entry_protections.reprotect_one(
                    Rf_allocVector(VECSXP, vectorize_length),
                    result_index
                );

                for (R_len_t lane = 0;
                        lane < (vectorize_length > 0
                            ? pattern_length : 0);
                        ++lane) {
                    const std::size_t pattern_index =
                        static_cast<std::size_t>(lane);
                    const shared::RegexInput current_pattern =
                        patterns.get(pattern_index);
                    const bool pattern_unusable =
                        current_pattern.missing ||
                        current_pattern.length <= 0;
                    bool pattern_bound = false;

                    R_len_t i = lane;
                    for (;;) {
                        const std::size_t subject_index =
                            pattern_length == 1
                                ? static_cast<std::size_t>(i)
                                : static_cast<std::size_t>(
                                    i % subject_length
                                );
                        const shared::StringView& subject =
                            subjects[subject_index];
                        SEXP child = R_NilValue;
                        R_len_t child_size = 1;

                        if (subject.is_na() || pattern_unusable) {
                            child = missing_strings_r(1);
                        }
                        else {
                            if (!pattern_bound) {
                                bind_pattern(
                                    matcher, current_pattern, patterns,
                                    pattern_index
                                );
                                pattern_bound = true;
                            }

                            UErrorCode status = U_ZERO_ERROR;
                            matcher.find_all(
                                subject, &subjects[subject_index],
                                matches, status
                            );
                            if (U_FAILURE(status)) {
                                throw_regex_error(
                                    status, false, patterns,
                                    pattern_index
                                );
                            }

                            child_size = static_cast<R_len_t>(
                                matches.size()
                            );
                            if (child_size <= 0) {
                                child_size = omit_no_match_value ? 0 : 1;
                                child = missing_strings_r(child_size);
                            }
                            else {
                                child = strings_r(child_size);
                                SET_VECTOR_ELT(result, i, child);
                                for (R_len_t j = 0;
                                        j < child_size; ++j) {
                                    set_match_r(
                                        child, j, subject,
                                        matches[
                                            static_cast<std::size_t>(j)
                                        ]
                                    );
                                }
                            }
                        }

                        SET_VECTOR_ELT(result, i, child);
                        if (simplifying && max_columns < child_size)
                            max_columns = child_size;

                        if (pattern_length >= vectorize_length-i)
                            break;
                        i += pattern_length;
                    }
                }

                if (simplifying) {
                    result = entry_protections.reprotect_one(
                        simplify_result_r(
                            result, vectorize_length, max_columns,
                            simplify_value == NA_LOGICAL
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

} } // namespace charr::base_backend
