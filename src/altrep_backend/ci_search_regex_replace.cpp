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

#include "ci_builder.h"
#include "io/reader_utils.h"
#include "ci_stringi.h"
#include "io/string_view.h"
#include "io/utf8_output.h"
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
#include <stdexcept>
#include <vector>

namespace charr { namespace altrep_backend {

namespace search_regex_replace {

CHARR_NEUTRAL_HELPER R_len_t recycling_length(
    R_len_t subject_length,
    R_len_t pattern_length,
    R_len_t replacement_length,
    bool& warning
) noexcept
{
    warning = false;
    if (subject_length <= 0 || pattern_length <= 0 ||
            replacement_length <= 0) {
        return 0;
    }

    R_len_t result = subject_length;
    if (pattern_length > result)
        result = pattern_length;
    if (replacement_length > result)
        result = replacement_length;
    warning = result % subject_length != 0 ||
        result % pattern_length != 0 ||
        result % replacement_length != 0;
    return result;
}


CHARR_R_HELPER R_len_t checked_length(SEXP value) noexcept
{
    const R_xlen_t length = XLENGTH(value);
    if (length > R_LEN_T_MAX)
        Rf_error("long character vectors are not supported");
    return static_cast<R_len_t>(length);
}


CHARR_CXX_HELPER shared::StringView normalize_input(
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


CHARR_CXX_HELPER void normalize_views(
    const charport::StrViews& input,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    std::vector<shared::StringView>& output
)
{
    output.resize(static_cast<std::size_t>(input.size()));
    for (R_xlen_t i = 0; i < input.size(); ++i) {
        output[static_cast<std::size_t>(i)] = normalize_input(
            io::as_shared_view(input[i]), converter, storage
        );
    }
}


CHARR_CXX_HELPER void load_utf16(
    const std::vector<shared::StringView>& input,
    std::vector<icu::UnicodeString>& output
)
{
    output.resize(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i].is_na())
            output[i].setToBogus();
        else
            shared::set_regex_utf16(output[i], input[i]);
    }
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


CHARR_CXX_HELPER shared::RegexReplaceResult replace_bound(
    shared::RegexMatcher& matcher,
    const shared::StringView& subject,
    const void* subject_identity,
    const icu::UnicodeString* replacement,
    shared::RegexReplaceMode mode,
    icu::UnicodeString& output,
    const shared::RegexPatterns& patterns,
    std::size_t pattern_index
)
{
    UErrorCode status = U_ZERO_ERROR;
    const shared::RegexReplaceResult result = matcher.replace(
        subject, subject_identity, replacement, mode, output, status
    );
    if (U_FAILURE(status))
        throw_regex_error(status, false, patterns, pattern_index);
    return result;
}


CHARR_CXX_HELPER shared::RegexReplaceResult replace_bound(
    shared::RegexMatcher& matcher,
    const icu::UnicodeString& subject,
    const icu::UnicodeString* replacement,
    shared::RegexReplaceMode mode,
    icu::UnicodeString& output,
    const shared::RegexPatterns& patterns,
    std::size_t pattern_index
)
{
    UErrorCode status = U_ZERO_ERROR;
    const shared::RegexReplaceResult result = matcher.replace(
        subject, replacement, mode, output, status
    );
    if (U_FAILURE(status))
        throw_regex_error(status, false, patterns, pattern_index);
    return result;
}


CHARR_CXX_HELPER shared::RegexReplaceResult replace_vectorized_record(
    const shared::StringView& subject,
    const shared::StringView& replacement,
    const icu::UnicodeString* scalar_replacement,
    bool replacement_is_scalar,
    const shared::RegexPatterns& patterns,
    const shared::RegexInput* scalar_pattern,
    bool* scalar_pattern_bound,
    std::size_t pattern_index,
    shared::RegexMatcher& matcher,
    shared::RegexReplaceMode mode,
    icu::UnicodeString& replacement_text,
    icu::UnicodeString& output_text
)
{
    const shared::RegexInput pattern = scalar_pattern == nullptr
        ? patterns.get(pattern_index)
        : *scalar_pattern;
    if (subject.is_na() || pattern.missing || pattern.length <= 0)
        return shared::RegexReplaceResult::missing;

    if (scalar_pattern_bound == nullptr || !*scalar_pattern_bound) {
        bind_pattern(matcher, pattern, patterns, pattern_index);
        if (scalar_pattern_bound != nullptr)
            *scalar_pattern_bound = true;
    }

    const icu::UnicodeString* replacement_value = scalar_replacement;
    if (!replacement_is_scalar) {
        replacement_value = nullptr;
        if (!replacement.is_na()) {
            shared::set_regex_utf16(replacement_text, replacement);
            replacement_value = &replacement_text;
        }
    }

    return replace_bound(
        matcher, subject, &subject, replacement_value, mode, output_text,
        patterns, pattern_index
    );
}


CHARR_CXX_HELPER void replace_sequential(
    std::vector<icu::UnicodeString>& subjects,
    const shared::RegexPatterns& patterns,
    const std::vector<icu::UnicodeString>& replacements,
    shared::RegexMatcher& matcher,
    icu::UnicodeString& output_text,
    int& additional_empty_warnings
)
{
    for (std::size_t pattern_index = 0;
            pattern_index < patterns.size(); ++pattern_index) {
        const shared::RegexInput pattern = patterns.get(pattern_index);
        if (pattern.missing) {
            for (std::size_t i = 0; i < subjects.size(); ++i)
                subjects[i].setToBogus();
            return;
        }
        if (pattern.length <= 0) {
            ++additional_empty_warnings;
            for (std::size_t i = 0; i < subjects.size(); ++i)
                subjects[i].setToBogus();
            return;
        }

        bind_pattern(matcher, pattern, patterns, pattern_index);
        const icu::UnicodeString& replacement = replacements[
            pattern_index % replacements.size()
        ];
        const icu::UnicodeString* replacement_value =
            replacement.isBogus() ? nullptr : &replacement;

        for (std::size_t i = 0; i < subjects.size(); ++i) {
            if (subjects[i].isBogus())
                continue;
            const shared::RegexReplaceResult replace_result = replace_bound(
                matcher, subjects[i], replacement_value,
                shared::RegexReplaceMode::all, output_text,
                patterns, pattern_index
            );
            if (replace_result == shared::RegexReplaceResult::missing)
                subjects[i].setToBogus();
            else
                subjects[i].setTo(output_text);
        }
    }
}


CHARR_CXX_HELPER void set_output(
    io::OutputBuilder& output,
    R_len_t index,
    const icu::UnicodeString& value,
    std::vector<char>& utf8_buffer
)
{
    int32_t utf8_length = 0;
    cetype_ext_t encoding = CETYPE_EXT_ASCII;
    const char* utf8 = ci::unicode_to_utf8(
        value, utf8_buffer, utf8_length, encoding
    );
    output.set_validated(
        index, io::OutputRecord{utf8, utf8_length, encoding}
    );
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


CHARR_R_HELPER void emit_empty_warnings(int count) noexcept
{
    for (int i = 0; i < count; ++i)
        Rf_warning(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
}

} // namespace search_regex_replace

using namespace search_regex_replace;


/** Replace every occurrence of a regular-expression pattern. */
CHARR_ENTRYPOINT SEXP ci_replace_all_regex(
    SEXP str,
    SEXP pattern,
    SEXP replacement,
    SEXP vectorize_all,
    SEXP opts_regex
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool vectorize = ci__prepare_arg_logical_1_notNA_r(
        vectorize_all, "vectorize_all"
    );

    R_len_t subject_length = 0;
    R_len_t pattern_length = 0;
    R_len_t replacement_length = 0;
    shared::RegexOptions options{0, 0, 0};
    R_len_t output_length = 0;
    bool empty_sequential = false;
    bool vectorized_core = false;
    bool recycling_warning = false;

    int empty_pattern_warnings = 0;
    int additional_empty_warnings = 0;

    try {
        charport::Reader subject_reader;
        charport::Reader pattern_reader;
        charport::Reader replacement_reader;
        charport::StrViews subject_views;
        charport::StrViews raw_pattern_views;
        charport::StrViews replacement_views;
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::NativeToUtf8 replacement_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        shared::SliceArena replacement_storage;
        std::vector<shared::StringView> subjects;
        std::vector<shared::StringView> pattern_views;
        std::vector<shared::StringView> replacements;
        shared::RegexPatterns patterns;
        shared::RegexMatcher matcher(shared::RegexOptions{0, 0, 0});
        icu::UnicodeString replacement_text;
        icu::UnicodeString output_text;
        std::vector<icu::UnicodeString> sequential_subjects;
        std::vector<icu::UnicodeString> sequential_replacements;
        std::vector<char> utf8_buffer;
        io::OutputBuilder output(0);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                str = callback_protections.protect_one(
                    ci__prepare_arg_string_r(str, "str")
                );
                subject_length = checked_length(str);
                empty_sequential = !vectorize && subject_length <= 0;

                if (vectorize) {
                    replacement = callback_protections.protect_one(
                        ci__prepare_arg_string_r(
                            replacement, "replacement"
                        )
                    );
                    pattern = callback_protections.protect_one(
                        ci__prepare_arg_string_r(pattern, "pattern")
                    );
                }
                else if (!empty_sequential) {
                    pattern = callback_protections.protect_one(
                        ci__prepare_arg_string_r(pattern, "pattern")
                    );
                    replacement = callback_protections.protect_one(
                        ci__prepare_arg_string_r(
                            replacement, "replacement"
                        )
                    );
                }

                pattern_length = empty_sequential
                    ? 0
                    : checked_length(pattern);
                replacement_length = empty_sequential
                    ? 0
                    : checked_length(replacement);
                output_length = subject_length;

                if (vectorize) {
                    options = regex::prepare_options(opts_regex);
                    output_length = recycling_length(
                        subject_length, pattern_length,
                        replacement_length, recycling_warning
                    );
                    if (recycling_warning)
                        Rf_warning(MSG__WARN_RECYCLING_RULE);
                }
                else if (!empty_sequential) {
                    options = regex::prepare_options(opts_regex);
                    require_sequential_lengths(
                        pattern_length, replacement_length
                    );
                    if (pattern_length % replacement_length != 0)
                        Rf_warning(MSG__WARN_RECYCLING_RULE);
                    if (pattern_length == 1)
                        options = regex::prepare_options(opts_regex);
                }

                vectorized_core = vectorize || pattern_length == 1;
                matcher.reset_options(options);

                output.reset(output_length);
                if (!empty_sequential && output_length > 0) {
                    subject_reader.reset(str);
                    if (subject_reader.size() != subject_length) {
                        throw std::runtime_error(
                            "Reader length changed during regex replacement"
                        );
                    }
                    subject_views.resize(subject_length);
                    subject_reader.views(
                        0, subject_length,
                        subject_views.ptrs(), subject_views.lengths(),
                        subject_views.encodings()
                    );
                    normalize_views(
                        subject_views, subject_converter, subject_storage,
                        subjects
                    );
                    if (!vectorized_core)
                        load_utf16(subjects, sequential_subjects);

                    pattern_reader.reset(pattern);
                    if (pattern_reader.size() != pattern_length) {
                        throw std::runtime_error(
                            "Reader length changed during regex replacement"
                        );
                    }
                    raw_pattern_views.resize(pattern_length);
                    pattern_reader.views(
                        0, pattern_length,
                        raw_pattern_views.ptrs(),
                        raw_pattern_views.lengths(),
                        raw_pattern_views.encodings()
                    );
                    normalize_views(
                        raw_pattern_views, pattern_converter,
                        pattern_storage, pattern_views
                    );
                    patterns.resize(
                        static_cast<std::size_t>(pattern_length)
                    );
                    for (R_len_t i = 0; i < pattern_length; ++i) {
                        patterns.set(
                            static_cast<std::size_t>(i),
                            pattern_views[static_cast<std::size_t>(i)]
                        );
                    }
                    empty_pattern_warnings = patterns.empty_count();

                    replacement_reader.reset(replacement);
                    if (replacement_reader.size() != replacement_length) {
                        throw std::runtime_error(
                            "Reader length changed during regex replacement"
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

                    if (vectorized_core) {
                        const bool scalar_pattern = pattern_length == 1;
                        const shared::RegexInput scalar_pattern_input =
                            scalar_pattern
                                ? patterns.get(0)
                                : shared::RegexInput{
                                    nullptr, nullptr, 0, true
                                };
                        bool scalar_pattern_bound = false;
                        const bool scalar_replacement =
                            replacement_length == 1;
                        const icu::UnicodeString* scalar_replacement_text =
                            nullptr;
                        if (scalar_replacement && !replacements[0].is_na()) {
                            shared::set_regex_utf16(
                                replacement_text, replacements[0]
                            );
                            scalar_replacement_text = &replacement_text;
                        }

                        for (R_len_t i = 0; i < output_length; ++i) {
                            const std::size_t subject_index =
                                subject_length == output_length
                                    ? static_cast<std::size_t>(i)
                                    : static_cast<std::size_t>(
                                        i % subject_length
                                    );
                            const std::size_t pattern_index =
                                scalar_pattern
                                    ? 0
                                    : static_cast<std::size_t>(
                                        i % pattern_length
                                    );
                            const std::size_t replacement_index =
                                scalar_replacement
                                    ? 0
                                    : static_cast<std::size_t>(
                                        i % replacement_length
                                    );
                            const shared::RegexReplaceResult replace_result =
                                replace_vectorized_record(
                                    subjects[subject_index],
                                    replacements[replacement_index],
                                    scalar_replacement_text,
                                    scalar_replacement,
                                    patterns,
                                    scalar_pattern
                                        ? &scalar_pattern_input
                                        : nullptr,
                                    scalar_pattern
                                        ? &scalar_pattern_bound
                                        : nullptr,
                                    pattern_index, matcher,
                                    shared::RegexReplaceMode::all,
                                    replacement_text, output_text
                                );
                            if (replace_result ==
                                    shared::RegexReplaceResult::missing) {
                                output.set_na(i);
                            }
                            else {
                                set_output(
                                    output, i, output_text, utf8_buffer
                                );
                            }
                        }
                    }
                    else {
                        load_utf16(
                            replacements, sequential_replacements
                        );
                        replace_sequential(
                            sequential_subjects, patterns,
                            sequential_replacements, matcher, output_text,
                            additional_empty_warnings
                        );
                        for (R_len_t i = 0; i < output_length; ++i) {
                            const icu::UnicodeString& value =
                                sequential_subjects[
                                    static_cast<std::size_t>(i)
                                ];
                            if (value.isBogus())
                                output.set_na(i);
                            else
                                set_output(output, i, value, utf8_buffer);
                        }
                    }
                }

                result = entry_protections.reprotect_one(
                    output.to_sexp(), result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        const int deferred_warnings =
            empty_pattern_warnings+additional_empty_warnings;
        emit_empty_warnings(deferred_warnings);
    );
}


/** Replace the first occurrence of a regular-expression pattern. */
CHARR_ENTRYPOINT SEXP ci_replace_first_regex(
    SEXP str,
    SEXP pattern,
    SEXP replacement,
    SEXP opts_regex
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    replacement = entry_protections.protect_one(
        ci__prepare_arg_string_r(
            replacement, "replacement"
        )
    );
    pattern = entry_protections.protect_one(
        ci__prepare_arg_string_r(pattern, "pattern")
    );
    const shared::RegexOptions options = regex::prepare_options(opts_regex);

    const R_len_t subject_length = checked_length(str);
    const R_len_t pattern_length = checked_length(pattern);
    const R_len_t replacement_length = checked_length(replacement);
    bool recycling_warning = false;
    const R_len_t output_length = recycling_length(
        subject_length, pattern_length, replacement_length,
        recycling_warning
    );
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);


    int empty_pattern_warnings = 0;

    try {
        charport::Reader subject_reader;
        charport::Reader pattern_reader;
        charport::Reader replacement_reader;
        charport::StrViews subject_views;
        charport::StrViews raw_pattern_views;
        charport::StrViews replacement_views;
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::NativeToUtf8 replacement_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        shared::SliceArena replacement_storage;
        std::vector<shared::StringView> subjects;
        std::vector<shared::StringView> pattern_views;
        std::vector<shared::StringView> replacements;
        shared::RegexPatterns patterns;
        shared::RegexMatcher matcher(options);
        icu::UnicodeString replacement_text;
        icu::UnicodeString output_text;
        std::vector<char> utf8_buffer;
        io::OutputBuilder output(0);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                output.reset(output_length);
                if (output_length > 0) {
                    subject_reader.reset(str);
                    if (subject_reader.size() != subject_length) {
                        throw std::runtime_error(
                            "Reader length changed during regex replacement"
                        );
                    }
                    subject_views.resize(subject_length);
                    subject_reader.views(
                        0, subject_length,
                        subject_views.ptrs(), subject_views.lengths(),
                        subject_views.encodings()
                    );
                    normalize_views(
                        subject_views, subject_converter, subject_storage,
                        subjects
                    );

                    pattern_reader.reset(pattern);
                    if (pattern_reader.size() != pattern_length) {
                        throw std::runtime_error(
                            "Reader length changed during regex replacement"
                        );
                    }
                    raw_pattern_views.resize(pattern_length);
                    pattern_reader.views(
                        0, pattern_length,
                        raw_pattern_views.ptrs(),
                        raw_pattern_views.lengths(),
                        raw_pattern_views.encodings()
                    );
                    normalize_views(
                        raw_pattern_views, pattern_converter,
                        pattern_storage, pattern_views
                    );
                    patterns.resize(
                        static_cast<std::size_t>(pattern_length)
                    );
                    for (R_len_t i = 0; i < pattern_length; ++i) {
                        patterns.set(
                            static_cast<std::size_t>(i),
                            pattern_views[static_cast<std::size_t>(i)]
                        );
                    }
                    empty_pattern_warnings = patterns.empty_count();

                    replacement_reader.reset(replacement);
                    if (replacement_reader.size() != replacement_length) {
                        throw std::runtime_error(
                            "Reader length changed during regex replacement"
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

                    const bool scalar_replacement =
                        replacement_length == 1;
                    const bool scalar_pattern = pattern_length == 1;
                    const shared::RegexInput scalar_pattern_input =
                        scalar_pattern
                            ? patterns.get(0)
                            : shared::RegexInput{
                                nullptr, nullptr, 0, true
                            };
                    bool scalar_pattern_bound = false;
                    const icu::UnicodeString* scalar_replacement_text =
                        nullptr;
                    if (scalar_replacement && !replacements[0].is_na()) {
                        shared::set_regex_utf16(
                            replacement_text, replacements[0]
                        );
                        scalar_replacement_text = &replacement_text;
                    }

                    for (R_len_t i = 0; i < output_length; ++i) {
                        const std::size_t subject_index =
                            subject_length == output_length
                                ? static_cast<std::size_t>(i)
                                : static_cast<std::size_t>(
                                    i % subject_length
                                );
                        const std::size_t pattern_index =
                            scalar_pattern
                                ? 0
                                : static_cast<std::size_t>(
                                    i % pattern_length
                                );
                        const std::size_t replacement_index =
                            scalar_replacement
                                ? 0
                                : static_cast<std::size_t>(
                                    i % replacement_length
                                );
                        const shared::RegexReplaceResult replace_result =
                            replace_vectorized_record(
                                subjects[subject_index],
                                replacements[replacement_index],
                                scalar_replacement_text,
                                scalar_replacement,
                                patterns,
                                scalar_pattern
                                    ? &scalar_pattern_input
                                    : nullptr,
                                scalar_pattern
                                    ? &scalar_pattern_bound
                                    : nullptr,
                                pattern_index, matcher,
                                shared::RegexReplaceMode::first,
                                replacement_text, output_text
                            );
                        if (replace_result ==
                                shared::RegexReplaceResult::missing) {
                            output.set_na(i);
                        }
                        else {
                            set_output(
                                output, i, output_text, utf8_buffer
                            );
                        }
                    }
                }

                result = entry_protections.reprotect_one(
                    output.to_sexp(), result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_empty_warnings(empty_pattern_warnings);
    );
}

} } // namespace charr::altrep_backend
