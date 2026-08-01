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

namespace search_regex_count {

enum class SubjectMode : unsigned char {
    direct,
    normalize,
    bytes
};


CHARR_NEUTRAL_HELPER R_len_t recycling_length(
    R_len_t subject_length,
    R_len_t pattern_length,
    bool& warning
) noexcept {
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


CHARR_R_HELPER SubjectMode subject_mode(
    const SEXP* values, R_len_t size
) noexcept {
    for (R_len_t i = 0; i < size; ++i) {
        const SEXP value = values[i];
        if (value == NA_STRING)
            continue;
        if (IS_BYTES(value))
            return SubjectMode::bytes;
        if (!IS_ASCII(value) && !IS_UTF8(value))
            return SubjectMode::normalize;
    }
    return SubjectMode::direct;
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


CHARR_R_HELPER void emit_empty_pattern_warnings(int count) noexcept
{
    for (int i = 0; i < count; ++i)
        Rf_warning(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
}

} // namespace search_regex_count

using namespace search_regex_count;


/**
 * Count the number of recurrences of \code{pattern} in \code{s}
 *
 * @param str strings to search in
 * @param pattern regex patterns to search for
 * @param opts_regex list
 *
 * @return integer vector
 */
CHARR_ENTRYPOINT SEXP ci_count_regex(
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
        std::vector<shared::StringView> normalized_subjects;
        shared::RegexPatterns patterns;
        shared::RegexMatcher matcher(options);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                result = entry_protections.reprotect_one(
                    Rf_allocVector(INTSXP, vectorize_length), result_index
                );
                int* output = INTEGER(result);

                if (vectorize_length > 0) {
                    patterns.resize(
                        static_cast<std::size_t>(pattern_length)
                    );
                    const SEXP* pattern_values = STRING_PTR_RO(pattern);
                    for (R_len_t i = 0; i < pattern_length; ++i) {
                        const shared::StringView value =
                            shared::normalize_utf8_preserve_bom(
                                io::as_shared_view(pattern_values[i]),
                                pattern_converter, pattern_storage
                            );
                        patterns.set(static_cast<std::size_t>(i), value);
                    }
                    const SEXP* subject_values = STRING_PTR_RO(str);
                    const SubjectMode mode = subject_mode(
                        subject_values, subject_length
                    );
                    if (mode == SubjectMode::bytes)
                        throw StriException(MSG__BYTESENC);

                    if (mode == SubjectMode::normalize) {
                        normalized_subjects.resize(
                            static_cast<std::size_t>(subject_length)
                        );
                        for (R_len_t i = 0; i < subject_length; ++i) {
                            normalized_subjects[
                                static_cast<std::size_t>(i)
                            ] = shared::normalize_utf8_preserve_bom(
                                io::as_shared_view(subject_values[i]),
                                subject_converter, subject_storage
                            );
                        }
                    }

                    empty_pattern_warnings = patterns.empty_count();

                    if (pattern_length == 1) {
                        const std::size_t pattern_index = 0;
                        const shared::RegexInput current_pattern =
                            patterns.get(pattern_index);
                        const bool pattern_unusable =
                            current_pattern.missing ||
                            current_pattern.length <= 0;
                        bool pattern_bound = false;

                        if (mode == SubjectMode::direct) {
                            for (R_len_t i = 0;
                                    i < vectorize_length; ++i) {
                                const SEXP identity = subject_values[i];
                                const shared::StringView current =
                                    io::as_direct_utf8_view(identity);
                                if (current.is_na() || pattern_unusable) {
                                    output[i] = NA_INTEGER;
                                    continue;
                                }

                                if (!pattern_bound) {
                                    bind_pattern(
                                        matcher, current_pattern, patterns,
                                        pattern_index
                                    );
                                    pattern_bound = true;
                                }
                                UErrorCode status = U_ZERO_ERROR;
                                output[i] = matcher.count(
                                    current, identity, status
                                );
                                if (U_FAILURE(status)) {
                                    throw_regex_error(
                                        status, false,
                                        patterns, pattern_index
                                    );
                                }
                            }
                        }
                        else {
                            for (R_len_t i = 0;
                                    i < vectorize_length; ++i) {
                                const shared::StringView& current =
                                    normalized_subjects[
                                        static_cast<std::size_t>(i)
                                    ];
                                if (current.is_na() || pattern_unusable) {
                                    output[i] = NA_INTEGER;
                                    continue;
                                }

                                if (!pattern_bound) {
                                    bind_pattern(
                                        matcher, current_pattern, patterns,
                                        pattern_index
                                    );
                                    pattern_bound = true;
                                }
                                UErrorCode status = U_ZERO_ERROR;
                                output[i] = matcher.count(
                                    current, &current, status
                                );
                                if (U_FAILURE(status)) {
                                    throw_regex_error(
                                        status, false,
                                        patterns, pattern_index
                                    );
                                }
                            }
                        }
                    }
                    else {
                        for (R_len_t lane = 0;
                                lane < pattern_length; ++lane) {
                            const std::size_t pattern_index =
                                static_cast<std::size_t>(lane);
                            const shared::RegexInput current_pattern =
                                patterns.get(pattern_index);
                            bool pattern_bound = false;

                            R_len_t i = lane;
                            for (;;) {
                                const R_len_t subject_index =
                                    i % subject_length;
                                shared::StringView current;
                                const void* identity;
                                if (mode == SubjectMode::direct) {
                                    const SEXP value = subject_values[
                                        subject_index
                                    ];
                                    current = io::as_direct_utf8_view(value);
                                    identity = value;
                                }
                                else {
                                    const shared::StringView& value =
                                        normalized_subjects[
                                            static_cast<std::size_t>(
                                                subject_index
                                            )
                                        ];
                                    current = value;
                                    identity = &value;
                                }

                                if (current.is_na() ||
                                        current_pattern.missing ||
                                        current_pattern.length <= 0) {
                                    output[i] = NA_INTEGER;
                                }
                                else {
                                    if (!pattern_bound) {
                                        bind_pattern(
                                            matcher, current_pattern,
                                            patterns, pattern_index
                                        );
                                        pattern_bound = true;
                                    }
                                    UErrorCode status = U_ZERO_ERROR;
                                    output[i] = matcher.count(
                                        current, identity, status
                                    );
                                    if (U_FAILURE(status)) {
                                        throw_regex_error(
                                            status, false,
                                            patterns, pattern_index
                                        );
                                    }
                                }

                                if (pattern_length >= vectorize_length-i)
                                    break;
                                i += pattern_length;
                            }
                        }
                    }
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_empty_pattern_warnings(empty_pattern_warnings);
    );
}

} } // namespace charr::base_backend
