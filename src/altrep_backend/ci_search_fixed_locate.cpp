
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

#include <charport.h>

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <vector>

namespace charr { namespace altrep_backend {


namespace search_fixed_locate {

struct DirectFixedString {
    const char* data;
    R_len_t length;
    bool is_na;
    bool is_ascii;
};


CHARR_NEUTRAL_HELPER bool direct_ascii_encoding(
    shared::StringEncoding encoding
) noexcept
{
    return encoding == shared::StringEncoding::ascii ||
        encoding == shared::StringEncoding::utf8 ||
        encoding == shared::StringEncoding::ascii_or_utf8;
}


CHARR_NEUTRAL_HELPER bool has_utf8_bom(
    const char* data, int length
) noexcept
{
    return length >= 3 &&
        static_cast<unsigned char>(data[0]) == 0xefU &&
        static_cast<unsigned char>(data[1]) == 0xbbU &&
        static_cast<unsigned char>(data[2]) == 0xbfU;
}


CHARR_NEUTRAL_HELPER bool direct_fixed_string(
    const charport::StrView& value, DirectFixedString& output
) noexcept
{
    if (value.is_na()) {
        output.data = NULL;
        output.length = 0;
        output.is_na = true;
        output.is_ascii = false;
        return true;
    }

    const shared::StringView source = io::as_shared_view(value);
    if (!direct_ascii_encoding(source.enc) || source.len < 0 ||
            (source.ptr == nullptr && source.len > 0)) {
        return false;
    }

    output.data = source.ptr;
    output.length = source.len;
    output.is_na = false;
    output.is_ascii = source.enc == shared::StringEncoding::ascii;

    if (!output.is_ascii && has_utf8_bom(output.data, output.length)) {
        output.data += 3;
        output.length -= 3;
    }

    return true;
}


CHARR_NEUTRAL_HELPER bool direct_fixed_pattern(
    const charport::StrView& pattern, unsigned char& pattern_byte
) noexcept
{
    DirectFixedString value;
    if (!direct_fixed_string(pattern, value) ||
            value.is_na || value.length != 1)
        return false;

    pattern_byte = static_cast<unsigned char>(value.data[0]);
    return pattern_byte <= 0x7f;
}


CHARR_NEUTRAL_HELPER R_len_t count_fixed_byte(
    const char* data, R_len_t length, unsigned char pattern_byte
) noexcept
{
    if (length <= 0)
        return 0;

    R_len_t count = 0;
    const unsigned char* current =
        reinterpret_cast<const unsigned char*>(data);
    const unsigned char* end = current + length;
    for (; current != end; ++current)
        count += (*current == pattern_byte);
    return count;
}


CHARR_NEUTRAL_HELPER R_len_t first_fixed_byte(
    const char* data, R_len_t length, unsigned char pattern_byte
) noexcept
{
    for (R_len_t i = 0; i < length; ++i) {
        if (static_cast<unsigned char>(data[i]) == pattern_byte)
            return i;
    }
    return -1;
}


CHARR_NEUTRAL_HELPER R_len_t fixed_codepoint_position(
    const DirectFixedString& value, R_len_t byte_position
) noexcept
{
    if (value.is_ascii)
        return byte_position + 1;

    R_len_t current = 0;
    R_len_t codepoint = 1;
    while (current < byte_position) {
        U8_FWD_1(
            reinterpret_cast<const uint8_t*>(value.data),
            current, value.length
        );
        ++codepoint;
    }
    return codepoint;
}


CHARR_NEUTRAL_HELPER void fill_fixed_byte_occurrences(
    const DirectFixedString& value, unsigned char pattern_byte,
    int* start, int* end, R_len_t occurrence_count, bool get_length
) noexcept
{
    R_len_t occurrence = 0;
    if (value.is_ascii) {
        for (R_len_t i = 0; i < value.length; ++i) {
            if (static_cast<unsigned char>(value.data[i]) != pattern_byte)
                continue;
            start[occurrence] = i + 1;
            end[occurrence] = get_length ? 1 : i + 1;
            ++occurrence;
        }
        return;
    }

    R_len_t current = 0;
    R_len_t codepoint = 1;
    while (current < value.length && occurrence < occurrence_count) {
        if (static_cast<unsigned char>(value.data[current]) == pattern_byte) {
            start[occurrence] = codepoint;
            end[occurrence] = get_length ? 1 : codepoint;
            ++occurrence;
        }
        U8_FWD_1(
            reinterpret_cast<const uint8_t*>(value.data),
            current, value.length
        );
        ++codepoint;
    }
}


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


CHARR_NEUTRAL_HELPER bool locate_first_ascii_scalar_direct(
    const charport::StrViews& subjects,
    const charport::StrViews& patterns,
    R_len_t pattern_length,
    R_len_t vectorize_length,
    shared::FixedSearchOptions options,
    bool return_length,
    int* result, R_len_t& general_start
) noexcept
{
    if (options.case_insensitive || options.overlap || pattern_length != 1)
        return false;

    unsigned char pattern_byte;
    if (!direct_fixed_pattern(patterns[0], pattern_byte))
        return false;

    for (R_len_t i = 0; i < vectorize_length; ++i) {
        int& start_result = result[i];
        int& end_result = result[i+vectorize_length];
        start_result = NA_INTEGER;
        end_result = NA_INTEGER;

        DirectFixedString value;
        if (!direct_fixed_string(subjects[i], value)) {
            general_start = i;
            return false;
        }
        if (value.is_na)
            continue;

        const R_len_t byte_position = first_fixed_byte(
            value.data, value.length, pattern_byte
        );
        if (byte_position < 0) {
            if (return_length)
                start_result = end_result = -1;
            continue;
        }

        start_result = fixed_codepoint_position(value, byte_position);
        end_result = return_length ? 1 : start_result;
    }

    return true;
}


CHARR_R_HELPER bool locate_all_ascii_scalar_direct(
    const charport::StrViews& subjects,
    const charport::StrViews& patterns,
    R_len_t pattern_length,
    R_len_t vectorize_length,
    shared::FixedSearchOptions options,
    bool omit_no_match,
    bool return_length,
    shared::ProtHelper& protections,
    PROTECT_INDEX current_index,
    SEXP& current,
    SEXP result,
    R_len_t& general_start
) noexcept
{
    if (options.case_insensitive || options.overlap || pattern_length != 1)
        return false;

    unsigned char pattern_byte;
    if (!direct_fixed_pattern(patterns[0], pattern_byte))
        return false;

    for (R_len_t i = 0; i < vectorize_length; ++i) {
        DirectFixedString value;
        if (!direct_fixed_string(subjects[i], value)) {
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

        const R_len_t occurrence_count = count_fixed_byte(
            value.data, value.length, pattern_byte
        );
        if (occurrence_count == 0) {
            current = protections.reprotect_slot(
                shared::filled_integer_matrix_r(
                    omit_no_match ? 0 : 1, 2,
                    return_length ? -1 : NA_INTEGER
                ),
                current_index
            );
            SET_VECTOR_ELT(result, i, current);
            continue;
        }

        current = protections.reprotect_slot(
            Rf_allocMatrix(INTSXP, occurrence_count, 2),
            current_index
        );
        int* output = INTEGER(current);
        fill_fixed_byte_occurrences(
            value, pattern_byte, output,
            output+occurrence_count, occurrence_count, return_length
        );
        SET_VECTOR_ELT(result, i, current);
    }

    return true;
}


CHARR_CXX_HELPER void normalize_views(
    const charport::StrViews& source,
    R_len_t source_length,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    std::vector<shared::StringView>& output
)
{
    output.resize(static_cast<std::size_t>(source_length));
    for (R_len_t i = 0; i < source_length; ++i) {
        output[static_cast<std::size_t>(i)] = shared::normalize_utf8(
            io::as_shared_view(source[i]), converter, storage
        );
    }
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


CHARR_CXX_HELPER void locate_first_general(
    const std::vector<shared::StringView>& subjects,
    const std::vector<shared::StringView>& patterns,
    R_len_t vectorize_length,
    R_len_t general_start,
    shared::FixedSearchOptions options,
    bool return_length,
    shared::FixedMatcher& matcher,
    int* starts
)
{
    if (vectorize_length <= 0)
        return;
    int* ends = starts+vectorize_length;

    const R_len_t subject_length = static_cast<R_len_t>(subjects.size());
    const R_len_t pattern_length = static_cast<R_len_t>(patterns.size());
    if (general_start > 0 && pattern_length != 1) {
        throw std::logic_error(
            "fixed-locate direct prefix requires a scalar pattern"
        );
    }

    for (R_len_t lane = 0; lane < pattern_length; ++lane) {
        const shared::StringView& pattern =
            patterns[static_cast<std::size_t>(lane)];
        const R_len_t first = general_start > 0 ? general_start : lane;
        R_len_t i = first;
        for (;;) {
            const shared::StringView& subject = subjects[
                static_cast<std::size_t>(i % subject_length)
            ];

            if (subject.is_na() || pattern.is_na() || pattern.len <= 0) {
                starts[i] = NA_INTEGER;
                ends[i] = NA_INTEGER;
            }
            else if (subject.len <= 0) {
                const shared::FixedRange location =
                    no_match_range(return_length);
                starts[i] = location.start;
                ends[i] = location.end;
            }
            else {
                shared::FixedRange match{0, 0};
                if (!matcher.find_first(subject, pattern, options, match)) {
                    const shared::FixedRange location =
                        no_match_range(return_length);
                    starts[i] = location.start;
                    ends[i] = location.end;
                }
                else {
                    shared::Utf8PositionCursor positions(subject);
                    const int start = positions.at_byte(match.start)+1;
                    const int end = positions.at_byte(match.end);
                    starts[i] = start;
                    ends[i] = return_length ? end-start+1 : end;
                }
            }

            if (pattern_length >= vectorize_length-i)
                break;
            i += pattern_length;
        }
    }
}


CHARR_CXX_HELPER bool fill_all_matches(
    const shared::StringView& subject,
    const shared::StringView& pattern,
    shared::FixedSearchOptions options,
    bool return_length,
    shared::FixedMatcher& matcher,
    std::vector<shared::FixedRange>& matches
)
{
    matches.clear();
    const bool missing = subject.is_na() || pattern.is_na() ||
        pattern.len <= 0;
    if (missing || subject.len <= 0)
        return missing;

    matcher.find_all(subject, pattern, options, matches);

    shared::Utf8PositionCursor starts(subject);
    for (shared::FixedRange& match : matches)
        match.start = starts.at_byte(match.start)+1;

    shared::Utf8PositionCursor ends(subject);
    for (shared::FixedRange& match : matches) {
        const int end = ends.at_byte(match.end);
        match.end = return_length ? end-match.start+1 : end;
    }
    return false;
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
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    pattern = entry_protections.protect_one(
        ci__prepare_arg_string_r(pattern, "pattern")
    );


    bool recycling_warning = false;
    R_len_t empty_pattern_warnings = 0;
    R_len_t general_start = 0;

    try {
        charport::Reader subject_reader;
        charport::Reader pattern_reader;
        charport::StrViews subject_views;
        charport::StrViews pattern_views;
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
                const R_len_t subject_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                const R_len_t pattern_length = io::checked_r_len(
                    XLENGTH(pattern), "character vectors"
                );
                const R_len_t vectorize_length = recycling_length(
                    subject_length, pattern_length, recycling_warning
                );

                result = entry_protections.reprotect_one(
                    Rf_allocMatrix(INTSXP, vectorize_length, 2),
                    result_index
                );
                int* output = INTEGER(result);

                if (vectorize_length > 0) {
                    subject_reader.reset(str);
                    if (subject_reader.size() != subject_length) {
                        throw std::runtime_error(
                            "Reader length changed during fixed location"
                        );
                    }
                    subject_views.resize(subject_length);
                    subject_reader.views(
                        0, subject_length,
                        subject_views.ptrs(), subject_views.lengths(),
                        subject_views.encodings()
                    );

                    pattern_reader.reset(pattern);
                    if (pattern_reader.size() != pattern_length) {
                        throw std::runtime_error(
                            "Reader length changed during fixed location"
                        );
                    }
                    pattern_views.resize(pattern_length);
                    pattern_reader.views(
                        0, pattern_length,
                        pattern_views.ptrs(), pattern_views.lengths(),
                        pattern_views.encodings()
                    );

                    if (!locate_first_ascii_scalar_direct(
                            subject_views, pattern_views, pattern_length,
                            vectorize_length, options, return_length,
                            output, general_start
                    )) {
                        normalize_views(
                            subject_views, subject_length,
                            subject_converter, subject_storage, subjects
                        );
                        normalize_views(
                            pattern_views, pattern_length,
                            pattern_converter, pattern_storage, patterns
                        );
                        empty_pattern_warnings = count_empty_patterns(patterns);
                        locate_first_general(
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
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    pattern = entry_protections.protect_one(
        ci__prepare_arg_string_r(pattern, "pattern")
    );


    bool recycling_warning = false;
    R_len_t empty_pattern_warnings = 0;
    R_len_t general_start = 0;

    try {
        charport::Reader subject_reader;
        charport::Reader pattern_reader;
        charport::StrViews subject_views;
        charport::StrViews pattern_views;
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
                const R_len_t subject_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                const R_len_t pattern_length = io::checked_r_len(
                    XLENGTH(pattern), "character vectors"
                );
                const R_len_t vectorize_length = recycling_length(
                    subject_length, pattern_length, recycling_warning
                );

                result = entry_protections.reprotect_one(
                    Rf_allocVector(VECSXP, vectorize_length), result_index
                );
                SEXP current = R_NilValue;
                PROTECT_INDEX current_index;
                callback_protections.protect_with_index(current, &current_index);

                if (vectorize_length > 0) {
                    subject_reader.reset(str);
                    if (subject_reader.size() != subject_length) {
                        throw std::runtime_error(
                            "Reader length changed during fixed location"
                        );
                    }
                    subject_views.resize(subject_length);
                    subject_reader.views(
                        0, subject_length,
                        subject_views.ptrs(), subject_views.lengths(),
                        subject_views.encodings()
                    );

                    pattern_reader.reset(pattern);
                    if (pattern_reader.size() != pattern_length) {
                        throw std::runtime_error(
                            "Reader length changed during fixed location"
                        );
                    }
                    pattern_views.resize(pattern_length);
                    pattern_reader.views(
                        0, pattern_length,
                        pattern_views.ptrs(), pattern_views.lengths(),
                        pattern_views.encodings()
                    );

                    const bool direct = locate_all_ascii_scalar_direct(
                        subject_views, pattern_views, pattern_length,
                        vectorize_length, options, omit, return_length,
                        callback_protections, current_index,
                        current, result,
                        general_start
                    );

                    if (!direct) {
                        normalize_views(
                            subject_views, subject_length,
                            subject_converter, subject_storage, subjects
                        );
                        normalize_views(
                            pattern_views, pattern_length,
                            pattern_converter, pattern_storage, patterns
                        );
                        empty_pattern_warnings = count_empty_patterns(patterns);
                        if (general_start > 0 && pattern_length != 1) {
                            throw std::logic_error(
                                "fixed-locate direct prefix requires a "
                                "scalar pattern"
                            );
                        }

                        for (R_len_t lane = 0;
                                lane < pattern_length; ++lane) {
                            const shared::StringView& current_pattern =
                                patterns[static_cast<std::size_t>(lane)];
                            R_len_t i = general_start > 0
                                ? general_start
                                : lane;
                            for (;;) {
                                const shared::StringView& subject = subjects[
                                    static_cast<std::size_t>(
                                        i % subject_length
                                    )
                                ];
                                const bool missing = fill_all_matches(
                                    subject, current_pattern, options,
                                    return_length, matcher, matches
                                );

                                if (missing) {
                                    current = callback_protections.reprotect_slot(
                                        shared::filled_integer_matrix_r(1, 2),
                                        current_index
                                    );
                                }
                                else if (matches.size() == 0) {
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
                                    const R_len_t match_count =
                                        static_cast<R_len_t>(matches.size());
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

} } // namespace charr::altrep_backend
