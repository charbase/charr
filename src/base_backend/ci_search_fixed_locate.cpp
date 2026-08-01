// Copied from stringi 19e9586ba39b3320df49355e32bd18d74ed6098f; stri_* renamed to ci_*. See inst/COPYRIGHTS.
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
#include "fixed/options.h"
#include "io/string_view.h"
#include "../shared/entrypoint.h"
#include "../shared/fixed_search.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/r_matrix.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <vector>

namespace charr { namespace base_backend {

namespace search_fixed_locate {

struct DirectFixedString {
    const char* data;
    R_len_t length;
    bool is_na;
    bool is_ascii;
};


CHARR_R_HELPER bool direct_fixed_string(
    SEXP value, DirectFixedString& output
) noexcept
{
    if (value == NA_STRING) {
        output = DirectFixedString{nullptr, 0, true, false};
        return true;
    }
    if (!IS_ASCII(value) && !IS_UTF8(value))
        return false;

    output = DirectFixedString{
        CHAR(value), LENGTH(value), false, IS_ASCII(value) != 0
    };
    if (!output.is_ascii && output.length >= 3 &&
            static_cast<unsigned char>(output.data[0]) == 0xefU &&
            static_cast<unsigned char>(output.data[1]) == 0xbbU &&
            static_cast<unsigned char>(output.data[2]) == 0xbfU) {
        output.data += 3;
        output.length -= 3;
    }
    return true;
}


CHARR_R_HELPER bool direct_fixed_pattern(
    SEXP pattern, R_len_t pattern_length, unsigned char& pattern_byte
) noexcept
{
    if (pattern_length != 1)
        return false;

    DirectFixedString value;
    if (!direct_fixed_string(STRING_ELT(pattern, 0), value) ||
            value.is_na || value.length != 1) {
        return false;
    }
    pattern_byte = static_cast<unsigned char>(value.data[0]);
    return pattern_byte <= 0x7fU;
}


CHARR_NEUTRAL_HELPER R_len_t count_ascii_byte(
    const char* data, R_len_t length, unsigned char pattern
) noexcept
{
    R_len_t count = 0;
    const unsigned char* current =
        reinterpret_cast<const unsigned char*>(data);
    const unsigned char* end = current+length;
    for (; current != end; ++current)
        count += *current == pattern;
    return count;
}


CHARR_NEUTRAL_HELPER R_len_t find_ascii_byte(
    const char* data, R_len_t length, unsigned char pattern
) noexcept
{
    for (R_len_t i = 0; i < length; ++i) {
        if (static_cast<unsigned char>(data[i]) == pattern)
            return i;
    }
    return -1;
}


CHARR_NEUTRAL_HELPER void fill_ascii_occurrences(
    const DirectFixedString& value, unsigned char pattern,
    int* starts, int* ends, R_len_t occurrence_count, bool return_length
) noexcept
{
    R_len_t occurrence = 0;
    if (value.is_ascii) {
        for (R_len_t i = 0; i < value.length; ++i) {
            if (static_cast<unsigned char>(value.data[i]) != pattern)
                continue;
            starts[occurrence] = i+1;
            ends[occurrence] = return_length ? 1 : i+1;
            ++occurrence;
        }
        return;
    }

    R_len_t current = 0;
    R_len_t position = 1;
    while (current < value.length && occurrence < occurrence_count) {
        if (static_cast<unsigned char>(value.data[current]) == pattern) {
            starts[occurrence] = position;
            ends[occurrence] = return_length ? 1 : position;
            ++occurrence;
        }
        U8_FWD_1(
            reinterpret_cast<const std::uint8_t*>(value.data),
            current, value.length
        );
        ++position;
    }
}


CHARR_R_HELPER bool locate_first_ascii_scalar_direct(
    SEXP subjects, SEXP patterns, shared::FixedSearchOptions options,
    R_len_t pattern_length, R_len_t vectorize_length, bool return_length,
    int* output, R_len_t& general_start
) noexcept
{
    if (options.case_insensitive || options.overlap)
        return false;

    unsigned char pattern;
    if (!direct_fixed_pattern(patterns, pattern_length, pattern))
        return false;

    for (R_len_t i = 0; i < vectorize_length; ++i) {
        int& start_output = output[i];
        int& end_output = output[i+vectorize_length];
        start_output = NA_INTEGER;
        end_output = NA_INTEGER;

        DirectFixedString value;
        if (!direct_fixed_string(STRING_ELT(subjects, i), value)) {
            general_start = i;
            return false;
        }
        if (value.is_na)
            continue;

        if (!value.is_ascii) {
            R_len_t current = 0;
            R_len_t position = 1;
            while (current < value.length) {
                if (static_cast<unsigned char>(value.data[current]) ==
                        pattern) {
                    start_output = position;
                    end_output = return_length ? 1 : position;
                    break;
                }
                U8_FWD_1(
                    reinterpret_cast<const std::uint8_t*>(value.data),
                    current, value.length
                );
                ++position;
            }
        }
        else {
            const R_len_t byte_position = find_ascii_byte(
                value.data, value.length, pattern
            );
            if (byte_position >= 0) {
                start_output = byte_position+1;
                end_output = return_length ? 1 : start_output;
            }
        }

        if (start_output == NA_INTEGER && return_length)
            start_output = end_output = -1;
    }
    return true;
}


CHARR_R_HELPER bool locate_all_ascii_scalar_direct(
    SEXP subjects, SEXP patterns, shared::FixedSearchOptions options,
    R_len_t pattern_length, R_len_t vectorize_length, bool omit,
    bool return_length, SEXP result, shared::ProtHelper& protections,
    SEXP& current, PROTECT_INDEX current_index, R_len_t& general_start
) noexcept
{
    if (options.case_insensitive || options.overlap)
        return false;

    unsigned char pattern;
    if (!direct_fixed_pattern(patterns, pattern_length, pattern))
        return false;

    for (R_len_t i = 0; i < vectorize_length; ++i) {
        DirectFixedString value;
        if (!direct_fixed_string(STRING_ELT(subjects, i), value)) {
            general_start = i;
            return false;
        }
        if (value.is_na) {
            current = protections.reprotect_slot(
                shared::filled_integer_matrix_r(1, 2), current_index
            );
            SET_VECTOR_ELT(result, i, current);
            continue;
        }

        const R_len_t occurrence_count = count_ascii_byte(
            value.data, value.length, pattern
        );
        if (occurrence_count <= 0) {
            current = protections.reprotect_slot(
                shared::filled_integer_matrix_r(
                    omit ? 0 : 1, 2,
                    return_length ? -1 : NA_INTEGER
                ),
                current_index
            );
            SET_VECTOR_ELT(result, i, current);
            continue;
        }

        current = protections.reprotect_slot(
            Rf_allocMatrix(INTSXP, occurrence_count, 2), current_index
        );
        int* output = INTEGER(current);
        fill_ascii_occurrences(
            value, pattern, output, output+occurrence_count,
            occurrence_count, return_length
        );
        SET_VECTOR_ELT(result, i, current);
    }
    return true;
}


CHARR_NEUTRAL_HELPER R_len_t recycling_length(
    R_len_t subject_length, R_len_t pattern_length, bool& warning
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


CHARR_NEUTRAL_HELPER R_len_t count_empty_patterns(
    const std::vector<shared::StringView>& patterns
) noexcept
{
    R_len_t result = 0;
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        const shared::StringView& pattern = patterns[i];
        if (!pattern.is_na() && pattern.len <= 0)
            ++result;
    }
    return result;
}


CHARR_NEUTRAL_HELPER shared::FixedRange no_match_range(
    bool return_length
) noexcept
{
    const int value = return_length ? -1 : NA_INTEGER;
    return shared::FixedRange{value, value};
}


CHARR_CXX_HELPER void locate_first_normalized(
    const std::vector<shared::StringView>& subjects,
    const std::vector<shared::StringView>& patterns,
    R_len_t vectorize_length, R_len_t general_start,
    shared::FixedSearchOptions options, bool return_length,
    shared::FixedMatcher& matcher, int* output
)
{
    const R_len_t subject_length = static_cast<R_len_t>(subjects.size());
    const R_len_t pattern_length = static_cast<R_len_t>(patterns.size());
    if (general_start > 0 && pattern_length != 1) {
        throw std::logic_error(
            "fixed-locate direct prefix requires a scalar pattern"
        );
    }

    for (R_len_t lane = 0; lane < pattern_length; ++lane) {
        const shared::StringView& pattern = patterns[
            static_cast<std::size_t>(lane)
        ];
        R_len_t i = general_start > 0 ? general_start : lane;
        for (;;) {
            const shared::StringView& subject = subjects[
                static_cast<std::size_t>(i % subject_length)
            ];
            shared::FixedRange location{NA_INTEGER, NA_INTEGER};
            if (subject.is_na() || pattern.is_na() || pattern.len <= 0) {
                location = shared::FixedRange{NA_INTEGER, NA_INTEGER};
            }
            else if (subject.len <= 0) {
                location = no_match_range(return_length);
            }
            else {
                shared::FixedRange match{0, 0};
                const bool found = matcher.find_first(
                    subject, pattern, options, match
                );
                if (!found) {
                    location = no_match_range(return_length);
                }
                else {
                    shared::Utf8PositionCursor positions(subject);
                    const int start = positions.at_byte(match.start)+1;
                    const int end = positions.at_byte(match.end);
                    location = shared::FixedRange{
                        start,
                        return_length ? end-start+1 : end
                    };
                }
            }
            output[i] = location.start;
            output[i+vectorize_length] = location.end;

            if (pattern_length >= vectorize_length-i)
                break;
            i += pattern_length;
        }
    }
}


CHARR_CXX_HELPER void find_all_positions(
    const shared::StringView& subject,
    const shared::StringView& pattern,
    shared::FixedSearchOptions options, bool return_length,
    shared::FixedMatcher& matcher,
    std::vector<shared::FixedRange>& matches
)
{
    matcher.find_all(subject, pattern, options, matches);

    shared::Utf8PositionCursor start_positions(subject);
    for (shared::FixedRange& match : matches)
        match.start = start_positions.at_byte(match.start)+1;

    shared::Utf8PositionCursor end_positions(subject);
    for (shared::FixedRange& match : matches) {
        const int end = end_positions.at_byte(match.end);
        match.end = return_length ? end-match.start+1 : end;
    }
}


CHARR_R_HELPER void emit_warnings(
    bool recycling_warning, R_len_t empty_pattern_warnings
) noexcept
{
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);
    for (R_len_t i = 0; i < empty_pattern_warnings; ++i)
        Rf_warning(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
}

} // namespace search_fixed_locate

using namespace search_fixed_locate;


/**
 * Locate first occurrences of pattern in a string [fixed pattern]
 *
 * @param str character vector
 * @param pattern character vector
 * @return integer matrix (2 columns)
 *
 * @version 0.1-?? (Bartlomiej Tartanus)
 *
 * @version 0.1-?? (Bartlomiej Tartanus, 2013-06-09)
 *          io::Utf16Input & collator
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-23)
 *          use ci_locate_firstlast_fixed
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          ci_locate_fixed now uses byte search only
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-07)
 *    FR #110, #23: opts_fixed arg added
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29)
 *     get_length
 */
CHARR_ENTRYPOINT SEXP ci_locate_first_fixed(
    SEXP str, SEXP pattern, SEXP opts_fixed, SEXP get_length
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool return_length = ci__prepare_arg_logical_1_notNA_r(
        get_length, "get_length"
    );
    const shared::FixedSearchOptions options = fixed::prepare_options(
        opts_fixed, false
    );
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    pattern = entry_protections.protect_one(ci__prepare_arg_string_r(pattern, "pattern"));

    const R_len_t subject_length = LENGTH(str);
    const R_len_t pattern_length = LENGTH(pattern);
    bool recycling_warning = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, recycling_warning
    );

    R_len_t empty_pattern_warnings = 0;

    try {
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        std::vector<shared::StringView> subjects;
        std::vector<shared::StringView> patterns;
        shared::FixedMatcher matcher;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                result = entry_protections.reprotect_one(
                    Rf_allocMatrix(INTSXP, vectorize_length, 2),
                    result_index
                );
                int* output = INTEGER(result);
                if (vectorize_length > 0) {
                    R_len_t general_start = 0;
                    const bool direct = locate_first_ascii_scalar_direct(
                        str, pattern, options, pattern_length,
                        vectorize_length, return_length, output,
                        general_start
                    );
                    if (!direct) {
                        const SEXP* subject_values = STRING_PTR_RO(str);
                        subjects.resize(
                            static_cast<std::size_t>(subject_length)
                        );
                        for (R_len_t i = 0; i < subject_length; ++i) {
                            subjects[static_cast<std::size_t>(i)] =
                                shared::normalize_utf8(
                                    io::as_shared_view(subject_values[i]),
                                    subject_converter, subject_storage
                                );
                        }

                        const SEXP* pattern_values = STRING_PTR_RO(pattern);
                        patterns.resize(
                            static_cast<std::size_t>(pattern_length)
                        );
                        for (R_len_t i = 0; i < pattern_length; ++i) {
                            patterns[static_cast<std::size_t>(i)] =
                                shared::normalize_utf8(
                                    io::as_shared_view(pattern_values[i]),
                                    pattern_converter, pattern_storage
                                );
                        }
                        empty_pattern_warnings = count_empty_patterns(patterns);

                        locate_first_normalized(
                            subjects, patterns, vectorize_length,
                            general_start, options, return_length,
                            matcher, output
                        );
                    }
                }

                ci__locate_set_dimnames_matrix(result, return_length);
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_warnings(recycling_warning, empty_pattern_warnings);
    );
}


/** Locate all occurrences of fixed-byte pattern
 *
 * @param str character vector
 * @param pattern character vector
 * @return list of integer matrices (2 columns)
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-23)
 *          StriException friendly, use fixed::PatternSet
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-20)
 *          Use indexed UTF-8 input
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          ci_locate_fixed now uses byte search only
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-27)
 *    #117: omit_no_match arg added
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-07)
 *    #110, #23: opts_fixed arg added
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-02-14)
 *    use shared::ByteSearchMatcher
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29)
 *     get_length
 */
CHARR_ENTRYPOINT SEXP ci_locate_all_fixed(
    SEXP str, SEXP pattern, SEXP omit_no_match,
    SEXP opts_fixed, SEXP get_length
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const shared::FixedSearchOptions options = fixed::prepare_options(
        opts_fixed, true
    );
    const bool omit = ci__prepare_arg_logical_1_notNA_r(
        omit_no_match, "omit_no_match"
    );
    const bool return_length = ci__prepare_arg_logical_1_notNA_r(
        get_length, "get_length"
    );
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    pattern = entry_protections.protect_one(ci__prepare_arg_string_r(pattern, "pattern"));

    const R_len_t subject_length = LENGTH(str);
    const R_len_t pattern_length = LENGTH(pattern);
    bool recycling_warning = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, recycling_warning
    );

    R_len_t empty_pattern_warnings = 0;

    try {
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        std::vector<shared::StringView> subjects;
        std::vector<shared::StringView> patterns;
        shared::FixedMatcher matcher;
        std::vector<shared::FixedRange> matches;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                result = entry_protections.reprotect_one(
                    Rf_allocVector(VECSXP, vectorize_length), result_index
                );
                SEXP current = R_NilValue;
                PROTECT_INDEX current_index;
                callback_protections.protect_with_index(current, &current_index);

                if (vectorize_length > 0) {
                    R_len_t general_start = 0;
                    const bool direct = locate_all_ascii_scalar_direct(
                        str, pattern, options, pattern_length,
                        vectorize_length, omit, return_length, result,
                        callback_protections, current, current_index,
                        general_start
                    );
                    if (!direct) {
                        const SEXP* subject_values = STRING_PTR_RO(str);
                        subjects.resize(
                            static_cast<std::size_t>(subject_length)
                        );
                        for (R_len_t i = 0; i < subject_length; ++i) {
                            subjects[static_cast<std::size_t>(i)] =
                                shared::normalize_utf8(
                                    io::as_shared_view(subject_values[i]),
                                    subject_converter, subject_storage
                                );
                        }

                        const SEXP* pattern_values = STRING_PTR_RO(pattern);
                        patterns.resize(
                            static_cast<std::size_t>(pattern_length)
                        );
                        for (R_len_t i = 0; i < pattern_length; ++i) {
                            patterns[static_cast<std::size_t>(i)] =
                                shared::normalize_utf8(
                                    io::as_shared_view(pattern_values[i]),
                                    pattern_converter, pattern_storage
                                );
                        }
                        empty_pattern_warnings = count_empty_patterns(patterns);

                        for (R_len_t lane = 0;
                                lane < pattern_length; ++lane) {
                            const shared::StringView& current_pattern =
                                patterns[static_cast<std::size_t>(lane)];
                            R_len_t i = general_start > 0
                                ? general_start
                                : lane;
                            for (;;) {
                                const shared::StringView& current_subject =
                                    subjects[static_cast<std::size_t>(
                                        i % subject_length
                                    )];
                                if (current_subject.is_na() ||
                                        current_pattern.is_na() ||
                                        current_pattern.len <= 0) {
                                    current = callback_protections.reprotect_slot(
                                        shared::filled_integer_matrix_r(1, 2),
                                        current_index
                                    );
                                }
                                else if (current_subject.len <= 0) {
                                    current = callback_protections.reprotect_slot(
                                        shared::filled_integer_matrix_r(
                                            omit ? 0 : 1, 2,
                                            return_length ? -1 : NA_INTEGER
                                        ),
                                        current_index
                                    );
                                }
                                else {
                                    find_all_positions(
                                        current_subject, current_pattern,
                                        options, return_length,
                                        matcher, matches
                                    );
                                    const R_len_t match_count =
                                        static_cast<R_len_t>(matches.size());
                                    if (match_count <= 0) {
                                        current = callback_protections.reprotect_slot(
                                            shared::filled_integer_matrix_r(
                                                omit ? 0 : 1, 2,
                                                return_length
                                                    ? -1
                                                    : NA_INTEGER
                                            ),
                                            current_index
                                        );
                                    }
                                    else {
                                        current = callback_protections.reprotect_slot(
                                            Rf_allocMatrix(
                                                INTSXP, match_count, 2
                                            ),
                                            current_index
                                        );
                                        int* output = INTEGER(current);
                                        for (R_len_t j = 0;
                                                j < match_count; ++j) {
                                            const shared::FixedRange& match =
                                                matches[
                                                    static_cast<std::size_t>(j)
                                                ];
                                            output[j] = match.start;
                                            output[j+match_count] = match.end;
                                        }
                                    }
                                }
                                SET_VECTOR_ELT(result, i, current);

                                if (pattern_length >= vectorize_length-i)
                                    break;
                                i += pattern_length;
                            }
                        }
                    }
                }

                ci__locate_set_dimnames_list(result, return_length);
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_warnings(recycling_warning, empty_pattern_warnings);
    );
}

} } // namespace charr::base_backend
