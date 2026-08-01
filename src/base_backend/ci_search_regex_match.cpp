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
#include <stdexcept>
#include <string>
#include <vector>

namespace charr { namespace base_backend {

namespace search_regex_match {

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


CHARR_CXX_HELPER bool find_first(
    shared::RegexMatcher& matcher,
    const shared::StringView& subject,
    const void* subject_identity,
    const shared::RegexPatterns& patterns,
    std::size_t pattern_index,
    shared::RegexRange& match,
    std::vector<shared::RegexRange>& captures
)
{
    UErrorCode status = U_ZERO_ERROR;
    const bool found = matcher.find_first_with_captures(
        subject, subject_identity, match, captures, status
    );
    if (U_FAILURE(status))
        throw_regex_error(status, false, patterns, pattern_index);
    return found;
}


CHARR_CXX_HELPER void find_all(
    shared::RegexMatcher& matcher,
    const shared::StringView& subject,
    const void* subject_identity,
    const shared::RegexPatterns& patterns,
    std::size_t pattern_index,
    std::vector<shared::RegexRange>& matches,
    std::vector<std::vector<shared::RegexRange> >& captures
)
{
    UErrorCode status = U_ZERO_ERROR;
    matcher.find_all_with_captures(
        subject, subject_identity, matches, captures, status
    );
    if (U_FAILURE(status))
        throw_regex_error(status, false, patterns, pattern_index);
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


CHARR_NEUTRAL_HELPER void stage_no_match(
    std::vector<shared::RegexRange>& matches,
    std::vector<std::vector<shared::RegexRange> >& captures,
    std::size_t group_count,
    std::size_t index
) noexcept
{
    matches[index] = shared::RegexRange{-1, -1};
    for (std::size_t i = 0; i < group_count; ++i)
        captures[i][index] = shared::RegexRange{-1, -1};
}


CHARR_NEUTRAL_HELPER void stage_match(
    const shared::RegexRange& match,
    const std::vector<shared::RegexRange>& current_captures,
    std::vector<shared::RegexRange>& matches,
    std::vector<std::vector<shared::RegexRange> >& captures,
    std::size_t index
) noexcept
{
    matches[index] = match;
    for (std::size_t i = 0; i < current_captures.size(); ++i)
        captures[i][index] = current_captures[i];
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


CHARR_R_HELPER SEXP missing_matrix_r(
    R_len_t rows, R_len_t columns
) noexcept
{
    SEXP result = Rf_allocMatrix(STRSXP, rows, columns);
    const R_xlen_t size = static_cast<R_xlen_t>(rows)*columns;
    for (R_xlen_t i = 0; i < size; ++i)
        SET_STRING_ELT(result, i, NA_STRING);
    return result;
}


CHARR_R_HELPER void set_match_cell_r(
    SEXP output,
    R_xlen_t index,
    const shared::StringView& subject,
    const shared::RegexRange& range,
    SEXP capture_missing
) noexcept
{
    if (range.start == NA_INTEGER || range.end == NA_INTEGER) {
        SET_STRING_ELT(output, index, NA_STRING);
        return;
    }
    if (range.start < 0 || range.end < 0) {
        SET_STRING_ELT(output, index, capture_missing);
        return;
    }

    const int length = range.end-range.start;
    SET_STRING_ELT(
        output, index,
        Rf_mkCharLenCE(
            length == 0 ? "" : subject.ptr+range.start,
            length, CE_UTF8
        )
    );
}


CHARR_R_HELPER void fill_staged_first_r(
    SEXP output,
    R_len_t rows,
    R_len_t subject_length,
    const std::vector<shared::StringView>& subjects,
    const std::vector<shared::RegexRange>& matches,
    const std::vector<std::vector<shared::RegexRange> >& captures,
    SEXP capture_missing
) noexcept
{
    for (R_len_t row = 0; row < rows; ++row) {
        const shared::StringView& subject = subjects[
            static_cast<std::size_t>(row % subject_length)
        ];
        set_match_cell_r(
            output, row, subject,
            matches[static_cast<std::size_t>(row)], capture_missing
        );
        for (std::size_t column = 0; column < captures.size(); ++column) {
            set_match_cell_r(
                output,
                row+static_cast<R_xlen_t>(column+1)*rows,
                subject, captures[column][static_cast<std::size_t>(row)],
                capture_missing
            );
        }
    }
}


CHARR_R_HELPER SEXP all_matches_matrix_r(
    const shared::StringView& subject,
    const std::vector<shared::RegexRange>& matches,
    const std::vector<std::vector<shared::RegexRange> >& captures,
    bool omit_no_match,
    SEXP capture_missing
) noexcept
{
    const R_len_t columns = static_cast<R_len_t>(captures.size()+1);
    if (subject.is_na())
        return missing_matrix_r(1, columns);

    const R_len_t count = static_cast<R_len_t>(matches.size());
    const R_len_t rows = count > 0 ? count : (omit_no_match ? 0 : 1);
    SEXP result = PROTECT(missing_matrix_r(rows, columns));
    for (R_len_t row = 0; row < count; ++row) {
        set_match_cell_r(
            result, row, subject,
            matches[static_cast<std::size_t>(row)], capture_missing
        );
        for (R_len_t column = 1; column < columns; ++column) {
            set_match_cell_r(
                result, row+static_cast<R_xlen_t>(column)*rows,
                subject,
                captures[static_cast<std::size_t>(column-1)][
                    static_cast<std::size_t>(row)
                ],
                capture_missing
            );
        }
    }
    UNPROTECT(1);
    return result;
}


CHARR_R_HELPER SEXP capture_dimnames_r(
    const std::vector<std::string>& names
) noexcept
{
    if (!capture_names_present(names))
        return R_NilValue;

    SEXP dimnames = PROTECT(Rf_allocVector(VECSXP, 2));
    SEXP columns = PROTECT(Rf_allocVector(STRSXP, names.size()+1));
    SET_STRING_ELT(columns, 0, R_BlankString);
    for (std::size_t i = 0; i < names.size(); ++i) {
        SET_STRING_ELT(
            columns, i+1,
            Rf_mkCharLenCE(
                names[i].size() == 0 ? "" : names[i].data(),
                static_cast<int>(names[i].size()), CE_UTF8
            )
        );
    }
    SET_VECTOR_ELT(dimnames, 1, columns);
    UNPROTECT(2);
    return dimnames;
}


CHARR_R_HELPER void set_dimnames_r(
    SEXP matrix, SEXP dimnames
) noexcept
{
    if (dimnames != R_NilValue)
        Rf_setAttrib(matrix, R_DimNamesSymbol, dimnames);
}


CHARR_R_HELPER void emit_warnings_r(
    int empty_pattern_warnings
) noexcept
{
    for (int i = 0; i < empty_pattern_warnings; ++i)
        Rf_warning(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
}

} // namespace search_regex_match

using namespace search_regex_match;


/** Extract the first regular-expression match and its capture groups. */
CHARR_ENTRYPOINT SEXP ci_match_first_regex(
    SEXP str, SEXP pattern, SEXP cg_missing, SEXP opts_regex
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    pattern = entry_protections.protect_one(ci__prepare_arg_string_r(pattern, "pattern"));
    cg_missing = entry_protections.protect_one(ci__prepare_arg_string_1_r(
        cg_missing, "cg_missing"
    ));

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
        shared::NativeToUtf8 missing_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        shared::SliceArena missing_storage;
        std::vector<shared::StringView> subjects;
        shared::RegexPatterns patterns;
        shared::RegexMatcher matcher(options);
        shared::RegexRange current_match{0, 0};
        std::vector<shared::RegexRange> current_captures;
        std::vector<shared::RegexRange> staged_matches;
        std::vector<std::vector<shared::RegexRange> > staged_captures;
        std::vector<std::string> capture_names;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const SEXP capture_missing = STRING_ELT(cg_missing, 0);
                (void)normalize_subject(
                    io::as_shared_view(capture_missing),
                    missing_converter, missing_storage
                );

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
                }

                patterns.resize(static_cast<std::size_t>(pattern_length));
                const SEXP* values = pattern_length > 0
                    ? STRING_PTR_RO(pattern)
                    : nullptr;
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

                if (pattern_length == 1) {
                    const std::size_t pattern_index = 0;
                    const shared::RegexInput current_pattern =
                        patterns.get(pattern_index);
                    const bool pattern_empty =
                        !current_pattern.missing &&
                        current_pattern.length <= 0;
                    int group_count = 0;
                    if (!current_pattern.missing && !pattern_empty) {
                        bind_pattern(
                            matcher, current_pattern, patterns,
                            pattern_index
                        );
                        group_count = matcher.group_count();
                        read_capture_names(
                            matcher, patterns, pattern_index,
                            capture_names
                        );
                    }
                    if (pattern_empty) {
                        empty_pattern_warnings += vectorize_length > 0
                            ? vectorize_length
                            : 1;
                    }

                    result = entry_protections.reprotect_one(
                        missing_matrix_r(
                            vectorize_length, group_count+1
                        ),
                        result_index
                    );
                    if (!current_pattern.missing && !pattern_empty) {
                        for (R_len_t i = 0; i < vectorize_length; ++i) {
                            const shared::StringView& subject = subjects[
                                static_cast<std::size_t>(i)
                            ];
                            if (subject.is_na())
                                continue;

                            const bool found = find_first(
                                matcher, subject, &subject,
                                patterns, pattern_index,
                                current_match, current_captures
                            );
                            if (!found) {
                                current_match = shared::RegexRange{-1, -1};
                                for (int column = 0;
                                        column <= group_count; ++column) {
                                    set_match_cell_r(
                                        result,
                                        i+static_cast<R_xlen_t>(column)*
                                            vectorize_length,
                                        subject, current_match,
                                        capture_missing
                                    );
                                }
                                continue;
                            }

                            set_match_cell_r(
                                result, i, subject, current_match,
                                capture_missing
                            );
                            for (int column = 0;
                                    column < group_count; ++column) {
                                set_match_cell_r(
                                    result,
                                    i+static_cast<R_xlen_t>(column+1)*
                                        vectorize_length,
                                    subject,
                                    current_captures[
                                        static_cast<std::size_t>(column)
                                    ],
                                    capture_missing
                                );
                            }
                        }
                    }
                }
                else {
                    staged_matches.assign(
                        static_cast<std::size_t>(vectorize_length),
                        shared::RegexRange{NA_INTEGER, NA_INTEGER}
                    );

                    for (R_len_t lane = 0; lane < pattern_length; ++lane) {
                        const std::size_t pattern_index =
                            static_cast<std::size_t>(lane);
                        const shared::RegexInput current_pattern =
                            patterns.get(pattern_index);
                        const bool pattern_empty =
                            !current_pattern.missing &&
                            current_pattern.length <= 0;
                        if (pattern_empty) {
                            if (vectorize_length <= 0) {
                                ++empty_pattern_warnings;
                            }
                            else {
                                for (R_len_t i = lane;
                                        i < vectorize_length;
                                        i += pattern_length) {
                                    ++empty_pattern_warnings;
                                }
                            }
                            continue;
                        }
                        if (current_pattern.missing)
                            continue;

                        bind_pattern(
                            matcher, current_pattern, patterns,
                            pattern_index
                        );
                        const std::size_t group_count =
                            static_cast<std::size_t>(
                                matcher.group_count()
                            );
                        ensure_capture_columns(
                            staged_captures, group_count,
                            static_cast<std::size_t>(vectorize_length)
                        );

                        for (R_len_t i = lane;
                                i < vectorize_length;
                                i += pattern_length) {
                            const std::size_t subject_index =
                                static_cast<std::size_t>(
                                    i % subject_length
                                );
                            const shared::StringView& subject =
                                subjects[subject_index];
                            if (subject.is_na())
                                continue;

                            const bool found = find_first(
                                matcher, subject,
                                &subjects[subject_index],
                                patterns, pattern_index,
                                current_match, current_captures
                            );
                            if (!found) {
                                stage_no_match(
                                    staged_matches, staged_captures,
                                    group_count,
                                    static_cast<std::size_t>(i)
                                );
                            }
                            else {
                                stage_match(
                                    current_match, current_captures,
                                    staged_matches, staged_captures,
                                    static_cast<std::size_t>(i)
                                );
                            }
                        }
                    }

                    result = entry_protections.reprotect_one(
                        missing_matrix_r(
                            vectorize_length,
                            static_cast<R_len_t>(
                                staged_captures.size()+1
                            )
                        ),
                        result_index
                    );
                    fill_staged_first_r(
                        result, vectorize_length, subject_length,
                        subjects, staged_matches, staged_captures,
                        capture_missing
                    );
                }

                SEXP dimnames = R_NilValue;
                PROTECT_INDEX dimnames_index;
                callback_protections.protect_with_index(
                    dimnames, &dimnames_index
                );
                dimnames = callback_protections.reprotect_slot(
                    capture_dimnames_r(capture_names),
                    dimnames_index
                );
                set_dimnames_r(result, dimnames);

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_warnings_r(empty_pattern_warnings);
    );
}


/** Extract every regular-expression match and its capture groups. */
CHARR_ENTRYPOINT SEXP ci_match_all_regex(
    SEXP str,
    SEXP pattern,
    SEXP omit_no_match,
    SEXP cg_missing,
    SEXP opts_regex
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool omit = ci__prepare_arg_logical_1_notNA_r(
        omit_no_match, "omit_no_match"
    );
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    pattern = entry_protections.protect_one(ci__prepare_arg_string_r(pattern, "pattern"));
    cg_missing = entry_protections.protect_one(ci__prepare_arg_string_1_r(
        cg_missing, "cg_missing"
    ));

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
        shared::NativeToUtf8 missing_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        shared::SliceArena missing_storage;
        std::vector<shared::StringView> subjects;
        shared::RegexPatterns patterns;
        shared::RegexMatcher matcher(options);
        std::vector<shared::RegexRange> matches;
        std::vector<std::vector<shared::RegexRange> > captures;
        std::vector<std::string> capture_names;

        matches.reserve(8);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const SEXP capture_missing = STRING_ELT(cg_missing, 0);
                (void)normalize_subject(
                    io::as_shared_view(capture_missing),
                    missing_converter, missing_storage
                );

                result = entry_protections.reprotect_one(
                    Rf_allocVector(VECSXP, vectorize_length),
                    result_index
                );
                if (vectorize_length > 0) {
                    subjects.resize(static_cast<std::size_t>(subject_length));
                    const SEXP* values = STRING_PTR_RO(str);
                    for (R_len_t i = 0; i < subject_length; ++i) {
                        subjects[static_cast<std::size_t>(i)] =
                            normalize_subject(
                                io::as_shared_view(values[i]),
                                subject_converter, subject_storage
                            );
                    }

                    patterns.resize(static_cast<std::size_t>(pattern_length));
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

                    SEXP current = R_NilValue;
                    PROTECT_INDEX current_index;
                    callback_protections.protect_with_index(current, &current_index);
                    SEXP dimnames = R_NilValue;
                    PROTECT_INDEX dimnames_index;
                    callback_protections.protect_with_index(
                        dimnames, &dimnames_index
                    );

                    for (R_len_t lane = 0; lane < pattern_length; ++lane) {
                        const std::size_t pattern_index =
                            static_cast<std::size_t>(lane);
                        const shared::RegexInput current_pattern =
                            patterns.get(pattern_index);
                        const bool pattern_empty =
                            !current_pattern.missing &&
                            current_pattern.length <= 0;
                        int group_count = 0;

                        if (!current_pattern.missing && !pattern_empty) {
                            bind_pattern(
                                matcher, current_pattern, patterns,
                                pattern_index
                            );
                            group_count = matcher.group_count();
                            read_capture_names(
                                matcher, patterns, pattern_index,
                                capture_names
                            );
                            dimnames = callback_protections.reprotect_slot(
                                capture_dimnames_r(capture_names),
                                dimnames_index
                            );
                        }
                        else {
                            capture_names.clear();
                            dimnames = callback_protections.reprotect_slot(
                                R_NilValue, dimnames_index
                            );
                        }

                        for (R_len_t i = lane;
                                i < vectorize_length;
                                i += pattern_length) {
                            if (current_pattern.missing || pattern_empty) {
                                if (pattern_empty)
                                    ++empty_pattern_warnings;
                                current = callback_protections.reprotect_slot(
                                    missing_matrix_r(1, 1), current_index
                                );
                                SET_VECTOR_ELT(result, i, current);
                                continue;
                            }

                            const std::size_t subject_index =
                                static_cast<std::size_t>(
                                    i % subject_length
                                );
                            const shared::StringView& subject =
                                subjects[subject_index];
                            if (subject.is_na()) {
                                current = callback_protections.reprotect_slot(
                                    missing_matrix_r(1, group_count+1),
                                    current_index
                                );
                            }
                            else {
                                find_all(
                                    matcher, subject,
                                    &subjects[subject_index],
                                    patterns, pattern_index,
                                    matches, captures
                                );
                                current = callback_protections.reprotect_slot(
                                    all_matches_matrix_r(
                                        subject, matches, captures,
                                        omit, capture_missing
                                    ),
                                    current_index
                                );
                            }
                            set_dimnames_r(current, dimnames);
                            SET_VECTOR_ELT(result, i, current);
                        }
                    }
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_warnings_r(empty_pattern_warnings);
    );
}

} } // namespace charr::base_backend
