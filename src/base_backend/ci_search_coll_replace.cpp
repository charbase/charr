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
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <cstddef>
#include <exception>
#include <vector>

namespace charr { namespace base_backend {

namespace search_coll_replace {

enum class ReplaceMode : unsigned char {
    first,
    all
};


struct ReplacementResult {
    shared::CollationInput value;
    bool changed;
};


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


CHARR_CXX_HELPER ReplacementResult replace_record(
    const shared::CollationInput& subject,
    const shared::CollationInput& pattern,
    const shared::CollationInput& replacement,
    ReplaceMode mode,
    UCollator* collator,
    shared::CollationMatcher& matcher,
    std::vector<shared::CollationRange>& ranges,
    icu::UnicodeString& output
)
{
    ranges.clear();
    UErrorCode status = U_ZERO_ERROR;
    if (mode == ReplaceMode::first) {
        shared::CollationRange range{0, 0};
        if (matcher.find_first(
                collator, subject, pattern, range, status)) {
            ranges.push_back(range);
        }
    }
    else {
        matcher.find_all(
            collator, subject, pattern, ranges, status
        );
    }
    require_icu_success(status);

    if (ranges.size() == 0)
        return ReplacementResult{subject, false};
    if (replacement.missing) {
        return ReplacementResult{
            shared::CollationInput{nullptr, nullptr, -1, true}, true
        };
    }

    shared::write_collation_replacement(
        subject, replacement, ranges, output
    );
    return ReplacementResult{
        shared::CollationInput{
            &output, output.getBuffer(), output.length(), false
        },
        true
    };
}


CHARR_CXX_HELPER shared::CollationUtf8Slice utf8_value(
    const shared::CollationInput& value,
    std::vector<char>& buffer
)
{
    UErrorCode status = U_ZERO_ERROR;
    const shared::CollationUtf8Slice result =
        shared::collation_utf8_slice(
            value,
            shared::CollationRange{0, value.length},
            buffer,
            status
        );
    require_icu_success(status);
    return result;
}


CHARR_CXX_HELPER void replace_sequential(
    shared::CollationInputs& subjects,
    const shared::CollationInputs& patterns,
    const shared::CollationInputs& replacements,
    UCollator* collator,
    shared::CollationMatcher& matcher,
    std::vector<shared::CollationRange>& ranges,
    icu::UnicodeString& output,
    bool& all_missing,
    R_len_t& extra_empty_warnings
)
{
    all_missing = false;
    extra_empty_warnings = 0;

    for (std::size_t pattern_index = 0;
            pattern_index < patterns.size(); ++pattern_index) {
        const shared::CollationInput pattern = patterns.get(pattern_index);
        if (pattern.missing) {
            all_missing = true;
            return;
        }
        if (pattern.length <= 0) {
            all_missing = true;
            extra_empty_warnings = 1;
            return;
        }

        const shared::CollationInput replacement = replacements.get(
            pattern_index % replacements.size()
        );
        for (std::size_t subject_index = 0;
                subject_index < subjects.size(); ++subject_index) {
            const shared::CollationInput subject =
                subjects.get(subject_index);
            if (subject.missing || subject.length <= 0)
                continue;

            const ReplacementResult replaced = replace_record(
                subject, pattern, replacement, ReplaceMode::all,
                collator, matcher, ranges, output
            );
            if (!replaced.changed)
                continue;
            if (replaced.value.missing)
                subjects.set_missing(subject_index);
            else
                subjects.swap_value(subject_index, output);
        }
    }
}


CHARR_R_HELPER void set_missing_r(
    SEXP output, R_len_t index
) noexcept
{
    SET_STRING_ELT(output, index, NA_STRING);
}


CHARR_R_HELPER void set_utf8_slice_r(
    SEXP output,
    R_len_t index,
    const shared::CollationUtf8Slice& value
) noexcept
{
    SET_STRING_ELT(
        output, index,
        Rf_mkCharLenCE(value.data, value.length, CE_UTF8)
    );
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

} // namespace search_coll_replace

using namespace search_coll_replace;


/** Replace the first collation-aware pattern occurrence. */
CHARR_ENTRYPOINT SEXP ci_replace_first_coll(
    SEXP str,
    SEXP pattern,
    SEXP replacement,
    SEXP opts_collator
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    replacement = entry_protections.protect_one(ci__prepare_arg_string_r(
        replacement, "replacement"
    ));
    pattern = entry_protections.protect_one(ci__prepare_arg_string_r(pattern, "pattern"));
    const shared::CollatorOptions options =
        collator::prepare_options(opts_collator);

    const R_len_t subject_length = LENGTH(str);
    const R_len_t pattern_length = LENGTH(pattern);
    const R_len_t replacement_length = LENGTH(replacement);
    bool recycling_needed = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, replacement_length,
        recycling_needed
    );

    bool root_fallback_warning = false;
    bool recycling_warning = false;
    R_len_t empty_pattern_warnings = 0;

    try {
        shared::Collator collator_owner;
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::NativeToUtf8 replacement_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        shared::SliceArena replacement_storage;
        shared::CollationInputs subjects;
        shared::CollationInputs patterns;
        shared::CollationInputs replacements;
        shared::CollationMatcher matcher;
        std::vector<shared::CollationRange> ranges;
        icu::UnicodeString replacement_output;
        std::vector<char> utf8_buffer;

        ranges.reserve(4);

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
                    const SEXP* values = STRING_PTR_RO(str);
                    for (R_len_t i = 0; i < subject_length; ++i) {
                        subjects.set(
                            static_cast<std::size_t>(i),
                            normalize_view(
                                io::as_shared_view(values[i]),
                                subject_converter, subject_storage
                            )
                        );
                    }

                    patterns.resize(
                        static_cast<std::size_t>(pattern_length)
                    );
                    values = STRING_PTR_RO(pattern);
                    for (R_len_t i = 0; i < pattern_length; ++i) {
                        patterns.set(
                            static_cast<std::size_t>(i),
                            normalize_view(
                                io::as_shared_view(values[i]),
                                pattern_converter, pattern_storage
                            )
                        );
                    }
                    empty_pattern_warnings =
                        count_empty_patterns(patterns);

                    replacements.resize(
                        static_cast<std::size_t>(replacement_length)
                    );
                    values = STRING_PTR_RO(replacement);
                    for (R_len_t i = 0; i < replacement_length; ++i) {
                        replacements.set(
                            static_cast<std::size_t>(i),
                            normalize_view(
                                io::as_shared_view(values[i]),
                                replacement_converter,
                                replacement_storage
                            )
                        );
                    }
                }

                result = entry_protections.reprotect_one(
                    Rf_allocVector(STRSXP, vectorize_length),
                    result_index
                );
                for (R_len_t lane = 0;
                        lane < (vectorize_length > 0
                            ? pattern_length : 0);
                        ++lane) {
                    const shared::CollationInput prepared_pattern =
                        patterns.get(static_cast<std::size_t>(lane));
                    R_len_t i = lane;
                    for (;;) {
                        const shared::CollationInput subject = subjects.get(
                            static_cast<std::size_t>(i % subject_length)
                        );
                        shared::CollationInput output = subject;
                        bool missing = subject.missing ||
                            prepared_pattern.missing ||
                            prepared_pattern.length <= 0;
                        if (!missing && subject.length > 0) {
                            const ReplacementResult replaced =
                                replace_record(
                                    subject, prepared_pattern,
                                    replacements.get(
                                        static_cast<std::size_t>(
                                            i % replacement_length
                                        )
                                    ),
                                    ReplaceMode::first,
                                    collator_owner.get(), matcher,
                                    ranges, replacement_output
                                );
                            output = replaced.value;
                            missing = output.missing;
                        }

                        if (missing) {
                            set_missing_r(result, i);
                        }
                        else {
                            const shared::CollationUtf8Slice utf8 =
                                utf8_value(output, utf8_buffer);
                            set_utf8_slice_r(result, i, utf8);
                        }

                        if (pattern_length >= vectorize_length-i)
                            break;
                        i += pattern_length;
                    }
                }

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


/** Replace every collation-aware pattern occurrence. */
CHARR_ENTRYPOINT SEXP ci_replace_all_coll(
    SEXP str,
    SEXP pattern,
    SEXP replacement,
    SEXP vectorize_all,
    SEXP opts_collator
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool vectorize = ci__prepare_arg_logical_1_notNA_r(
        vectorize_all, "vectorize_all"
    );

    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    R_len_t subject_length = 0;
    bool empty_sequential = false;
    if (!vectorize) {
        subject_length = LENGTH(str);
        empty_sequential = subject_length <= 0;
    }
    SEXP first_followup = entry_protections.protect_one(empty_sequential
        ? R_NilValue
        : ci__prepare_arg_string_r(
            vectorize ? replacement : pattern,
            vectorize ? "replacement" : "pattern"
        ));
    SEXP second_followup = entry_protections.protect_one(empty_sequential
        ? R_NilValue
        : ci__prepare_arg_string_r(
            vectorize ? pattern : replacement,
            vectorize ? "pattern" : "replacement"
        ));
    if (vectorize) {
        replacement = first_followup;
        pattern = second_followup;
    }
    else {
        pattern = first_followup;
        replacement = second_followup;
    }

    shared::CollatorOptions options{};
    R_len_t pattern_length = 0;
    R_len_t replacement_length = 0;
    R_len_t vectorize_length = subject_length;
    bool recycling_needed = false;
    if (vectorize) {
        options = collator::prepare_options(opts_collator);
        subject_length = LENGTH(str);
        pattern_length = LENGTH(pattern);
        replacement_length = LENGTH(replacement);
        vectorize_length = recycling_length(
            subject_length, pattern_length, replacement_length,
            recycling_needed
        );
    }
    else if (!empty_sequential) {
        pattern_length = LENGTH(pattern);
        replacement_length = LENGTH(replacement);
        require_sequential_lengths(
            pattern_length, replacement_length
        );
        if (pattern_length % replacement_length != 0)
            Rf_warning(MSG__WARN_RECYCLING_RULE);
        options = collator::prepare_options(opts_collator);
    }
    const bool vectorized_core = vectorize || pattern_length == 1;

    bool root_fallback_warning = false;
    bool recycling_warning = false;
    R_len_t empty_pattern_warnings = 0;
    R_len_t extra_empty_warnings = 0;

    try {
        shared::Collator collator_owner;
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::NativeToUtf8 replacement_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        shared::SliceArena replacement_storage;
        shared::CollationInputs subjects;
        shared::CollationInputs patterns;
        shared::CollationInputs replacements;
        shared::CollationMatcher matcher;
        std::vector<shared::CollationRange> ranges;
        icu::UnicodeString replacement_output;
        std::vector<char> utf8_buffer;
        bool sequential_all_missing = false;

        ranges.reserve(4);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                if (!empty_sequential) {
                    const shared::CollatorOpenResult opened =
                        collator_owner.reset(options);
                    root_fallback_warning = opened.root_fallback;
                    require_icu_success(opened.status);
                    recycling_warning = vectorize && recycling_needed;
                }

                if (!empty_sequential && vectorize_length > 0) {
                    subjects.resize(
                        static_cast<std::size_t>(subject_length)
                    );
                    const SEXP* values = STRING_PTR_RO(str);
                    for (R_len_t i = 0; i < subject_length; ++i) {
                        subjects.set(
                            static_cast<std::size_t>(i),
                            normalize_view(
                                io::as_shared_view(values[i]),
                                subject_converter, subject_storage
                            )
                        );
                    }

                    patterns.resize(
                        static_cast<std::size_t>(pattern_length)
                    );
                    values = STRING_PTR_RO(pattern);
                    for (R_len_t i = 0; i < pattern_length; ++i) {
                        patterns.set(
                            static_cast<std::size_t>(i),
                            normalize_view(
                                io::as_shared_view(values[i]),
                                pattern_converter, pattern_storage
                            )
                        );
                    }
                    empty_pattern_warnings =
                        count_empty_patterns(patterns);

                    replacements.resize(
                        static_cast<std::size_t>(replacement_length)
                    );
                    values = STRING_PTR_RO(replacement);
                    for (R_len_t i = 0; i < replacement_length; ++i) {
                        replacements.set(
                            static_cast<std::size_t>(i),
                            normalize_view(
                                io::as_shared_view(values[i]),
                                replacement_converter,
                                replacement_storage
                            )
                        );
                    }

                    if (!vectorized_core) {
                        replace_sequential(
                            subjects, patterns, replacements,
                            collator_owner.get(), matcher, ranges,
                            replacement_output,
                            sequential_all_missing,
                            extra_empty_warnings
                        );
                    }
                }

                result = entry_protections.reprotect_one(
                    Rf_allocVector(STRSXP, vectorize_length),
                    result_index
                );
                if (vectorized_core) {
                    for (R_len_t lane = 0;
                            lane < (vectorize_length > 0
                                ? pattern_length : 0);
                            ++lane) {
                        const shared::CollationInput prepared_pattern =
                            patterns.get(static_cast<std::size_t>(lane));
                        R_len_t i = lane;
                        for (;;) {
                            const shared::CollationInput subject =
                                subjects.get(
                                    static_cast<std::size_t>(
                                        i % subject_length
                                    )
                                );
                            shared::CollationInput output = subject;
                            bool missing = subject.missing ||
                                prepared_pattern.missing ||
                                prepared_pattern.length <= 0;
                            if (!missing && subject.length > 0) {
                                const ReplacementResult replaced =
                                    replace_record(
                                        subject, prepared_pattern,
                                        replacements.get(
                                            static_cast<std::size_t>(
                                                i % replacement_length
                                            )
                                        ),
                                        ReplaceMode::all,
                                        collator_owner.get(), matcher,
                                        ranges, replacement_output
                                    );
                                output = replaced.value;
                                missing = output.missing;
                            }

                            if (missing) {
                                set_missing_r(result, i);
                            }
                            else {
                                const shared::CollationUtf8Slice utf8 =
                                    utf8_value(output, utf8_buffer);
                                set_utf8_slice_r(result, i, utf8);
                            }

                            if (pattern_length >= vectorize_length-i)
                                break;
                            i += pattern_length;
                        }
                    }
                }
                else {
                    for (R_len_t i = 0; i < subject_length; ++i) {
                        if (sequential_all_missing) {
                            set_missing_r(result, i);
                            continue;
                        }

                        const shared::CollationInput output = subjects.get(
                            static_cast<std::size_t>(i)
                        );
                        if (output.missing) {
                            set_missing_r(result, i);
                        }
                        else {
                            const shared::CollationUtf8Slice utf8 =
                                utf8_value(output, utf8_buffer);
                            set_utf8_slice_r(result, i, utf8);
                        }
                    }
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        const R_len_t deferred_empty_warnings =
            empty_pattern_warnings+extra_empty_warnings;
        emit_warnings(
            root_fallback_warning,
            recycling_warning,
            deferred_empty_warnings
        );
    );
}

} } // namespace charr::base_backend
