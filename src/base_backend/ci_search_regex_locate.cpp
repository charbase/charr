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
#include "io/string_view.h"
#include "regex/options_r.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/r_matrix.h"
#include "../shared/regex_search.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace charr { namespace base_backend {

namespace search_regex_locate {

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


CHARR_CXX_HELPER shared::StringView normalize_input(
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


CHARR_CXX_HELPER void read_capture_names(
    const shared::RegexMatcher& matcher,
    const shared::RegexPatterns& patterns,
    std::size_t pattern_index,
    std::vector<std::string>& names
)
{
    UErrorCode status = U_ZERO_ERROR;
    matcher.capture_names(names, status);
    if (U_FAILURE(status))
        throw_regex_error(status, false, patterns, pattern_index);
}


CHARR_CXX_HELPER bool locate_first(
    shared::RegexMatcher& matcher,
    const shared::StringView& subject,
    const void* subject_identity,
    bool capture_groups,
    bool return_length,
    const shared::RegexPatterns& patterns,
    std::size_t pattern_index,
    shared::RegexRange& match,
    std::vector<shared::RegexRange>& captures
)
{
    UErrorCode status = U_ZERO_ERROR;
    const bool found = capture_groups
        ? matcher.find_first_with_captures(
            subject, subject_identity, match, captures, status
        )
        : matcher.find_first(
            subject, subject_identity, match, status
        );
    if (U_FAILURE(status))
        throw_regex_error(status, false, patterns, pattern_index);
    if (!found)
        return false;

    shared::regex_range_to_positions(subject, match, return_length);
    if (capture_groups) {
        for (std::size_t i = 0; i < captures.size(); ++i) {
            shared::regex_range_to_positions(
                subject, captures[i], return_length
            );
        }
    }
    return true;
}


CHARR_CXX_HELPER void locate_all(
    shared::RegexMatcher& matcher,
    const shared::StringView& subject,
    const void* subject_identity,
    bool capture_groups,
    bool return_length,
    const shared::RegexPatterns& patterns,
    std::size_t pattern_index,
    std::vector<shared::RegexRange>& matches,
    std::vector<std::vector<shared::RegexRange> >& captures
)
{
    UErrorCode status = U_ZERO_ERROR;
    if (capture_groups) {
        matcher.find_all_with_captures(
            subject, subject_identity, matches, captures, status
        );
    }
    else {
        matcher.find_all(subject, subject_identity, matches, status);
    }
    if (U_FAILURE(status))
        throw_regex_error(status, false, patterns, pattern_index);

    shared::regex_ranges_to_positions(
        subject, matches, return_length
    );
    if (capture_groups) {
        for (std::size_t i = 0; i < captures.size(); ++i) {
            shared::regex_ranges_to_positions(
                subject, captures[i], return_length
            );
        }
    }
}


CHARR_CXX_HELPER void ensure_capture_columns(
    std::vector<std::vector<shared::RegexRange> >& columns,
    std::size_t count,
    std::size_t length
)
{
    const std::size_t previous = columns.size();
    if (previous >= count)
        return;
    columns.resize(count);
    for (std::size_t i = previous; i < count; ++i) {
        columns[i].assign(
            length, shared::RegexRange{NA_INTEGER, NA_INTEGER}
        );
    }
}


CHARR_NEUTRAL_HELPER void set_no_match_captures(
    std::vector<std::vector<shared::RegexRange> >& columns,
    std::size_t count,
    std::size_t index,
    bool return_length
) noexcept
{
    const int value = return_length ? -1 : NA_INTEGER;
    for (std::size_t i = 0; i < count; ++i)
        columns[i][index] = shared::RegexRange{value, value};
}


CHARR_NEUTRAL_HELPER void store_captures(
    const std::vector<shared::RegexRange>& captures,
    std::vector<std::vector<shared::RegexRange> >& columns,
    std::size_t index,
    bool return_length
) noexcept
{
    for (std::size_t i = 0; i < captures.size(); ++i) {
        shared::RegexRange value = captures[i];
        if (value.start < 0 || value.end < 0) {
            const int missing = return_length ? -1 : NA_INTEGER;
            value = shared::RegexRange{missing, missing};
        }
        columns[i][index] = value;
    }
}


CHARR_NEUTRAL_HELPER bool capture_names_present(
    const std::vector<std::string>& names
) noexcept
{
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i].size() > 0)
            return true;
    }
    return false;
}


CHARR_R_HELPER SEXP ranges_matrix_r(
    const std::vector<shared::RegexRange>& ranges,
    bool subject_missing,
    bool omit_no_match,
    bool return_length
) noexcept
{
    if (subject_missing)
        return shared::filled_integer_matrix_r(1, 2);

    const R_len_t count = static_cast<R_len_t>(ranges.size());
    if (count <= 0) {
        return shared::filled_integer_matrix_r(
            omit_no_match ? 0 : 1, 2,
            return_length ? -1 : NA_INTEGER
        );
    }

    SEXP result = Rf_allocMatrix(INTSXP, count, 2);
    int* output = INTEGER(result);
    for (R_len_t i = 0; i < count; ++i) {
        const shared::RegexRange& range = ranges[
            static_cast<std::size_t>(i)
        ];
        if (range.start < 0 || range.end < 0) {
            const int missing = return_length ? -1 : NA_INTEGER;
            output[i] = missing;
            output[i+count] = missing;
        }
        else {
            output[i] = range.start;
            output[i+count] = range.end;
        }
    }
    return result;
}


CHARR_R_HELPER void fill_capture_matrix_r(
    SEXP output,
    const std::vector<shared::RegexRange>& values
) noexcept
{
    const R_len_t count = static_cast<R_len_t>(values.size());
    int* data = INTEGER(output);
    for (R_len_t i = 0; i < count; ++i) {
        const shared::RegexRange& value = values[
            static_cast<std::size_t>(i)
        ];
        data[i] = value.start;
        data[i+count] = value.end;
    }
}


CHARR_R_HELPER SEXP capture_names_r(
    const std::vector<std::string>& names
) noexcept
{
    if (!capture_names_present(names))
        return R_NilValue;

    const R_len_t count = static_cast<R_len_t>(names.size());
    SEXP result = Rf_allocVector(STRSXP, count);
    for (R_len_t i = 0; i < count; ++i) {
        const std::string& name = names[static_cast<std::size_t>(i)];
        SET_STRING_ELT(
            result, i,
            Rf_mkCharLenCE(
                name.size() == 0 ? "" : name.data(),
                static_cast<int>(name.size()), CE_UTF8
            )
        );
    }
    return result;
}


CHARR_R_HELPER void emit_warnings_r(
    bool recycling_warning,
    int empty_pattern_warnings
) noexcept
{
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);
    for (int i = 0; i < empty_pattern_warnings; ++i)
        Rf_warning(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
}

} // namespace search_regex_locate

using namespace search_regex_locate;


/** Locate the first regular-expression match in each string. */
CHARR_ENTRYPOINT SEXP ci_locate_first_regex(
    SEXP str,
    SEXP pattern,
    SEXP opts_regex,
    SEXP capture_groups,
    SEXP get_length
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool capture = ci__prepare_arg_logical_1_notNA_r(
        capture_groups, "capture_groups"
    );
    const bool return_length = ci__prepare_arg_logical_1_notNA_r(
        get_length, "get_length"
    );
    const shared::RegexOptions options = regex::prepare_options(opts_regex);
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    pattern = entry_protections.protect_one(ci__prepare_arg_string_r(pattern, "pattern"));

    const R_len_t subject_length = LENGTH(str);
    const R_len_t pattern_length = LENGTH(pattern);
    bool recycling_warning = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, recycling_warning
    );
    const SEXP capture_symbol = capture
        ? Rf_install("capture_groups")
        : R_NilValue;

    int empty_pattern_warnings = 0;

    try {
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        std::vector<shared::StringView> subjects;
        shared::RegexPatterns patterns;
        shared::RegexMatcher matcher(options);
        std::vector<shared::RegexRange> captures;
        std::vector<std::vector<shared::RegexRange> > capture_columns;
        std::vector<std::string> capture_names;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                if (vectorize_length > 0) {
                    patterns.resize(
                        static_cast<std::size_t>(pattern_length)
                    );
                    const SEXP* values = STRING_PTR_RO(pattern);
                    for (R_len_t i = 0; i < pattern_length; ++i) {
                        patterns.set(
                            static_cast<std::size_t>(i),
                            normalize_input(
                                io::as_shared_view(values[i]),
                                pattern_converter, pattern_storage
                            )
                        );
                    }
                    empty_pattern_warnings = patterns.empty_count();

                    subjects.resize(
                        static_cast<std::size_t>(subject_length)
                    );
                    values = STRING_PTR_RO(str);
                    for (R_len_t i = 0; i < subject_length; ++i) {
                        subjects[static_cast<std::size_t>(i)] =
                            normalize_input(
                                io::as_shared_view(values[i]),
                                subject_converter, subject_storage
                            );
                    }
                }

                result = entry_protections.reprotect_one(
                    Rf_allocMatrix(INTSXP, vectorize_length, 2),
                    result_index
                );
                int* output = INTEGER(result);
                for (R_len_t i = 0; i < vectorize_length; ++i) {
                    output[i] = NA_INTEGER;
                    output[i+vectorize_length] = NA_INTEGER;
                }

                for (R_len_t lane = 0;
                        lane < (vectorize_length > 0
                            ? pattern_length : 0);
                        ++lane) {
                    const std::size_t pattern_index =
                        static_cast<std::size_t>(lane);
                    const shared::RegexInput current_pattern =
                        patterns.get(pattern_index);
                    const bool pattern_empty =
                        !current_pattern.missing &&
                        current_pattern.length <= 0;
                    const bool pattern_unusable =
                        current_pattern.missing || pattern_empty;

                    if (!pattern_unusable) {
                        bind_pattern(
                            matcher, current_pattern, patterns,
                            pattern_index
                        );
                        if (capture) {
                            ensure_capture_columns(
                                capture_columns,
                                static_cast<std::size_t>(
                                    matcher.group_count()
                                ),
                                static_cast<std::size_t>(vectorize_length)
                            );
                            if (pattern_length == 1) {
                                read_capture_names(
                                    matcher, patterns, pattern_index,
                                    capture_names
                                );
                            }
                        }
                    }

                    for (R_len_t i = lane; i < vectorize_length;
                            i += pattern_length) {
                        if (pattern_unusable) {
                            if (pattern_empty)
                                ++empty_pattern_warnings;
                            continue;
                        }

                        const shared::StringView& subject = subjects[
                            static_cast<std::size_t>(i % subject_length)
                        ];
                        if (subject.is_na())
                            continue;

                        shared::RegexRange match{0, 0};
                        const bool found = locate_first(
                            matcher, subject, &subject,
                            capture, return_length,
                            patterns, pattern_index, match, captures
                        );
                        const std::size_t group_count =
                            static_cast<std::size_t>(
                                matcher.group_count()
                            );
                        if (!found) {
                            if (return_length) {
                                output[i] = -1;
                                output[i+vectorize_length] = -1;
                            }
                            if (capture) {
                                set_no_match_captures(
                                    capture_columns, group_count,
                                    static_cast<std::size_t>(i),
                                    return_length
                                );
                            }
                            continue;
                        }

                        output[i] = match.start;
                        output[i+vectorize_length] = match.end;
                        if (capture) {
                            store_captures(
                                captures, capture_columns,
                                static_cast<std::size_t>(i),
                                return_length
                            );
                        }
                    }
                }

                if (capture) {
                    SEXP capture_result = R_NilValue;
                    PROTECT_INDEX capture_result_index;
                    callback_protections.protect_with_index(
                        capture_result, &capture_result_index
                    );
                    SEXP capture_matrix = R_NilValue;
                    PROTECT_INDEX capture_matrix_index;
                    callback_protections.protect_with_index(
                        capture_matrix, &capture_matrix_index
                    );
                    SEXP names = R_NilValue;
                    PROTECT_INDEX names_index;
                    callback_protections.protect_with_index(names, &names_index);

                    const R_len_t group_count = static_cast<R_len_t>(
                        capture_columns.size()
                    );
                    capture_result = callback_protections.reprotect_slot(
                        Rf_allocVector(VECSXP, group_count),
                        capture_result_index
                    );
                    for (R_len_t i = 0; i < group_count; ++i) {
                        capture_matrix = callback_protections.reprotect_slot(
                            Rf_allocMatrix(
                                INTSXP, vectorize_length, 2
                            ),
                            capture_matrix_index
                        );
                        fill_capture_matrix_r(
                            capture_matrix,
                            capture_columns[static_cast<std::size_t>(i)]
                        );
                        SET_VECTOR_ELT(capture_result, i, capture_matrix);
                    }
                    ci__locate_set_dimnames_list(
                        capture_result, return_length
                    );
                    if (capture_names_present(capture_names)) {
                        names = callback_protections.reprotect_slot(
                            capture_names_r(capture_names), names_index
                        );
                        Rf_setAttrib(
                            capture_result, R_NamesSymbol, names
                        );
                    }
                    Rf_setAttrib(
                        result, capture_symbol, capture_result
                    );
                }

                ci__locate_set_dimnames_matrix(result, return_length);
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


/** Locate all regular-expression matches in each string. */
CHARR_ENTRYPOINT SEXP ci_locate_all_regex(
    SEXP str,
    SEXP pattern,
    SEXP omit_no_match,
    SEXP opts_regex,
    SEXP capture_groups,
    SEXP get_length
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool omit = ci__prepare_arg_logical_1_notNA_r(
        omit_no_match, "omit_no_match"
    );
    const bool capture = ci__prepare_arg_logical_1_notNA_r(
        capture_groups, "capture_groups"
    );
    const bool return_length = ci__prepare_arg_logical_1_notNA_r(
        get_length, "get_length"
    );
    const shared::RegexOptions options = regex::prepare_options(opts_regex);
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    pattern = entry_protections.protect_one(ci__prepare_arg_string_r(pattern, "pattern"));

    const R_len_t subject_length = LENGTH(str);
    const R_len_t pattern_length = LENGTH(pattern);
    bool recycling_warning = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, recycling_warning
    );
    const SEXP capture_symbol = capture
        ? Rf_install("capture_groups")
        : R_NilValue;

    int empty_pattern_warnings = 0;

    try {
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        std::vector<shared::StringView> subjects;
        shared::RegexPatterns patterns;
        shared::RegexMatcher matcher(options);
        std::vector<shared::RegexRange> matches;
        std::vector<std::vector<shared::RegexRange> > captures;
        std::vector<std::string> capture_names;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                if (vectorize_length > 0) {
                    patterns.resize(
                        static_cast<std::size_t>(pattern_length)
                    );
                    const SEXP* values = STRING_PTR_RO(pattern);
                    for (R_len_t i = 0; i < pattern_length; ++i) {
                        patterns.set(
                            static_cast<std::size_t>(i),
                            normalize_input(
                                io::as_shared_view(values[i]),
                                pattern_converter, pattern_storage
                            )
                        );
                    }
                    empty_pattern_warnings = patterns.empty_count();

                    subjects.resize(
                        static_cast<std::size_t>(subject_length)
                    );
                    values = STRING_PTR_RO(str);
                    for (R_len_t i = 0; i < subject_length; ++i) {
                        subjects[static_cast<std::size_t>(i)] =
                            normalize_input(
                                io::as_shared_view(values[i]),
                                subject_converter, subject_storage
                            );
                    }
                }

                result = entry_protections.reprotect_one(
                    Rf_allocVector(VECSXP, vectorize_length),
                    result_index
                );
                SEXP current = R_NilValue;
                PROTECT_INDEX current_index;
                callback_protections.protect_with_index(current, &current_index);

                SEXP capture_result = R_NilValue;
                PROTECT_INDEX capture_result_index;
                SEXP capture_matrix = R_NilValue;
                PROTECT_INDEX capture_matrix_index;
                SEXP names = R_NilValue;
                PROTECT_INDEX names_index;
                callback_protections.protect_with_index(
                    capture_result, &capture_result_index
                );
                callback_protections.protect_with_index(
                    capture_matrix, &capture_matrix_index
                );
                callback_protections.protect_with_index(names, &names_index);

                for (R_len_t lane = 0;
                        lane < (vectorize_length > 0
                            ? pattern_length : 0);
                        ++lane) {
                    const std::size_t pattern_index =
                        static_cast<std::size_t>(lane);
                    const shared::RegexInput current_pattern =
                        patterns.get(pattern_index);
                    const bool pattern_empty =
                        !current_pattern.missing &&
                        current_pattern.length <= 0;
                    const bool pattern_unusable =
                        current_pattern.missing || pattern_empty;
                    int group_count = 0;

                    if (!pattern_unusable) {
                        bind_pattern(
                            matcher, current_pattern, patterns,
                            pattern_index
                        );
                        group_count = matcher.group_count();
                        if (capture) {
                            read_capture_names(
                                matcher, patterns, pattern_index,
                                capture_names
                            );
                        }
                    }

                    for (R_len_t i = lane; i < vectorize_length;
                            i += pattern_length) {
                        if (pattern_unusable) {
                            if (pattern_empty)
                                ++empty_pattern_warnings;
                            current = callback_protections.reprotect_slot(
                                shared::filled_integer_matrix_r(1, 2),
                                current_index
                            );
                            if (capture) {
                                capture_result =
                                    callback_protections.reprotect_slot(
                                        Rf_allocVector(VECSXP, 0),
                                        capture_result_index
                                    );
                                Rf_setAttrib(
                                    current, capture_symbol,
                                    capture_result
                                );
                            }
                            SET_VECTOR_ELT(result, i, current);
                            continue;
                        }

                        const shared::StringView& subject = subjects[
                            static_cast<std::size_t>(i % subject_length)
                        ];
                        const bool subject_missing = subject.is_na();
                        if (subject_missing) {
                            matches.clear();
                            captures.clear();
                            if (capture) {
                                captures.resize(
                                    static_cast<std::size_t>(group_count)
                                );
                            }
                        }
                        else {
                            locate_all(
                                matcher, subject, &subject,
                                capture, return_length,
                                patterns, pattern_index,
                                matches, captures
                            );
                        }

                        current = callback_protections.reprotect_slot(
                            ranges_matrix_r(
                                matches, subject_missing, omit,
                                return_length
                            ),
                            current_index
                        );

                        if (capture) {
                            capture_result = callback_protections.reprotect_slot(
                                Rf_allocVector(VECSXP, group_count),
                                capture_result_index
                            );
                            for (int j = 0; j < group_count; ++j) {
                                capture_matrix =
                                    callback_protections.reprotect_slot(
                                        ranges_matrix_r(
                                            captures[
                                                static_cast<std::size_t>(j)
                                            ],
                                            subject_missing, omit,
                                            return_length
                                        ),
                                        capture_matrix_index
                                    );
                                SET_VECTOR_ELT(
                                    capture_result, j, capture_matrix
                                );
                            }
                            ci__locate_set_dimnames_list(
                                capture_result, return_length
                            );
                            if (capture_names_present(capture_names)) {
                                names = callback_protections.reprotect_slot(
                                    capture_names_r(capture_names),
                                    names_index
                                );
                                Rf_setAttrib(
                                    capture_result, R_NamesSymbol,
                                    names
                                );
                            }
                            Rf_setAttrib(
                                current, capture_symbol, capture_result
                            );
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
        emit_warnings_r(
            recycling_warning, empty_pattern_warnings
        );
    );
}

} } // namespace charr::base_backend
