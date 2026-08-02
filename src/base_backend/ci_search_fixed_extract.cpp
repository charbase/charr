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
#include "fixed/options.h"
#include "io/string_view.h"
#include "../shared/entrypoint.h"
#include "../shared/fixed_search.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <unordered_map>
#include <vector>


namespace charr { namespace base_backend {

namespace search_fixed_extract {

struct DirectRepeatKey {
    SEXP pattern;
    R_len_t count;

    CHARR_NEUTRAL_HELPER bool operator==(
        const DirectRepeatKey& other
    ) const noexcept
    {
        return pattern == other.pattern && count == other.count;
    }
};


struct DirectRepeatHash {
    CHARR_NEUTRAL_HELPER std::size_t operator()(
        const DirectRepeatKey& value
    ) const noexcept
    {
        const std::size_t pointer = reinterpret_cast<std::size_t>(
            value.pattern
        );
        return (pointer >> 3) ^
            (static_cast<std::size_t>(value.count) *
                static_cast<std::size_t>(0x9e3779b1U));
    }
};


CHARR_NEUTRAL_HELPER R_len_t recycling_length(
    R_len_t subject_length,
    R_len_t pattern_length,
    bool& warning
) noexcept
{
    warning = false;
    if (subject_length <= 0 || pattern_length <= 0)
        return 0;

    const R_len_t output = subject_length > pattern_length
        ? subject_length : pattern_length;
    warning = output % subject_length != 0 ||
        output % pattern_length != 0;
    return output;
}


CHARR_R_HELPER bool direct_view_r(
    SEXP value,
    shared::StringView& output,
    bool& modified
) noexcept
{
    modified = false;
    if (value == NA_STRING) {
        output = shared::StringView{
            nullptr, shared::missing_string_length,
            shared::StringEncoding::missing
        };
        return true;
    }
    if (!IS_ASCII(value) && !IS_UTF8(value))
        return false;

    output = shared::StringView{
        CHAR(value), LENGTH(value),
        IS_ASCII(value)
            ? shared::StringEncoding::ascii
            : shared::StringEncoding::utf8
    };
    if (output.enc == shared::StringEncoding::utf8 &&
            output.len >= 3 &&
            static_cast<unsigned char>(output.ptr[0]) == 0xefU &&
            static_cast<unsigned char>(output.ptr[1]) == 0xbbU &&
            static_cast<unsigned char>(output.ptr[2]) == 0xbfU) {
        output.ptr += 3;
        output.len -= 3;
        modified = true;
    }
    return true;
}


CHARR_CXX_HELPER CHARR_ALWAYS_INLINE shared::StringView normalize_view(
    const shared::StringView& source,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage
)
{
    if (source.enc == shared::StringEncoding::bytes)
        throw StriException(MSG__BYTESENC);
    return shared::normalize_utf8(source, converter, storage);
}


CHARR_CXX_HELPER R_len_t count_empty_patterns(
    const std::vector<shared::StringView>& patterns
)
{
    R_len_t result = 0;
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        if (!patterns[i].is_na() && patterns[i].len <= 0)
            ++result;
    }
    return result;
}


CHARR_R_HELPER SEXP missing_child_r() noexcept
{
    SEXP output = Rf_allocVector(STRSXP, 1);
    SET_STRING_ELT(output, 0, NA_STRING);
    return output;
}


CHARR_R_HELPER SEXP empty_child_r() noexcept
{
    return Rf_allocVector(STRSXP, 0);
}


CHARR_R_HELPER SEXP repeated_direct_child_r(
    SEXP value,
    R_len_t size
) noexcept
{
    SEXP output = Rf_allocVector(STRSXP, size);
    for (R_len_t i = 0; i < size; ++i)
        SET_STRING_ELT(output, i, value);
    return output;
}


CHARR_R_HELPER SEXP repeated_child_r(
    const shared::StringView& pattern,
    R_len_t size
) noexcept
{
    SEXP output = PROTECT(Rf_allocVector(STRSXP, size));
    const SEXP value = Rf_mkCharLenCE(
        pattern.len == 0 ? "" : pattern.ptr,
        pattern.len,
        pattern.enc == shared::StringEncoding::ascii
            ? CE_NATIVE : CE_UTF8
    );
    for (R_len_t i = 0; i < size; ++i)
        SET_STRING_ELT(output, i, value);
    UNPROTECT(1);
    return output;
}


CHARR_R_HELPER SEXP matched_child_r(
    const shared::FixedExtractPlan& plan,
    const shared::FixedExtractRow& row
) noexcept
{
    SEXP output = Rf_allocVector(STRSXP, row.count);
    for (R_len_t i = 0; i < row.count; ++i) {
        const shared::StringView& match = plan.matches[
            row.begin+static_cast<std::size_t>(i)
        ];
        SET_STRING_ELT(
            output, i,
            Rf_mkCharLenCE(
                match.len == 0 ? "" : match.ptr,
                match.len, CE_UTF8
            )
        );
    }
    return output;
}


CHARR_R_HELPER void emit_warnings_r(
    bool recycling_warning,
    R_len_t empty_pattern_warnings
) noexcept
{
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);
    for (R_len_t i = 0; i < empty_pattern_warnings; ++i)
        Rf_warning(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
}

} // namespace search_fixed_extract

using namespace search_fixed_extract;


/** Extract all fixed-pattern matches. */
CHARR_ENTRYPOINT SEXP ci_extract_all_fixed(
    SEXP str,
    SEXP pattern,
    SEXP simplify,
    SEXP omit_no_match,
    SEXP opts_fixed
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const shared::FixedSearchOptions options =
        fixed::prepare_options(opts_fixed, true);
    const bool omit_no_match_value =
        ci__prepare_arg_logical_1_notNA_r(
            omit_no_match, "omit_no_match"
        );
    simplify = entry_protections.protect_one(ci__prepare_arg_logical_1_r(
        simplify, "simplify"
    ));
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    pattern = entry_protections.protect_one(ci__prepare_arg_string_r(
        pattern, "pattern"
    ));

    const int simplify_value = LOGICAL_RO(simplify)[0];
    const bool simplifying =
        simplify_value == NA_LOGICAL || simplify_value != 0;
    const R_len_t subject_length = LENGTH(str);
    const R_len_t pattern_length = LENGTH(pattern);
    bool recycling_needed = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, recycling_needed
    );

    bool recycling_warning = false;
    R_len_t empty_pattern_warnings = 0;

    try {
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        std::vector<shared::StringView> subjects;
        std::vector<shared::StringView> patterns;
        shared::FixedMatcher matcher;
        std::vector<shared::FixedRange> scratch;
        shared::FixedExtractPlan plan;
        std::unordered_map<
            shared::FixedExtractRepeatKey,
            SEXP,
            shared::FixedExtractRepeatHash
        > repeated_children;
        std::unordered_map<
            DirectRepeatKey,
            SEXP,
            DirectRepeatHash
        > direct_children;

        scratch.reserve(16);
        repeated_children.reserve(16);
        direct_children.reserve(16);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                recycling_warning = recycling_needed;

                const SEXP* direct_subject_values = nullptr;
                const SEXP* direct_pattern_values = nullptr;
                bool direct = !options.case_insensitive &&
                    vectorize_length > 0;
                if (direct) {
                    direct_subject_values = STRING_PTR_RO(str);
                    direct_pattern_values = STRING_PTR_RO(pattern);
                    plan.rows.clear();
                    plan.matches.clear();
                    plan.max_columns = 0;
                    plan.matches_are_patterns = true;
                    plan.rows.resize(
                        static_cast<std::size_t>(vectorize_length)
                    );

                    for (R_len_t i = 0;
                            i < vectorize_length; ++i) {
                        const std::size_t pattern_index =
                            static_cast<std::size_t>(
                                i % pattern_length
                            );
                        shared::StringView prepared_pattern;
                        bool pattern_modified = false;
                        if (!direct_view_r(
                                direct_pattern_values[pattern_index],
                                prepared_pattern, pattern_modified
                        ) || pattern_modified) {
                            direct = false;
                            break;
                        }

                        shared::StringView prepared_subject;
                        bool subject_modified = false;
                        if (!direct_view_r(
                                direct_subject_values[i % subject_length],
                                prepared_subject, subject_modified
                        )) {
                            direct = false;
                            break;
                        }

                        if (i < pattern_length &&
                                !prepared_pattern.is_na() &&
                                prepared_pattern.len <= 0) {
                            ++empty_pattern_warnings;
                        }

                        shared::FixedExtractRow& row = plan.rows[
                            static_cast<std::size_t>(i)
                        ];
                        row.begin = pattern_index;
                        row.count = 0;
                        row.forced_na = false;
                        if (prepared_subject.is_na() ||
                                prepared_pattern.is_na() ||
                                prepared_pattern.len <= 0) {
                            row.forced_na = true;
                        }
                        else if (prepared_subject.len <= 0) {
                            row.forced_na = !omit_no_match_value;
                        }
                        else {
                            row.count = options.overlap
                                ? matcher.count(
                                    prepared_subject,
                                    prepared_pattern,
                                    options
                                )
                                : shared::count_exact_bytes(
                                    prepared_subject.ptr,
                                    prepared_subject.len,
                                    prepared_pattern.ptr,
                                    prepared_pattern.len
                                );
                            row.forced_na = row.count == 0 &&
                                !omit_no_match_value;
                        }

                        const int width = row.forced_na
                            ? 1 : row.count;
                        if (width > plan.max_columns)
                            plan.max_columns = width;
                    }
                }

                if (!direct) {
                    empty_pattern_warnings = 0;
                    if (vectorize_length > 0) {
                        subjects.resize(
                            static_cast<std::size_t>(subject_length)
                        );
                        const SEXP* values = STRING_PTR_RO(str);
                        for (R_len_t i = 0;
                                i < subject_length; ++i) {
                            subjects[static_cast<std::size_t>(i)] =
                                normalize_view(
                                    io::as_shared_view(values[i]),
                                    subject_converter, subject_storage
                                );
                        }

                        patterns.resize(
                            static_cast<std::size_t>(pattern_length)
                        );
                        values = STRING_PTR_RO(pattern);
                        for (R_len_t i = 0;
                                i < pattern_length; ++i) {
                            patterns[static_cast<std::size_t>(i)] =
                                normalize_view(
                                    io::as_shared_view(values[i]),
                                    pattern_converter, pattern_storage
                                );
                        }
                        empty_pattern_warnings =
                            count_empty_patterns(patterns);
                    }

                    shared::plan_fixed_extract(
                        subjects, patterns, vectorize_length,
                        options, omit_no_match_value,
                        matcher, scratch, plan
                    );
                }

                if (!simplifying) {
                    result = entry_protections.reprotect_one(
                        Rf_allocVector(VECSXP, vectorize_length),
                        result_index
                    );
                    SEXP child = R_NilValue;
                    PROTECT_INDEX child_index;
                    callback_protections.protect_with_index(
                        child, &child_index
                    );
                    SEXP missing_child = R_NilValue;
                    SEXP empty_child = R_NilValue;

                    for (R_len_t i = 0;
                            i < vectorize_length; ++i) {
                        const shared::FixedExtractRow& row =
                            plan.rows[static_cast<std::size_t>(i)];
                        if (row.forced_na) {
                            if (missing_child == R_NilValue) {
                                child = callback_protections.reprotect_slot(
                                    missing_child_r(), child_index
                                );
                                SET_VECTOR_ELT(result, i, child);
                                missing_child = child;
                            }
                            else {
                                child = callback_protections.reprotect_slot(
                                    missing_child, child_index
                                );
                                SET_VECTOR_ELT(result, i, child);
                            }
                        }
                        else if (row.count == 0) {
                            if (empty_child == R_NilValue) {
                                child = callback_protections.reprotect_slot(
                                    empty_child_r(), child_index
                                );
                                SET_VECTOR_ELT(result, i, child);
                                empty_child = child;
                            }
                            else {
                                child = callback_protections.reprotect_slot(
                                    empty_child, child_index
                                );
                                SET_VECTOR_ELT(result, i, child);
                            }
                        }
                        else if (plan.matches_are_patterns) {
                            if (direct) {
                                const DirectRepeatKey key{
                                    direct_pattern_values[row.begin],
                                    row.count
                                };
                                const auto found =
                                    direct_children.find(key);
                                if (found != direct_children.end()) {
                                    child = callback_protections.reprotect_slot(
                                        found->second, child_index
                                    );
                                    SET_VECTOR_ELT(result, i, child);
                                }
                                else {
                                    child = callback_protections.reprotect_slot(
                                        repeated_direct_child_r(
                                            key.pattern, row.count
                                        ),
                                        child_index
                                    );
                                    SET_VECTOR_ELT(result, i, child);
                                    direct_children.emplace(key, child);
                                }
                                continue;
                            }

                            const shared::FixedExtractRepeatKey key{
                                patterns[row.begin], row.count
                            };
                            const auto found =
                                repeated_children.find(key);
                            if (found != repeated_children.end()) {
                                child = callback_protections.reprotect_slot(
                                    found->second, child_index
                                );
                                SET_VECTOR_ELT(result, i, child);
                            }
                            else {
                                child = callback_protections.reprotect_slot(
                                    repeated_child_r(
                                        key.pattern, row.count
                                    ),
                                    child_index
                                );
                                SET_VECTOR_ELT(result, i, child);
                                repeated_children.emplace(
                                    key, child
                                );
                            }
                        }
                        else {
                            child = callback_protections.reprotect_slot(
                                matched_child_r(plan, row),
                                child_index
                            );
                            SET_VECTOR_ELT(result, i, child);
                        }
                    }
                }
                else {
                    result = entry_protections.reprotect_one(
                        Rf_allocMatrix(
                            STRSXP, vectorize_length,
                            plan.max_columns
                        ),
                        result_index
                    );
                    for (R_len_t i = 0;
                            i < vectorize_length; ++i) {
                        const shared::FixedExtractRow& row =
                            plan.rows[static_cast<std::size_t>(i)];
                        R_len_t j = 0;
                        if (row.forced_na) {
                            SET_STRING_ELT(result, i, NA_STRING);
                            j = 1;
                        }
                        else if (plan.matches_are_patterns) {
                            if (direct) {
                                const SEXP pattern_sexp =
                                    direct_pattern_values[row.begin];
                                for (; j < row.count; ++j) {
                                    SET_STRING_ELT(
                                        result,
                                        i+j*vectorize_length,
                                        pattern_sexp
                                    );
                                }
                                for (; j < plan.max_columns; ++j) {
                                    SET_STRING_ELT(
                                        result,
                                        i+j*vectorize_length,
                                        simplify_value == NA_LOGICAL
                                            ? NA_STRING : R_BlankString
                                    );
                                }
                                continue;
                            }

                            const shared::StringView& pattern_value =
                                patterns[row.begin];
                            const SEXP pattern_sexp = Rf_mkCharLenCE(
                                pattern_value.len == 0
                                    ? "" : pattern_value.ptr,
                                pattern_value.len,
                                pattern_value.enc ==
                                        shared::StringEncoding::ascii
                                    ? CE_NATIVE : CE_UTF8
                            );
                            for (; j < row.count; ++j) {
                                SET_STRING_ELT(
                                    result,
                                    i+j*vectorize_length,
                                    pattern_sexp
                                );
                            }
                        }
                        else {
                            for (; j < row.count; ++j) {
                                const shared::StringView& match =
                                    plan.matches[
                                        row.begin+
                                        static_cast<std::size_t>(j)
                                    ];
                                SET_STRING_ELT(
                                    result,
                                    i+j*vectorize_length,
                                    Rf_mkCharLenCE(
                                        match.len == 0
                                            ? "" : match.ptr,
                                        match.len, CE_UTF8
                                    )
                                );
                            }
                        }
                        for (; j < plan.max_columns; ++j) {
                            SET_STRING_ELT(
                                result,
                                i+j*vectorize_length,
                                simplify_value == NA_LOGICAL
                                    ? NA_STRING : R_BlankString
                            );
                        }
                    }
                }

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
