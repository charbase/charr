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
#include "io/reader_utils.h"
#include "io/string_view.h"
#include "io/utf8_output.h"
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
#include <utility>
#include <vector>

namespace charr { namespace altrep_backend {

namespace search_class_replace {

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


CHARR_CXX_HELPER void normalize_views(
    const charport::StrViews& source,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    std::vector<shared::StringView>& output
)
{
    output.resize(static_cast<std::size_t>(source.size()));
    for (R_xlen_t i = 0; i < source.size(); ++i) {
        const shared::StringView value = io::as_shared_view(source[i]);
        if (value.enc == shared::StringEncoding::bytes)
            throw StriException(MSG__BYTESENC);
        output[static_cast<std::size_t>(i)] = shared::normalize_utf8(
            value, converter, storage
        );
    }
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


CHARR_CXX_HELPER void replace_vectorized(
    const std::vector<shared::StringView>& subjects,
    const std::vector<shared::StringView>& replacements,
    const shared::CharacterClassSet& patterns,
    R_len_t output_length,
    bool merge,
    std::vector<shared::CharacterClassRange>& ranges,
    io::OutputBuilder& output
)
{
    output.reset(output_length);
    const R_len_t subject_length = static_cast<R_len_t>(subjects.size());
    const R_len_t pattern_length = static_cast<R_len_t>(patterns.size());
    const R_len_t replacement_length =
        static_cast<R_len_t>(replacements.size());

    for (R_len_t pattern_offset = 0;
            pattern_offset < pattern_length; ++pattern_offset) {
        R_len_t i = pattern_offset;
        while (i < output_length) {
            const shared::StringView& subject = subjects[
                static_cast<std::size_t>(i % subject_length)
            ];
            if (subject.is_na() || patterns.is_na(
                    static_cast<std::size_t>(i)
                )) {
                output.set_na(i);
            }
            else {
                const std::size_t matched_bytes =
                    shared::find_character_class_ranges(
                        subject,
                        patterns.get(static_cast<std::size_t>(i)),
                        merge, ranges
                    );
                if (ranges.size() == 0) {
                    output.set(i, io::as_charport_view(subject));
                }
                else {
                    const shared::StringView& replacement = replacements[
                        static_cast<std::size_t>(
                            i % replacement_length
                        )
                    ];
                    if (replacement.is_na()) {
                        output.set_na(i);
                    }
                    else {
                        const std::size_t output_size =
                            shared::checked_replacement_size(
                                subject, matched_bytes, ranges, replacement
                            );
                        const bool ascii =
                            shared::replacement_is_ascii(
                                subject, ranges, replacement
                            );
                        char* destination = output.reserve(
                            i, output_size,
                            ascii
                                ? cetype_ext_t::CE_ASCII
                                : cetype_ext_t::CE_UTF8
                        );
                        shared::write_replacement(
                            subject, ranges, replacement,
                            destination, output_size
                        );
                    }
                }
            }

            if (output_length-i <= pattern_length)
                break;
            i += pattern_length;
        }
    }
}


CHARR_NEUTRAL_HELPER bool sequential_subject_is_ascii(
    const shared::StringView& value
) noexcept
{
    if (value.enc == shared::StringEncoding::ascii)
        return true;
    if (value.enc != shared::StringEncoding::ascii_or_utf8)
        return false;
    return io::is_ascii(
        value.ptr, static_cast<std::size_t>(value.len)
    );
}


CHARR_CXX_HELPER void replace_sequential(
    const std::vector<shared::StringView>& subjects,
    const std::vector<shared::StringView>& replacements,
    const shared::CharacterClassSet& patterns,
    bool merge,
    std::vector<shared::StringView>& working,
    std::vector<shared::CharacterClassRange>& ranges,
    shared::SliceArena& replacement_storage,
    io::OutputBuilder& output
)
{
    working.resize(subjects.size());
    for (std::size_t i = 0; i < subjects.size(); ++i)
        working[i] = subjects[i];

    bool all_missing = false;
    const std::size_t pattern_length = patterns.size();
    const std::size_t replacement_length = replacements.size();
    for (std::size_t pattern_index = 0;
            pattern_index < pattern_length; ++pattern_index) {
        if (patterns.is_na(pattern_index)) {
            all_missing = true;
            break;
        }

        const icu::UnicodeSet& pattern = patterns.get(pattern_index);
        const shared::StringView& replacement = replacements[
            pattern_index % replacement_length
        ];
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
                continue;
            }

            const std::size_t output_size =
                shared::checked_replacement_size(
                    subject, matched_bytes, ranges, replacement
                );
            const bool ascii = sequential_subject_is_ascii(subject) &&
                io::is_ascii(
                    replacement.ptr,
                    static_cast<std::size_t>(replacement.len)
                );
            char* destination = output_size > 0
                ? replacement_storage.allocate(output_size)
                : nullptr;
            shared::write_replacement(
                subject, ranges, replacement, destination, output_size
            );
            working[subject_index] = shared::StringView{
                output_size > 0 ? destination : replacement.ptr,
                static_cast<R_len_t>(output_size),
                ascii
                    ? shared::StringEncoding::ascii
                    : shared::StringEncoding::utf8
            };
        }
    }

    output.reset(static_cast<R_len_t>(subjects.size()));
    for (std::size_t i = 0; i < subjects.size(); ++i) {
        if (all_missing || working[i].is_na())
            output.set_na(static_cast<R_len_t>(i));
        else
            output.set(
                static_cast<R_len_t>(i),
                io::as_charport_view(working[i])
            );
    }
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


CHARR_R_HELPER void emit_recycling_warning(bool should_warn) noexcept
{
    if (should_warn)
        Rf_warning(MSG__WARN_RECYCLING_RULE);
}

} // namespace search_class_replace

using namespace search_class_replace;


/**
 * Replace all occurrences of a character class
 *
 * @param str character vector; strings to search in
 * @param pattern character vector; character classes to search for
 * @param replacement character vector; strings to replace with
 * @param merge merge consecutive matches into a single one?
 * @param vectorize_all apply all patterns sequentially or vectorize them?
 *
 * @return character vector
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
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    const R_len_t subject_length = LENGTH(str);
    const bool empty_sequential = !vectorize && subject_length <= 0;
    pattern = entry_protections.protect_one(
        empty_sequential
                ? R_NilValue
                : ci__prepare_arg_string_r(pattern, "pattern")
    );
    replacement = entry_protections.protect_one(
        empty_sequential
                ? R_NilValue
                : ci__prepare_arg_string_r(replacement, "replacement")
    );

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
        charport::Reader subject_reader;
        charport::Reader pattern_reader;
        charport::Reader replacement_reader;
        charport::StrViews subject_views;
        charport::StrViews pattern_views;
        charport::StrViews replacement_views;
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::NativeToUtf8 replacement_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        shared::SliceArena replacement_storage;
        shared::SliceArena sequential_replacement_storage;
        std::vector<shared::StringView> subjects;
        std::vector<shared::StringView> patterns;
        std::vector<shared::StringView> replacements;
        std::vector<shared::StringView> sequential_output;
        shared::CharacterClassSet character_classes;
        std::vector<shared::CharacterClassRange> ranges;
        io::OutputBuilder output(0);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                if (!empty_sequential) {
                    if (vectorize_length > 0) {
                        subject_reader.reset(str);
                        if (subject_reader.size() != subject_length) {
                            throw std::runtime_error(
                                "Reader length changed during character-class replacement"
                            );
                        }
                        subject_views.resize(subject_length);
                        subject_reader.views(
                            0, subject_length,
                            subject_views.ptrs(), subject_views.lengths(),
                            subject_views.encodings()
                        );
                        normalize_views(
                            subject_views, subject_converter,
                            subject_storage, subjects
                        );

                        replacement_reader.reset(replacement);
                        if (replacement_reader.size() !=
                                replacement_length) {
                            throw std::runtime_error(
                                "Reader length changed during character-class replacement"
                            );
                        }
                        replacement_views.resize(replacement_length);
                        replacement_reader.views(
                            0, replacement_length,
                            replacement_views.ptrs(),
                            replacement_views.lengths(),
                            replacement_views.encodings()
                        );
                        normalize_views(
                            replacement_views, replacement_converter,
                            replacement_storage, replacements
                        );
                    }

                    if (pattern_length > 0) {
                        pattern_reader.reset(pattern);
                        if (pattern_reader.size() != pattern_length) {
                            throw std::runtime_error(
                                "Reader length changed during character-class replacement"
                            );
                        }
                        pattern_views.resize(pattern_length);
                        pattern_reader.views(
                            0, pattern_length,
                            pattern_views.ptrs(), pattern_views.lengths(),
                            pattern_views.encodings()
                        );
                        normalize_views(
                            pattern_views, pattern_converter,
                            pattern_storage, patterns
                        );
                    }
                    compile_patterns(patterns, character_classes);

                    if (vectorize || pattern_length == 1) {
                        replace_vectorized(
                            subjects, replacements, character_classes,
                            vectorize_length, merge_value, ranges, output
                        );
                    }
                    else {
                        replace_sequential(
                            subjects, replacements, character_classes,
                            merge_value, sequential_output, ranges,
                            sequential_replacement_storage, output
                        );
                    }
                }
                else {
                    output.reset(0);
                }

                result = entry_protections.reprotect_one(
                    output.to_sexp(), result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        if (vectorize)
            emit_recycling_warning(recycling_warning);
    );
}


} } // namespace charr::altrep_backend
