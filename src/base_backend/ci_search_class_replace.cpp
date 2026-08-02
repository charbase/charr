
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
#include "../shared/character_class_search.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <vector>

namespace charr { namespace base_backend {

namespace search_class_replace {

struct ReplacementRecord {
    const char* data;
    R_len_t length;
    bool missing;
    bool changed;
};


CHARR_NEUTRAL_HELPER R_len_t recycling_length(
    R_len_t subject_length,
    R_len_t pattern_length,
    R_len_t replacement_length,
    bool& should_warn
) noexcept
{
    should_warn = false;
    if (subject_length <= 0 || pattern_length <= 0 ||
            replacement_length <= 0) {
        return 0;
    }

    R_len_t result = subject_length;
    if (pattern_length > result)
        result = pattern_length;
    if (replacement_length > result)
        result = replacement_length;
    should_warn = result % subject_length != 0 ||
        result % pattern_length != 0 ||
        result % replacement_length != 0;
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
    shared::CharacterClassSet& output
)
{
    const UErrorCode status = output.reset(patterns, false);
    if (U_FAILURE(status))
        throw StriException(status);
}


CHARR_CXX_HELPER ReplacementRecord replace_record(
    const shared::StringView& subject,
    bool pattern_missing,
    const icu::UnicodeSet* pattern,
    const shared::StringView& replacement,
    bool merge,
    std::vector<shared::CharacterClassRange>& ranges,
    std::vector<char>& scratch
)
{
    if (subject.is_na() || pattern_missing)
        return ReplacementRecord{nullptr, 0, true, false};

    const std::size_t matched_bytes =
        shared::find_character_class_ranges(
            subject, *pattern, merge, ranges
        );
    if (ranges.size() == 0) {
        return ReplacementRecord{
            subject.ptr, subject.len, false, false
        };
    }
    if (replacement.is_na())
        return ReplacementRecord{nullptr, 0, true, true};

    const std::size_t output_size =
        shared::checked_replacement_size(
            subject, matched_bytes, ranges, replacement
        );
    scratch.resize(output_size);
    char* output = output_size > 0 ? scratch.data() : nullptr;
    shared::write_replacement(
        subject, ranges, replacement, output, output_size
    );
    return ReplacementRecord{
        output_size > 0 ? output : replacement.ptr,
        static_cast<R_len_t>(output_size), false, true
    };
}


CHARR_CXX_HELPER void compute_sequential(
    const std::vector<shared::StringView>& subjects,
    const std::vector<shared::StringView>& replacements,
    const shared::CharacterClassSet& patterns,
    bool merge,
    std::vector<shared::StringView>& working,
    std::vector<unsigned char>& changed,
    std::vector<shared::CharacterClassRange>& ranges,
    shared::SliceArena& replacement_storage,
    bool& all_missing
)
{
    working.resize(subjects.size());
    changed.assign(subjects.size(), 0);
    for (std::size_t i = 0; i < subjects.size(); ++i)
        working[i] = subjects[i];

    all_missing = false;
    const std::size_t pattern_length = patterns.size();
    const std::size_t replacement_length = replacements.size();
    for (std::size_t pattern_index = 0;
            pattern_index < pattern_length; ++pattern_index) {
        if (patterns.is_na(pattern_index)) {
            all_missing = true;
            return;
        }

        const shared::StringView& replacement = replacements[
            pattern_index % replacement_length
        ];
        const icu::UnicodeSet& pattern = patterns.get(pattern_index);
        for (std::size_t subject_index = 0;
                subject_index < working.size(); ++subject_index) {
            const shared::StringView subject = working[subject_index];
            if (subject.is_na())
                continue;

            const std::size_t matched_bytes =
                shared::find_character_class_ranges(
                    subject, pattern, merge, ranges
                );
            if (ranges.size() == 0)
                continue;
            if (replacement.is_na()) {
                working[subject_index] = shared::StringView{
                    nullptr, shared::missing_string_length,
                    shared::StringEncoding::missing
                };
                changed[subject_index] = 1;
                continue;
            }

            const std::size_t output_size =
                shared::checked_replacement_size(
                    subject, matched_bytes, ranges, replacement
                );
            char* output = output_size > 0
                ? replacement_storage.allocate(output_size)
                : nullptr;
            shared::write_replacement(
                subject, ranges, replacement, output, output_size
            );
            working[subject_index] = shared::StringView{
                output_size > 0 ? output : replacement.ptr,
                static_cast<R_len_t>(output_size),
                shared::StringEncoding::utf8
            };
            changed[subject_index] = 1;
        }
    }
}


CHARR_R_HELPER void install_record(
    SEXP output,
    R_len_t output_index,
    SEXP original,
    const shared::StringView& normalized,
    const ReplacementRecord& record
) noexcept
{
    if (record.missing) {
        SET_STRING_ELT(output, output_index, NA_STRING);
        return;
    }

    if (!record.changed && original != NA_STRING &&
            normalized.ptr == CHAR(original) &&
            normalized.len == LENGTH(original)) {
        SET_STRING_ELT(output, output_index, original);
        return;
    }

    SET_STRING_ELT(
        output, output_index,
        Rf_mkCharLenCE(record.data, record.length, CE_UTF8)
    );
}


CHARR_R_HELPER void emit_recycling_warning(
    bool should_warn
) noexcept
{
    if (should_warn)
        Rf_warning(MSG__WARN_RECYCLING_RULE);
}


CHARR_R_HELPER void require_sequential_lengths(
    R_len_t pattern_length,
    R_len_t replacement_length
) noexcept
{
    if (pattern_length < replacement_length || pattern_length <= 0 ||
            replacement_length <= 0) {
        Rf_error(MSG__WARN_RECYCLING_RULE2);
    }
}

} // namespace search_class_replace

using namespace search_class_replace;


/**
 * Replace all occurrences of a character class
 *
 * @param str character vector; strings to search in
 * @param pattern character vector; charclasses to search for
 * @param replacement character vector; strings to replace with
 * @param merge merge consecutive matches into a single one?
 *
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-07)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException-friendly
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-03)
 *          detects invalid UTF-8 byte stream;
 *          merge arg added (replacement of old ci_trim_both/double by BT)
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-05)
 *          charclass::PatternSet now relies on UnicodeSet
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-02)
 *          using String8buf::replaceAllAtPos and charclass::PatternSet::locateAll;
 *          no longer vectorized over merge
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.0-2 (Marek Gagolewski, 2016-01-30)
 *    Issue #210: Allow NA replacement
 */
CHARR_ENTRYPOINT SEXP ci_replace_all_charclass(
    SEXP str,
    SEXP pattern,
    SEXP replacement,
    SEXP merge,
    SEXP vectorize_all
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool vectorize = ci__prepare_arg_logical_1_notNA_r(
        vectorize_all, "vectorize_all"
    );
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    const R_len_t subject_length = LENGTH(str);
    const bool empty_sequential = !vectorize && subject_length <= 0;
    pattern = entry_protections.protect_one(empty_sequential
        ? R_NilValue
        : ci__prepare_arg_string_r(pattern, "pattern"));
    replacement = entry_protections.protect_one(empty_sequential
        ? R_NilValue
        : ci__prepare_arg_string_r(replacement, "replacement"));

    const R_len_t pattern_length = empty_sequential
        ? 0
        : LENGTH(pattern);
    const R_len_t replacement_length = empty_sequential
        ? 0
        : LENGTH(replacement);
    R_len_t vectorize_length = subject_length;
    bool merge_value = false;
    bool recycling_warning = false;
    if (vectorize) {
        merge_value = ci__prepare_arg_logical_1_notNA_r(
            merge, "merge"
        );
        vectorize_length = recycling_length(
            subject_length, pattern_length, replacement_length,
            recycling_warning
        );
    }
    else if (!empty_sequential) {
        require_sequential_lengths(
            pattern_length, replacement_length
        );
        recycling_warning = pattern_length % replacement_length != 0;
        emit_recycling_warning(recycling_warning);
        merge_value = ci__prepare_arg_logical_1_notNA_r(
            merge, "merge"
        );
    }

    try {
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 replacement_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena replacement_storage;
        shared::SliceArena pattern_storage;
        shared::SliceArena sequential_replacement_storage;
        std::vector<shared::StringView> subjects;
        std::vector<shared::StringView> replacements;
        std::vector<shared::StringView> patterns;
        shared::CharacterClassSet character_classes;
        std::vector<shared::CharacterClassRange> ranges;
        std::vector<char> output_scratch;
        std::vector<shared::StringView> sequential_output;
        std::vector<unsigned char> sequential_changed;
        bool sequential_all_missing = false;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                if (empty_sequential) {
                    result = entry_protections.reprotect_one(
                        Rf_allocVector(STRSXP, 0),
                        result_index
                    );
                }
                else {
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

                        replacements.resize(
                            static_cast<std::size_t>(replacement_length)
                        );
                        values = STRING_PTR_RO(replacement);
                        for (R_len_t i = 0;
                                i < replacement_length; ++i) {
                            replacements[static_cast<std::size_t>(i)] =
                                normalize_input(
                                    io::as_shared_view(values[i]),
                                    replacement_converter,
                                    replacement_storage
                                );
                        }
                    }

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
                    compile_patterns(patterns, character_classes);

                    if (vectorize || pattern_length == 1) {
                        result = entry_protections.reprotect_one(
                            Rf_allocVector(STRSXP, vectorize_length),
                            result_index
                        );
                        for (R_len_t pattern_offset = 0;
                                pattern_offset < pattern_length;
                                ++pattern_offset) {
                            R_len_t i = pattern_offset;
                            while (i < vectorize_length) {
                                const R_len_t source_offset =
                                    i % subject_length;
                                const bool pattern_missing =
                                    character_classes.is_na(
                                        static_cast<std::size_t>(i)
                                    );
                                const ReplacementRecord record =
                                    replace_record(
                                        subjects[
                                            static_cast<std::size_t>(
                                                source_offset
                                            )
                                        ],
                                        pattern_missing,
                                        pattern_missing
                                            ? nullptr
                                            : &character_classes.get(
                                                static_cast<std::size_t>(i)
                                            ),
                                        replacements[
                                            static_cast<std::size_t>(
                                                i % replacement_length
                                            )
                                        ],
                                        merge_value, ranges,
                                        output_scratch
                                    );
                                install_record(
                                    result, i,
                                    STRING_ELT(
                                        str, source_offset
                                    ),
                                    subjects[
                                        static_cast<std::size_t>(
                                            source_offset
                                        )
                                    ],
                                    record
                                );

                                if (vectorize_length-i <= pattern_length)
                                    break;
                                i += pattern_length;
                            }
                        }
                    }
                    else {
                        compute_sequential(
                            subjects, replacements, character_classes,
                            merge_value, sequential_output,
                            sequential_changed, ranges,
                            sequential_replacement_storage,
                            sequential_all_missing
                        );

                        result = entry_protections.reprotect_one(
                            Rf_allocVector(STRSXP, subject_length),
                            result_index
                        );
                        for (R_len_t i = 0; i < subject_length; ++i) {
                            const std::size_t index =
                                static_cast<std::size_t>(i);
                            const ReplacementRecord record =
                                sequential_all_missing
                                    ? ReplacementRecord{
                                        nullptr, 0, true, true
                                    }
                                    : ReplacementRecord{
                                        sequential_output[index].ptr,
                                        sequential_output[index].is_na()
                                            ? 0
                                            : sequential_output[index].len,
                                        sequential_output[index].is_na(),
                                        sequential_changed[index] != 0
                                    };
                            install_record(
                                result, i, STRING_ELT(str, i),
                                subjects[index], record
                            );
                        }
                    }
                }
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        if (vectorize)
            emit_recycling_warning(recycling_warning);
    );
}


} } // namespace charr::base_backend
