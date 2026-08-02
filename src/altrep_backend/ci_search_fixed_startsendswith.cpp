
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
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <charport.h>

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <vector>

namespace charr { namespace altrep_backend {


namespace search_fixed_startsendswith {

CHARR_NEUTRAL_HELPER bool has_utf8_bom(
    const char* data, int length
) noexcept
{
    return length >= 3 &&
        static_cast<unsigned char>(data[0]) == 0xef &&
        static_cast<unsigned char>(data[1]) == 0xbb &&
        static_cast<unsigned char>(data[2]) == 0xbf;
}


CHARR_NEUTRAL_HELPER bool direct_utf8_view(
    const charport::StrView& source, shared::StringView& output
) noexcept
{
    if (source.is_na() || source.ptr == nullptr || source.len < 0)
        return false;

    const char* data = source.ptr;
    int length = source.len;
    shared::StringEncoding encoding;

    if (source.enc == CETYPE_EXT_ASCII) {
        encoding = shared::StringEncoding::ascii;
    }
    else if (source.enc == CETYPE_EXT_UTF8 ||
            source.enc == CETYPE_EXT_ASCII_OR_UTF8) {
        const bool strip_bom = has_utf8_bom(data, length);
        if (strip_bom) {
            data += 3;
            length -= 3;
        }
        encoding = strip_bom
            ? shared::StringEncoding::utf8
            : source.enc == CETYPE_EXT_UTF8
                ? shared::StringEncoding::utf8
                : shared::StringEncoding::ascii_or_utf8;
    }
    else {
        return false;
    }

    output = shared::StringView{data, length, encoding};
    return true;
}


CHARR_NEUTRAL_HELPER R_len_t recycling_length(
    R_len_t first, R_len_t second, R_len_t third, bool& warning
) noexcept
{
    warning = false;
    if (first <= 0 || second <= 0 || third <= 0)
        return 0;

    R_len_t result = first > second ? first : second;
    if (third > result)
        result = third;
    warning = result % first != 0 || result % second != 0 ||
        result % third != 0;
    return result;
}


CHARR_NEUTRAL_HELPER bool direct_scalar_default(
    const charport::StrViews& subjects,
    const charport::StrViews& patterns,
    R_len_t vectorize_length,
    shared::FixedSearchOptions options,
    bool starts,
    bool negate,
    int* result,
    R_len_t& general_start
) noexcept
{
    if (options.case_insensitive || options.overlap)
        return false;

    shared::StringView pattern;
    if (!direct_utf8_view(patterns[0], pattern) || pattern.len <= 0)
        return false;

    for (R_len_t i = 0; i < vectorize_length; ++i) {
        const charport::StrView source = subjects[i];
        if (source.is_na()) {
            result[i] = NA_LOGICAL;
            continue;
        }

        shared::StringView subject;
        if (!direct_utf8_view(source, subject)) {
            general_start = i;
            return false;
        }

        const bool matched = starts
            ? shared::fixed_starts_with(subject, 0, pattern, false)
            : shared::fixed_ends_with(
                subject, subject.len, pattern, false
            );
        result[i] = static_cast<int>(matched != negate);
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


CHARR_NEUTRAL_HELPER void match_general(
    const std::vector<shared::StringView>& subjects,
    const std::vector<shared::StringView>& patterns,
    const int* positions,
    R_len_t position_length,
    R_len_t vectorize_length,
    R_len_t general_start,
    bool starts,
    bool case_insensitive,
    bool negate,
    int* result
) noexcept
{
    const R_len_t subject_length = static_cast<R_len_t>(subjects.size());
    const R_len_t pattern_length = static_cast<R_len_t>(patterns.size());

    for (R_len_t lane = 0; lane < pattern_length; ++lane) {
        const shared::StringView& pattern = patterns[
            static_cast<std::size_t>(lane)
        ];
        R_len_t i = general_start > 0 ? general_start : lane;

        for (;;) {
            const shared::StringView& subject = subjects[
                static_cast<std::size_t>(i % subject_length)
            ];

            if (subject.is_na() || pattern.is_na() || pattern.len <= 0) {
                result[i] = NA_LOGICAL;
            }
            else if (subject.len <= 0) {
                result[i] = negate;
            }
            else {
                const int position = positions[i % position_length];
                if (position == NA_INTEGER) {
                    result[i] = NA_LOGICAL;
                }
                else {
                    int byte_index;
                    if (starts) {
                        if (position == 1) {
                            byte_index = 0;
                        }
                        else if (position >= 0) {
                            byte_index = shared::utf8_index_forward(
                                subject, position-1
                            );
                        }
                        else {
                            byte_index = shared::utf8_index_backward(
                                subject, -position
                            );
                        }
                    }
                    else {
                        if (position == -1) {
                            byte_index = subject.len;
                        }
                        else if (position >= 0) {
                            byte_index = shared::utf8_index_forward(
                                subject, position
                            );
                        }
                        else {
                            byte_index = shared::utf8_index_backward(
                                subject, -position-1
                            );
                        }
                    }

                    const bool matched = starts
                        ? shared::fixed_starts_with(
                            subject, byte_index, pattern, case_insensitive
                        )
                        : shared::fixed_ends_with(
                            subject, byte_index, pattern, case_insensitive
                        );
                    result[i] = static_cast<int>(matched != negate);
                }
            }

            if (pattern_length >= vectorize_length-i)
                break;
            i += pattern_length;
        }
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

} // namespace search_fixed_startsendswith

using namespace search_fixed_startsendswith;

/**
 * Detect if a string starts with a pattern match
 *
 * @param str character vector
 * @param pattern character vector
 * @param from integer vector
 * @return logical vector
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-06-03)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-07)
 *    FR #110, #23: opts_fixed arg added;
 *    use fixed::PatternSet::startsWith() and endsWith()
 *
 * @version 1.4.7 (Marek Gagolewski, 2020-08-24)
 *    #345: `negate` arg added
 */
CHARR_ENTRYPOINT SEXP ci_startswith_fixed(
    SEXP str, SEXP pattern, SEXP from,
    SEXP negate, SEXP opts_fixed
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool negate_1 = ci__prepare_arg_logical_1_notNA_r(
        negate, "negate"
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
    from = entry_protections.protect_one(
        ci__prepare_arg_integer_r(from, "from")
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

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t subject_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                const R_len_t pattern_length = io::checked_r_len(
                    XLENGTH(pattern), "character vectors"
                );
                const R_len_t position_length = io::checked_r_len(
                    XLENGTH(from), "integer vectors"
                );
                const R_len_t vectorize_length = recycling_length(
                    subject_length, pattern_length, position_length,
                    recycling_warning
                );

                result = entry_protections.reprotect_one(
                    Rf_allocVector(LGLSXP, vectorize_length), result_index
                );
                int* output = LOGICAL(result);

                if (vectorize_length > 0) {
                    const int* positions = INTEGER_RO(from);

                    subject_reader.reset(str);
                    if (subject_reader.size() != subject_length) {
                        throw std::runtime_error(
                            "Reader length changed during fixed starts-with"
                        );
                    }
                    pattern_reader.reset(pattern);
                    if (pattern_reader.size() != pattern_length) {
                        throw std::runtime_error(
                            "Reader length changed during fixed starts-with"
                        );
                    }

                    subject_views.resize(subject_length);
                    subject_reader.views(
                        0, subject_length,
                        subject_views.ptrs(), subject_views.lengths(),
                        subject_views.encodings()
                    );
                    pattern_views.resize(pattern_length);
                    pattern_reader.views(
                        0, pattern_length,
                        pattern_views.ptrs(), pattern_views.lengths(),
                        pattern_views.encodings()
                    );

                    const bool direct =
                        subject_length == vectorize_length &&
                        pattern_length == 1 && position_length == 1 &&
                        positions[0] == 1 &&
                        direct_scalar_default(
                            subject_views, pattern_views, vectorize_length,
                            options, true, negate_1, output, general_start
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
                        match_general(
                            subjects, patterns, positions, position_length,
                            vectorize_length, general_start, true,
                            options.case_insensitive, negate_1, output
                        );
                    }
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_warnings(recycling_warning, empty_pattern_warnings);
    );
}


/**
 * Detect if a string ends with a pattern match
 *
 * @param str character vector
 * @param pattern character vector
 * @param to integer vector
 * @return logical vector
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-06-03)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-07)
 *    FR #110, #23: opts_fixed arg added
 *
 * @version 1.4.7 (Marek Gagolewski, 2020-08-24)
 *    #345: `negate` arg added
 */
CHARR_ENTRYPOINT SEXP ci_endswith_fixed(
    SEXP str, SEXP pattern, SEXP to,
    SEXP negate, SEXP opts_fixed
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool negate_1 = ci__prepare_arg_logical_1_notNA_r(
        negate, "negate"
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
    to = entry_protections.protect_one(
        ci__prepare_arg_integer_r(to, "to")
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

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t subject_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                const R_len_t pattern_length = io::checked_r_len(
                    XLENGTH(pattern), "character vectors"
                );
                const R_len_t position_length = io::checked_r_len(
                    XLENGTH(to), "integer vectors"
                );
                const R_len_t vectorize_length = recycling_length(
                    subject_length, pattern_length, position_length,
                    recycling_warning
                );

                result = entry_protections.reprotect_one(
                    Rf_allocVector(LGLSXP, vectorize_length), result_index
                );
                int* output = LOGICAL(result);

                if (vectorize_length > 0) {
                    const int* positions = INTEGER_RO(to);

                    subject_reader.reset(str);
                    if (subject_reader.size() != subject_length) {
                        throw std::runtime_error(
                            "Reader length changed during fixed ends-with"
                        );
                    }
                    pattern_reader.reset(pattern);
                    if (pattern_reader.size() != pattern_length) {
                        throw std::runtime_error(
                            "Reader length changed during fixed ends-with"
                        );
                    }

                    subject_views.resize(subject_length);
                    subject_reader.views(
                        0, subject_length,
                        subject_views.ptrs(), subject_views.lengths(),
                        subject_views.encodings()
                    );
                    pattern_views.resize(pattern_length);
                    pattern_reader.views(
                        0, pattern_length,
                        pattern_views.ptrs(), pattern_views.lengths(),
                        pattern_views.encodings()
                    );

                    const bool direct =
                        subject_length == vectorize_length &&
                        pattern_length == 1 && position_length == 1 &&
                        positions[0] == -1 &&
                        direct_scalar_default(
                            subject_views, pattern_views, vectorize_length,
                            options, false, negate_1, output, general_start
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
                        match_general(
                            subjects, patterns, positions, position_length,
                            vectorize_length, general_start, false,
                            options.case_insensitive, negate_1, output
                        );
                    }
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_warnings(recycling_warning, empty_pattern_warnings);
    );
}

} } // namespace charr::altrep_backend
