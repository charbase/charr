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

#include <charport.h>

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <vector>

namespace charr { namespace altrep_backend {

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


CHARR_CXX_HELPER void stage_utf16(
    const charport::StrViews& source,
    shared::CollationInputs& output
)
{
    output.resize(static_cast<std::size_t>(source.size()));
    for (R_xlen_t i = 0; i < source.size(); ++i) {
        output.set(
            static_cast<std::size_t>(i),
            io::as_shared_view(source[i])
        );
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


CHARR_NEUTRAL_HELPER shared::CollationRange no_match_range(
    bool return_length
) noexcept
{
    const int value = return_length ? -1 : NA_INTEGER;
    return shared::CollationRange{value, value};
}


CHARR_CXX_HELPER void fill_first_inputs(
    const charport::StrViews& subjects,
    const shared::CollationInputs& patterns,
    R_len_t vectorize_length,
    UCollator* collator,
    bool return_length,
    shared::CollationCursor& subject_cursor,
    shared::CollationMatcher& matcher,
    int* starts
)
{
    if (vectorize_length <= 0)
        return;
    int* ends = starts+vectorize_length;

    const R_len_t subject_length = static_cast<R_len_t>(subjects.size());
    const R_len_t pattern_length = static_cast<R_len_t>(patterns.size());
    for (R_len_t lane = 0; lane < pattern_length; ++lane) {
        const shared::CollationInput pattern = patterns.get(
            static_cast<std::size_t>(lane)
        );
        R_len_t i = lane;
        for (;;) {
            const R_len_t raw_subject = i % subject_length;
            const shared::StringView source = io::as_shared_view(
                subjects[raw_subject]
            );
            if (source.is_na() || pattern.missing || pattern.length <= 0) {
                starts[i] = NA_INTEGER;
                ends[i] = NA_INTEGER;
            }
            else if (source.len <= 0) {
                const shared::CollationRange location =
                    no_match_range(return_length);
                starts[i] = location.start;
                ends[i] = location.end;
            }
            else {
                const shared::CollationInput subject = subject_cursor.get(
                    static_cast<const void*>(
                        subjects.ptrs() + raw_subject
                    ),
                    source
                );
                UErrorCode status = U_ZERO_ERROR;
                shared::CollationRange match{0, 0};
                const bool found = matcher.find_first(
                    collator, subject, pattern, match, status
                );
                require_icu_success(status);
                if (!found) {
                    const shared::CollationRange location =
                        no_match_range(return_length);
                    starts[i] = location.start;
                    ends[i] = location.end;
                }
                else {
                    shared::CollationPositionCursor positions(subject);
                    const shared::CollationRange location =
                        positions.to_r_range(match, return_length);
                    starts[i] = location.start;
                    ends[i] = location.end;
                }
            }

            if (pattern_length >= vectorize_length-i)
                break;
            i += pattern_length;
        }
    }
}


CHARR_CXX_HELPER bool fill_all_matches(
    const charport::StrViews& subjects,
    R_len_t raw_subject,
    const shared::CollationInput& pattern,
    UCollator* collator,
    bool return_length,
    shared::CollationCursor& subject_cursor,
    shared::CollationMatcher& matcher,
    std::vector<shared::CollationRange>& matches
)
{
    matches.clear();
    const shared::StringView source = io::as_shared_view(
        subjects[raw_subject]
    );
    const bool missing = source.is_na() || pattern.missing ||
        pattern.length <= 0;
    if (missing || source.len <= 0)
        return missing;

    const shared::CollationInput subject = subject_cursor.get(
        static_cast<const void*>(subjects.ptrs() + raw_subject),
        source
    );
    UErrorCode status = U_ZERO_ERROR;
    matcher.find_all(collator, subject, pattern, matches, status);
    require_icu_success(status);

    shared::CollationPositionCursor positions(subject);
    for (shared::CollationRange& match : matches) {
        match = positions.to_r_range(match, return_length);
    }
    return false;
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
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    pattern = entry_protections.protect_one(
        ci__prepare_arg_string_r(pattern, "pattern")
    );
    const shared::CollatorOptions options =
        collator::prepare_options(opts_collator);


    bool root_fallback_warning = false;
    bool recycling_warning = false;
    R_len_t empty_pattern_warnings = 0;

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

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t subject_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                const R_len_t pattern_length = io::checked_r_len(
                    XLENGTH(pattern), "character vectors"
                );
                bool pending_recycling_warning = false;
                const R_len_t vectorize_length = recycling_length(
                    subject_length, pattern_length,
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
                            "Reader length changed during collation location"
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
                            "Reader length changed during collation location"
                        );
                    }
                    pattern_views.resize(pattern_length);
                    pattern_reader.views(
                        0, pattern_length,
                        pattern_views.ptrs(), pattern_views.lengths(),
                        pattern_views.encodings()
                    );
                    normalize_views(
                        pattern_views, pattern_converter, pattern_storage
                    );
                    stage_utf16(pattern_views, patterns);
                    empty_pattern_warnings =
                        count_empty_patterns(patterns);
                }

                result = entry_protections.reprotect_one(
                    Rf_allocMatrix(INTSXP, vectorize_length, 2),
                    result_index
                );
                int* output = INTEGER(result);
                fill_first_inputs(
                    subject_views, patterns, vectorize_length,
                    collator_owner.get(), return_length,
                    subject_cursor, matcher, output
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
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    pattern = entry_protections.protect_one(
        ci__prepare_arg_string_r(pattern, "pattern")
    );
    const shared::CollatorOptions options =
        collator::prepare_options(opts_collator);


    bool root_fallback_warning = false;
    bool recycling_warning = false;
    R_len_t empty_pattern_warnings = 0;

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
        std::vector<shared::CollationRange> matches;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t subject_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                const R_len_t pattern_length = io::checked_r_len(
                    XLENGTH(pattern), "character vectors"
                );
                bool pending_recycling_warning = false;
                const R_len_t vectorize_length = recycling_length(
                    subject_length, pattern_length,
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
                            "Reader length changed during collation location"
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
                            "Reader length changed during collation location"
                        );
                    }
                    pattern_views.resize(pattern_length);
                    pattern_reader.views(
                        0, pattern_length,
                        pattern_views.ptrs(), pattern_views.lengths(),
                        pattern_views.encodings()
                    );
                    normalize_views(
                        pattern_views, pattern_converter, pattern_storage
                    );
                    stage_utf16(pattern_views, patterns);
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

                if (vectorize_length > 0) {
                    for (R_len_t lane = 0; lane < pattern_length; ++lane) {
                        const shared::CollationInput current_pattern =
                            patterns.get(static_cast<std::size_t>(lane));
                        R_len_t i = lane;
                        for (;;) {
                            const R_len_t raw_subject = i % subject_length;
                            const bool missing = fill_all_matches(
                                subject_views, raw_subject,
                                current_pattern, collator_owner.get(),
                                return_length, subject_cursor, matcher,
                                matches
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
                                        return_length ? -1 : NA_INTEGER
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
                                for (R_len_t j = 0; j < match_count; ++j) {
                                    const shared::CollationRange& match =
                                        matches[static_cast<std::size_t>(j)];
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

} } // namespace charr::altrep_backend
