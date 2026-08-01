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
#include "../shared/character_class.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <array>
#include <cstddef>
#include <exception>
#include <stdexcept>
#include <vector>


namespace charr { namespace base_backend {

namespace search_class_trim {

struct TrimSlice {
    const char* data;
    R_len_t length;
    bool missing;
};


struct ScalarTrimPattern {
    const icu::UnicodeSet* retained;
    std::array<unsigned char, 128> ascii_retained;
    bool missing;
};


CHARR_NEUTRAL_HELPER bool retained_contains(
    const icu::UnicodeSet& retained,
    const unsigned char* ascii_retained,
    UChar32 code_point
) noexcept
{
    if (ascii_retained != nullptr && code_point <= 0x7f)
        return ascii_retained[code_point] != 0;
    return retained.contains(code_point);
}


// Compile-time direction flags keep the hot edge scan branch-free.
template<bool Left, bool Right>
CHARR_CXX_HELPER TrimSlice trim_slice(
    const char* data, R_len_t length,
    const icu::UnicodeSet& retained,
    const unsigned char* ascii_retained
)
{
    R_len_t begin = 0;
    R_len_t end = length;

    if (Left) {
        UChar32 code_point;
        for (R_len_t cursor = 0; cursor < length; ) {
            U8_NEXT(data, cursor, length, code_point);
            if (code_point < 0)
                throw StriException(MSG__INVALID_UTF8);
            if (retained_contains(
                    retained, ascii_retained, code_point
                ))
                break;
            begin = cursor;
        }
    }

    if (Right && begin < length) {
        UChar32 code_point;
        for (R_len_t cursor = length; cursor > 0; ) {
            U8_PREV(data, 0, cursor, code_point);
            if (code_point < 0)
                throw StriException(MSG__INVALID_UTF8);
            if (retained_contains(
                    retained, ascii_retained, code_point
                ))
                break;
            end = cursor;
        }
    }

    return TrimSlice{data + begin, end - begin, false};
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


CHARR_CXX_HELPER shared::StringView normalize_input(
    const shared::StringView& source,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage
)
{
    if (source.enc == shared::StringEncoding::bytes)
        throw StriException(MSG__BYTESENC);
    return shared::normalize_utf8(source, converter, storage);
}


CHARR_CXX_HELPER void compile_patterns(
    const std::vector<shared::StringView>& patterns,
    bool negate, shared::CharacterClassSet& output
)
{
    const UErrorCode status = output.reset(patterns, negate);
    if (U_FAILURE(status))
        throw StriException(status);
}


CHARR_CXX_HELPER ScalarTrimPattern make_scalar_pattern(
    const shared::CharacterClassSet& patterns
)
{
    ScalarTrimPattern scalar{nullptr, {}, patterns.is_na(0)};
    if (scalar.missing)
        return scalar;

    scalar.retained = &patterns.get(0);
    for (std::size_t i = 0; i < scalar.ascii_retained.size(); ++i) {
        scalar.ascii_retained[i] = static_cast<unsigned char>(
            scalar.retained->contains(static_cast<UChar32>(i))
        );
    }
    return scalar;
}


template<bool Left, bool Right, bool ScalarPattern>
CHARR_CXX_HELPER TrimSlice trim_record(
    const std::vector<shared::StringView>& subjects,
    R_len_t index,
    const shared::CharacterClassSet& patterns,
    const ScalarTrimPattern& scalar
)
{
    const R_len_t subject_length = static_cast<R_len_t>(subjects.size());
    const shared::StringView& value = subjects[
        static_cast<std::size_t>(index % subject_length)
    ];

    const bool pattern_missing = ScalarPattern
        ? scalar.missing
        : patterns.is_na(static_cast<std::size_t>(index));
    if (value.is_na() || pattern_missing)
        return TrimSlice{nullptr, 0, true};

    const icu::UnicodeSet* retained;
    const unsigned char* ascii_retained;
    if constexpr (ScalarPattern) {
        retained = scalar.retained;
        ascii_retained = scalar.ascii_retained.data();
    }
    else {
        retained = &patterns.get(static_cast<std::size_t>(index));
        ascii_retained = nullptr;
    }
    return trim_slice<Left, Right>(
        value.ptr, value.len, *retained, ascii_retained
    );
}


CHARR_R_HELPER void install_record(
    SEXP output, R_len_t index, const TrimSlice& record
) noexcept
{
    if (record.missing) {
        SET_STRING_ELT(output, index, NA_STRING);
        return;
    }
    SET_STRING_ELT(
        output, index,
        Rf_mkCharLenCE(record.data, record.length, CE_UTF8)
    );
}


CHARR_R_HELPER void emit_recycling_warning(bool should_warn) noexcept
{
    if (should_warn)
        Rf_warning(MSG__WARN_RECYCLING_RULE);
}

} // namespace search_class_trim

using namespace search_class_trim;

/**
 * Trim characters from a charclass from both sides of the string
 *
 * @param str character vector
 * @param pattern character vector
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-04)
 *          Use ci__trim_leftright
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException-friendly
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-06-10) negate
*/
CHARR_ENTRYPOINT SEXP ci_trim_both(
    SEXP str, SEXP pattern, SEXP negate
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool negate_1 = ci__prepare_arg_logical_1_notNA_r(
        negate, "negate"
    );
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    pattern = entry_protections.protect_one(ci__prepare_arg_string_r(pattern, "pattern"));

    const R_len_t subject_length = LENGTH(str);
    const R_len_t pattern_length = LENGTH(pattern);
    bool recycling_warning = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, recycling_warning
    );

    try {
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        std::vector<shared::StringView> subjects;
        std::vector<shared::StringView> patterns;
        shared::CharacterClassSet character_classes;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                patterns.resize(
                    static_cast<std::size_t>(pattern_length)
                );
                if (pattern_length > 0) {
                    const SEXP* values = STRING_PTR_RO(pattern);
                    for (R_len_t i = 0; i < pattern_length; ++i) {
                        patterns[static_cast<std::size_t>(i)] =
                            normalize_input(
                                io::as_shared_view(values[i]),
                                pattern_converter, pattern_storage
                            );
                    }
                }
                compile_patterns(
                    patterns, negate_1, character_classes
                );

                result = entry_protections.reprotect_one(
                    Rf_allocVector(STRSXP, vectorize_length), result_index
                );

                if (vectorize_length > 0) {
                    subjects.resize(
                        static_cast<std::size_t>(subject_length)
                    );
                    const SEXP* values = STRING_PTR_RO(str);
                    for (R_len_t i = 0; i < subject_length; ++i) {
                        subjects[static_cast<std::size_t>(i)] =
                            normalize_input(
                                io::as_shared_view(values[i]),
                                subject_converter, subject_storage
                            );
                    }

                    const bool scalar_pattern = pattern_length == 1;
                    const ScalarTrimPattern scalar = scalar_pattern
                        ? make_scalar_pattern(character_classes)
                        : ScalarTrimPattern{nullptr, {}, false};
                    if (scalar_pattern) {
                        for (R_len_t i = 0; i < vectorize_length; ++i) {
                            install_record(
                                result, i,
                                trim_record<true, true, true>(
                                    subjects, i, character_classes, scalar
                                )
                            );
                        }
                    }
                    else {
                        for (R_len_t i = 0; i < vectorize_length; ++i) {
                            install_record(
                                result, i,
                                trim_record<true, true, false>(
                                    subjects, i, character_classes, scalar
                                )
                            );
                        }
                    }
                }
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_recycling_warning(recycling_warning);
    );
}


/**
 * Trim characters from a charclass from the left of the string
 *
 * @param str character vector
 * @param pattern character vector
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-04)
 *          Use ci__trim_leftright
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-06-10) negate
*/
CHARR_ENTRYPOINT SEXP ci_trim_left(
    SEXP str, SEXP pattern, SEXP negate
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool negate_1 = ci__prepare_arg_logical_1_notNA_r(
        negate, "negate"
    );
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    pattern = entry_protections.protect_one(ci__prepare_arg_string_r(pattern, "pattern"));

    const R_len_t subject_length = LENGTH(str);
    const R_len_t pattern_length = LENGTH(pattern);
    bool recycling_warning = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, recycling_warning
    );

    try {
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        std::vector<shared::StringView> subjects;
        std::vector<shared::StringView> patterns;
        shared::CharacterClassSet character_classes;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                patterns.resize(
                    static_cast<std::size_t>(pattern_length)
                );
                if (pattern_length > 0) {
                    const SEXP* values = STRING_PTR_RO(pattern);
                    for (R_len_t i = 0; i < pattern_length; ++i) {
                        patterns[static_cast<std::size_t>(i)] =
                            normalize_input(
                                io::as_shared_view(values[i]),
                                pattern_converter, pattern_storage
                            );
                    }
                }
                compile_patterns(
                    patterns, negate_1, character_classes
                );

                result = entry_protections.reprotect_one(
                    Rf_allocVector(STRSXP, vectorize_length), result_index
                );

                if (vectorize_length > 0) {
                    subjects.resize(
                        static_cast<std::size_t>(subject_length)
                    );
                    const SEXP* values = STRING_PTR_RO(str);
                    for (R_len_t i = 0; i < subject_length; ++i) {
                        subjects[static_cast<std::size_t>(i)] =
                            normalize_input(
                                io::as_shared_view(values[i]),
                                subject_converter, subject_storage
                            );
                    }

                    const bool scalar_pattern = pattern_length == 1;
                    const ScalarTrimPattern scalar = scalar_pattern
                        ? make_scalar_pattern(character_classes)
                        : ScalarTrimPattern{nullptr, {}, false};
                    if (scalar_pattern) {
                        for (R_len_t i = 0; i < vectorize_length; ++i) {
                            install_record(
                                result, i,
                                trim_record<true, false, true>(
                                    subjects, i, character_classes, scalar
                                )
                            );
                        }
                    }
                    else {
                        for (R_len_t i = 0; i < vectorize_length; ++i) {
                            install_record(
                                result, i,
                                trim_record<true, false, false>(
                                    subjects, i, character_classes, scalar
                                )
                            );
                        }
                    }
                }
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_recycling_warning(recycling_warning);
    );
}


/**
 * Trim characters from a charclass from the right of the string
 *
 * @param str character vector
 * @param pattern character vector
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-04)
 *          Use ci__trim_leftright
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-06-10) negate
*/
CHARR_ENTRYPOINT SEXP ci_trim_right(
    SEXP str, SEXP pattern, SEXP negate
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool negate_1 = ci__prepare_arg_logical_1_notNA_r(
        negate, "negate"
    );
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    pattern = entry_protections.protect_one(ci__prepare_arg_string_r(pattern, "pattern"));

    const R_len_t subject_length = LENGTH(str);
    const R_len_t pattern_length = LENGTH(pattern);
    bool recycling_warning = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, recycling_warning
    );

    try {
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        std::vector<shared::StringView> subjects;
        std::vector<shared::StringView> patterns;
        shared::CharacterClassSet character_classes;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                patterns.resize(
                    static_cast<std::size_t>(pattern_length)
                );
                if (pattern_length > 0) {
                    const SEXP* values = STRING_PTR_RO(pattern);
                    for (R_len_t i = 0; i < pattern_length; ++i) {
                        patterns[static_cast<std::size_t>(i)] =
                            normalize_input(
                                io::as_shared_view(values[i]),
                                pattern_converter, pattern_storage
                            );
                    }
                }
                compile_patterns(
                    patterns, negate_1, character_classes
                );

                result = entry_protections.reprotect_one(
                    Rf_allocVector(STRSXP, vectorize_length), result_index
                );

                if (vectorize_length > 0) {
                    subjects.resize(
                        static_cast<std::size_t>(subject_length)
                    );
                    const SEXP* values = STRING_PTR_RO(str);
                    for (R_len_t i = 0; i < subject_length; ++i) {
                        subjects[static_cast<std::size_t>(i)] =
                            normalize_input(
                                io::as_shared_view(values[i]),
                                subject_converter, subject_storage
                            );
                    }

                    const bool scalar_pattern = pattern_length == 1;
                    const ScalarTrimPattern scalar = scalar_pattern
                        ? make_scalar_pattern(character_classes)
                        : ScalarTrimPattern{nullptr, {}, false};
                    if (scalar_pattern) {
                        for (R_len_t i = 0; i < vectorize_length; ++i) {
                            install_record(
                                result, i,
                                trim_record<false, true, true>(
                                    subjects, i, character_classes, scalar
                                )
                            );
                        }
                    }
                    else {
                        for (R_len_t i = 0; i < vectorize_length; ++i) {
                            install_record(
                                result, i,
                                trim_record<false, true, false>(
                                    subjects, i, character_classes, scalar
                                )
                            );
                        }
                    }
                }
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_recycling_warning(recycling_warning);
    );
}

} } // namespace charr::base_backend
