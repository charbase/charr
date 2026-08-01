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
#include "collator/options.h"
#include "io/string_view.h"
#include "../shared/collation_search.h"
#include "../shared/collator.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/r_matrix.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <cstddef>
#include <exception>
#include <vector>

namespace charr { namespace base_backend {

namespace search_coll_locate {

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


CHARR_CXX_HELPER void require_icu_success(UErrorCode status)
{
    if (U_FAILURE(status))
        throw StriException(status);
}


CHARR_CXX_HELPER shared::StringView normalize_view(
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


CHARR_NEUTRAL_HELPER shared::CollationRange no_match_range(
    bool return_length
) noexcept
{
    const int value = return_length ? -1 : NA_INTEGER;
    return shared::CollationRange{value, value};
}


CHARR_CXX_HELPER void build_first_inputs(
    const shared::CollationInputs& subjects,
    const shared::CollationInputs& patterns,
    R_len_t vectorize_length,
    UCollator* collator,
    bool return_length,
    shared::CollationMatcher& matcher,
    int* output
)
{
    if (vectorize_length <= 0)
        return;

    const R_len_t subject_length = static_cast<R_len_t>(subjects.size());
    const R_len_t pattern_length = static_cast<R_len_t>(patterns.size());
    for (R_len_t lane = 0; lane < pattern_length; ++lane) {
        const shared::CollationInput pattern = patterns.get(
            static_cast<std::size_t>(lane)
        );
        R_len_t i = lane;
        for (;;) {
            const shared::CollationInput subject = subjects.get(
                static_cast<std::size_t>(i % subject_length)
            );
            shared::CollationRange current{0, 0};
            if (subject.missing || pattern.missing || pattern.length <= 0) {
                current = shared::CollationRange{
                    NA_INTEGER, NA_INTEGER
                };
            }
            else if (subject.length <= 0) {
                current = no_match_range(return_length);
            }
            else {
                UErrorCode status = U_ZERO_ERROR;
                shared::CollationRange match{0, 0};
                const bool found = matcher.find_first(
                    collator, subject, pattern, match, status
                );
                require_icu_success(status);
                if (!found) {
                    current = no_match_range(return_length);
                }
                else {
                    shared::CollationPositionCursor positions(subject);
                    current = positions.to_r_range(match, return_length);
                }
            }
            output[i] = current.start;
            output[i+vectorize_length] = current.end;

            if (pattern_length >= vectorize_length-i)
                break;
            i += pattern_length;
        }
    }
}


CHARR_CXX_HELPER void find_all_positions(
    const shared::CollationInput& subject,
    const shared::CollationInput& pattern,
    UCollator* collator,
    bool return_length,
    shared::CollationMatcher& matcher,
    std::vector<shared::CollationRange>& matches
)
{
    UErrorCode status = U_ZERO_ERROR;
    matcher.find_all(collator, subject, pattern, matches, status);
    require_icu_success(status);

    shared::CollationPositionCursor positions(subject);
    for (shared::CollationRange& match : matches)
        match = positions.to_r_range(match, return_length);
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

} // namespace search_coll_locate

using namespace search_coll_locate;


/** Locate the first collation-aware pattern occurrence. */
CHARR_ENTRYPOINT SEXP ci_locate_first_coll(
    SEXP str, SEXP pattern, SEXP opts_collator, SEXP get_length
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool return_length = ci__prepare_arg_logical_1_notNA_r(
        get_length, "get_length"
    );
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    pattern = entry_protections.protect_one(ci__prepare_arg_string_r(pattern, "pattern"));
    const shared::CollatorOptions options =
        collator::prepare_options(opts_collator);

    const R_len_t subject_length = LENGTH(str);
    const R_len_t pattern_length = LENGTH(pattern);
    bool recycling_needed = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, recycling_needed
    );

    bool root_fallback_warning = false;
    bool recycling_warning = false;
    R_len_t empty_pattern_warnings = 0;

    try {
        shared::Collator collator_owner;
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        shared::CollationInputs subjects;
        shared::CollationInputs patterns;
        shared::CollationMatcher matcher;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const shared::CollatorOpenResult opened =
                    collator_owner.reset(options);
                root_fallback_warning = opened.root_fallback;
                require_icu_success(opened.status);
                recycling_warning = recycling_needed;

                if (vectorize_length > 0) {
                    subjects.resize(
                        static_cast<std::size_t>(subject_length)
                    );
                    const SEXP* subject_values = STRING_PTR_RO(str);
                    for (R_len_t i = 0; i < subject_length; ++i) {
                        subjects.set(
                            static_cast<std::size_t>(i),
                            normalize_view(
                                io::as_shared_view(subject_values[i]),
                                subject_converter, subject_storage
                            )
                        );
                    }

                    patterns.resize(
                        static_cast<std::size_t>(pattern_length)
                    );
                    const SEXP* pattern_values = STRING_PTR_RO(pattern);
                    for (R_len_t i = 0; i < pattern_length; ++i) {
                        patterns.set(
                            static_cast<std::size_t>(i),
                            normalize_view(
                                io::as_shared_view(pattern_values[i]),
                                pattern_converter, pattern_storage
                            )
                        );
                    }
                    empty_pattern_warnings =
                        count_empty_patterns(patterns);
                }

                result = entry_protections.reprotect_one(
                    Rf_allocMatrix(INTSXP, vectorize_length, 2),
                    result_index
                );
                int* output = INTEGER(result);
                build_first_inputs(
                    subjects, patterns, vectorize_length,
                    collator_owner.get(), return_length,
                    matcher, output
                );
                ci__locate_set_dimnames_matrix(result, return_length);

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


/** Locate every collation-aware pattern occurrence. */
CHARR_ENTRYPOINT SEXP ci_locate_all_coll(
    SEXP str, SEXP pattern, SEXP omit_no_match,
    SEXP opts_collator, SEXP get_length
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool omit = ci__prepare_arg_logical_1_notNA_r(
        omit_no_match, "omit_no_match"
    );
    const bool return_length = ci__prepare_arg_logical_1_notNA_r(
        get_length, "get_length"
    );
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    pattern = entry_protections.protect_one(ci__prepare_arg_string_r(pattern, "pattern"));
    const shared::CollatorOptions options =
        collator::prepare_options(opts_collator);

    const R_len_t subject_length = LENGTH(str);
    const R_len_t pattern_length = LENGTH(pattern);
    bool recycling_needed = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, recycling_needed
    );

    bool root_fallback_warning = false;
    bool recycling_warning = false;
    R_len_t empty_pattern_warnings = 0;

    try {
        shared::Collator collator_owner;
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        shared::CollationInputs subjects;
        shared::CollationInputs patterns;
        shared::CollationMatcher matcher;
        std::vector<shared::CollationRange> matches;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const shared::CollatorOpenResult opened =
                    collator_owner.reset(options);
                root_fallback_warning = opened.root_fallback;
                require_icu_success(opened.status);
                recycling_warning = recycling_needed;

                if (vectorize_length > 0) {
                    subjects.resize(
                        static_cast<std::size_t>(subject_length)
                    );
                    const SEXP* subject_values = STRING_PTR_RO(str);
                    for (R_len_t i = 0; i < subject_length; ++i) {
                        subjects.set(
                            static_cast<std::size_t>(i),
                            normalize_view(
                                io::as_shared_view(subject_values[i]),
                                subject_converter, subject_storage
                            )
                        );
                    }

                    patterns.resize(
                        static_cast<std::size_t>(pattern_length)
                    );
                    const SEXP* pattern_values = STRING_PTR_RO(pattern);
                    for (R_len_t i = 0; i < pattern_length; ++i) {
                        patterns.set(
                            static_cast<std::size_t>(i),
                            normalize_view(
                                io::as_shared_view(pattern_values[i]),
                                pattern_converter, pattern_storage
                            )
                        );
                    }
                    empty_pattern_warnings =
                        count_empty_patterns(patterns);
                }

                result = entry_protections.reprotect_one(
                    Rf_allocVector(VECSXP, vectorize_length),
                    result_index
                );
                SEXP current = R_NilValue;
                PROTECT_INDEX current_index;
                callback_protections.protect_with_index(current, &current_index);

                for (R_len_t lane = 0;
                        lane < (vectorize_length > 0 ? pattern_length : 0);
                        ++lane) {
                    const shared::CollationInput prepared_pattern =
                        patterns.get(static_cast<std::size_t>(lane));
                    R_len_t i = lane;
                    for (;;) {
                        const shared::CollationInput subject = subjects.get(
                            static_cast<std::size_t>(i % subject_length)
                        );
                        if (subject.missing || prepared_pattern.missing ||
                                prepared_pattern.length <= 0) {
                            current = callback_protections.reprotect_slot(
                                shared::filled_integer_matrix_r(1, 2), current_index
                            );
                        }
                        else if (subject.length <= 0) {
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
                                subject, prepared_pattern,
                                collator_owner.get(), return_length,
                                matcher, matches
                            );
                            const R_len_t match_count =
                                static_cast<R_len_t>(matches.size());
                            if (match_count == 0) {
                                current = callback_protections.reprotect_slot(
                                    shared::filled_integer_matrix_r(
                                        omit ? 0 : 1, 2,
                                        return_length ? -1 : NA_INTEGER
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
                                    const shared::CollationRange& match =
                                        matches[static_cast<std::size_t>(j)];
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

                ci__locate_set_dimnames_list(result, return_length);
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

} } // namespace charr::base_backend
