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
#include "fixed/options.h"
#include "io/string_view.h"
#include "../shared/entrypoint.h"
#include "../shared/fixed_search.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/replacement.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <cstddef>
#include <cstring>
#include <exception>
#include <limits>
#include <stdexcept>
#include <vector>

namespace charr { namespace base_backend {

namespace search_fixed_replace {

enum class ReplaceMode : unsigned char {
    first,
    all
};


struct ReplacementRecord {
    const char* data;
    R_len_t length;
    bool missing;
    bool changed;
};


struct DirectString {
    const char* data;
    R_len_t length;
    bool missing;
    bool had_bom;
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


CHARR_R_HELPER bool direct_string(
    SEXP value, DirectString& output
) noexcept
{
    if (value == NA_STRING) {
        output = DirectString{nullptr, 0, true, false};
        return true;
    }
    if (!IS_ASCII(value) && !IS_UTF8(value))
        return false;

    output = DirectString{
        CHAR(value), LENGTH(value), false, false
    };
    output.had_bom = IS_UTF8(value) && output.length >= 3 &&
        static_cast<unsigned char>(output.data[0]) == 0xefU &&
        static_cast<unsigned char>(output.data[1]) == 0xbbU &&
        static_cast<unsigned char>(output.data[2]) == 0xbfU;
    if (output.had_bom) {
        output.data += 3;
        output.length -= 3;
    }
    return true;
}


CHARR_NEUTRAL_HELPER R_len_t find_byte(
    const char* data, R_len_t length, unsigned char pattern
) noexcept
{
    for (R_len_t i = 0; i < length; ++i) {
        if (static_cast<unsigned char>(data[i]) == pattern)
            return i;
    }
    return -1;
}


CHARR_CXX_HELPER std::size_t checked_direct_size(
    R_len_t source_length,
    R_len_t replacement_length,
    std::size_t count
)
{
    const std::size_t source_size =
        static_cast<std::size_t>(source_length);
    const std::size_t replacement_size =
        static_cast<std::size_t>(replacement_length);
    if (count > source_size)
        throw std::length_error("fixed replacement match count overflow");

    const std::size_t unmatched = source_size-count;
    const std::size_t maximum = static_cast<std::size_t>(R_LEN_T_MAX);
    if (replacement_size > 0 && count >
            (maximum-unmatched)/replacement_size) {
        throw StriException(MSG__CHARSXP_2147483647);
    }
    return unmatched+count*replacement_size;
}


CHARR_CXX_HELPER ReplacementRecord replace_direct_byte(
    const DirectString& subject,
    unsigned char pattern,
    const DirectString& replacement,
    ReplaceMode mode,
    std::vector<char>& scratch
)
{
    if (subject.missing)
        return ReplacementRecord{nullptr, 0, true, false};

    const R_len_t first = find_byte(
        subject.data, subject.length, pattern
    );
    if (first < 0) {
        return ReplacementRecord{
            subject.data, subject.length, false, subject.had_bom
        };
    }
    if (replacement.missing)
        return ReplacementRecord{nullptr, 0, true, true};

    std::size_t count = 1;
    if (mode == ReplaceMode::all) {
        for (R_len_t i = first+1; i < subject.length; ++i) {
            if (static_cast<unsigned char>(subject.data[i]) == pattern)
                ++count;
        }
    }
    const std::size_t output_size = checked_direct_size(
        subject.length, replacement.length, count
    );
    scratch.resize(output_size);
    char* output = output_size > 0 ? scratch.data() : nullptr;

    std::size_t used = 0;
    R_len_t previous = 0;
    std::size_t replaced = 0;
    for (R_len_t i = first; i < subject.length; ++i) {
        if (static_cast<unsigned char>(subject.data[i]) != pattern)
            continue;

        const std::size_t prefix = static_cast<std::size_t>(i-previous);
        if (prefix > 0) {
            std::memcpy(output+used, subject.data+previous, prefix);
            used += prefix;
        }
        if (replacement.length > 0) {
            const std::size_t replacement_size =
                static_cast<std::size_t>(replacement.length);
            std::memcpy(output+used, replacement.data, replacement_size);
            used += replacement_size;
        }
        previous = i+1;
        ++replaced;
        if (mode == ReplaceMode::first)
            break;
    }

    const std::size_t suffix =
        static_cast<std::size_t>(subject.length-previous);
    if (suffix > 0) {
        std::memcpy(output+used, subject.data+previous, suffix);
        used += suffix;
    }
    if (replaced != count || used != output_size)
        throw std::logic_error("fixed replacement size mismatch");

    return ReplacementRecord{
        output_size > 0 ? output : replacement.data,
        static_cast<R_len_t>(output_size), false, true
    };
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


CHARR_CXX_HELPER std::size_t matched_bytes(
    const std::vector<shared::FixedRange>& ranges
)
{
    std::size_t total = 0;
    const std::size_t count = ranges.size();
    for (std::size_t i = 0; i < count; ++i) {
        const int length = ranges[i].end-ranges[i].start;
        if (length <= 0 || static_cast<std::size_t>(length) >
                std::numeric_limits<std::size_t>::max()-total) {
            throw std::length_error("fixed replacement match overflow");
        }
        total += static_cast<std::size_t>(length);
    }
    return total;
}


CHARR_CXX_HELPER ReplacementRecord replace_record(
    const shared::StringView& subject,
    const shared::StringView& pattern,
    const shared::StringView& replacement,
    ReplaceMode mode,
    shared::FixedSearchOptions options,
    shared::FixedMatcher& matcher,
    std::vector<shared::FixedRange>& ranges,
    std::vector<char>& scratch
)
{
    if (subject.is_na() || pattern.is_na() || pattern.len <= 0)
        return ReplacementRecord{nullptr, 0, true, false};

    ranges.clear();
    if (mode == ReplaceMode::first) {
        shared::FixedRange range{0, 0};
        if (!matcher.find_first(subject, pattern, options, range)) {
            return ReplacementRecord{
                subject.ptr, subject.len, false, false
            };
        }
        if (replacement.is_na())
            return ReplacementRecord{nullptr, 0, true, true};
        if (range.start < 0 || range.end <= range.start ||
                range.end > subject.len) {
            throw std::out_of_range(
                "fixed replacement range is out of bounds"
            );
        }

        const std::size_t prefix =
            static_cast<std::size_t>(range.start);
        const std::size_t suffix =
            static_cast<std::size_t>(subject.len-range.end);
        const std::size_t replacement_size =
            static_cast<std::size_t>(replacement.len);
        const std::size_t maximum =
            static_cast<std::size_t>(R_LEN_T_MAX);
        if (replacement_size > maximum-prefix-suffix)
            throw StriException(MSG__CHARSXP_2147483647);
        const std::size_t output_size =
            prefix+replacement_size+suffix;
        scratch.resize(output_size);
        char* output = output_size > 0 ? scratch.data() : nullptr;
        std::size_t used = 0;
        if (prefix > 0) {
            std::memcpy(output, subject.ptr, prefix);
            used += prefix;
        }
        if (replacement_size > 0) {
            std::memcpy(output+used, replacement.ptr, replacement_size);
            used += replacement_size;
        }
        if (suffix > 0) {
            std::memcpy(output+used, subject.ptr+range.end, suffix);
            used += suffix;
        }
        if (used != output_size)
            throw std::logic_error("fixed replacement size mismatch");
        return ReplacementRecord{
            output_size > 0 ? output : replacement.ptr,
            static_cast<R_len_t>(output_size), false, true
        };
    }
    else {
        matcher.find_all(subject, pattern, options, ranges);
    }

    if (ranges.size() == 0) {
        return ReplacementRecord{
            subject.ptr, subject.len, false, false
        };
    }
    if (replacement.is_na())
        return ReplacementRecord{nullptr, 0, true, true};

    const std::size_t output_size = shared::checked_replacement_size(
        subject, matched_bytes(ranges), ranges, replacement
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
    const std::vector<shared::StringView>& patterns,
    const std::vector<shared::StringView>& replacements,
    shared::FixedSearchOptions options,
    shared::FixedMatcher& matcher,
    std::vector<shared::FixedRange>& ranges,
    shared::SliceArena& output_storage,
    std::vector<shared::StringView>& output,
    std::vector<unsigned char>& changed,
    bool& all_missing,
    R_len_t& extra_empty_warnings
)
{
    output.resize(subjects.size());
    for (std::size_t i = 0; i < subjects.size(); ++i)
        output[i] = subjects[i];
    changed.assign(subjects.size(), 0);
    all_missing = false;
    extra_empty_warnings = 0;

    const std::size_t pattern_length = patterns.size();
    const std::size_t replacement_length = replacements.size();
    for (std::size_t pattern_index = 0;
            pattern_index < pattern_length; ++pattern_index) {
        const shared::StringView& pattern = patterns[pattern_index];
        if (pattern.is_na()) {
            all_missing = true;
            return;
        }
        if (pattern.len <= 0) {
            all_missing = true;
            extra_empty_warnings = 1;
            return;
        }

        const shared::StringView& replacement = replacements[
            pattern_index % replacement_length
        ];
        for (std::size_t subject_index = 0;
                subject_index < output.size(); ++subject_index) {
            const shared::StringView subject = output[subject_index];
            if (subject.is_na())
                continue;

            matcher.find_all(subject, pattern, options, ranges);
            if (ranges.size() == 0)
                continue;
            if (replacement.is_na()) {
                output[subject_index] = shared::StringView{
                    nullptr, shared::missing_string_length,
                    shared::StringEncoding::missing
                };
                changed[subject_index] = 1;
                continue;
            }

            const std::size_t output_size =
                shared::checked_replacement_size(
                    subject, matched_bytes(ranges), ranges, replacement
                );
            char* bytes = output_size > 0
                ? output_storage.allocate(output_size)
                : nullptr;
            shared::write_replacement(
                subject, ranges, replacement, bytes, output_size
            );
            output[subject_index] = shared::StringView{
                output_size > 0 ? bytes : replacement.ptr,
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


CHARR_R_HELPER void emit_warnings(
    bool recycling_warning,
    R_len_t empty_pattern_warnings
) noexcept
{
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

} // namespace search_fixed_replace

using namespace search_fixed_replace;


CHARR_ENTRYPOINT SEXP ci_replace_all_fixed(
    SEXP str,
    SEXP pattern,
    SEXP replacement,
    SEXP vectorize_all,
    SEXP opts_fixed
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool vectorize = ci__prepare_arg_logical_1_notNA_r(
        vectorize_all, "vectorize_all"
    );

    shared::FixedSearchOptions options{false, false};
    if (vectorize)
        options = fixed::prepare_options(opts_fixed, false);

    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    const R_len_t subject_length = LENGTH(str);
    const bool empty_sequential = !vectorize && subject_length <= 0;
    pattern = entry_protections.protect_one(empty_sequential
        ? R_NilValue
        : ci__prepare_arg_string_r(pattern, "pattern"));
    replacement = entry_protections.protect_one(empty_sequential
        ? R_NilValue
        : ci__prepare_arg_string_r(replacement, "replacement"));

    const R_len_t pattern_length = empty_sequential ? 0 : LENGTH(pattern);
    const R_len_t replacement_length = empty_sequential
        ? 0
        : LENGTH(replacement);
    R_len_t vectorize_length = subject_length;
    bool recycling_warning = false;
    if (vectorize) {
        vectorize_length = recycling_length(
            subject_length, pattern_length, replacement_length,
            recycling_warning
        );
    }
    else if (!empty_sequential) {
        require_sequential_lengths(pattern_length, replacement_length);
        if (pattern_length % replacement_length != 0)
            Rf_warning(MSG__WARN_RECYCLING_RULE);
        options = fixed::prepare_options(opts_fixed, false);
    }

    const bool vectorized_core = vectorize || pattern_length == 1;

    R_len_t empty_pattern_warnings = 0;
    R_len_t extra_empty_warnings = 0;

    try {
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::NativeToUtf8 replacement_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        shared::SliceArena replacement_storage;
        shared::SliceArena sequential_output_storage;
        std::vector<shared::StringView> subjects;
        std::vector<shared::StringView> patterns;
        std::vector<shared::StringView> replacements;
        shared::FixedMatcher matcher;
        std::vector<shared::FixedRange> ranges;
        std::vector<char> output_scratch;
        std::vector<shared::StringView> sequential_output;
        std::vector<unsigned char> sequential_changed;
        bool sequential_all_missing = false;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                result = entry_protections.reprotect_one(
                    Rf_allocVector(STRSXP, vectorize_length),
                    result_index
                );
                if (!empty_sequential && vectorize_length > 0) {
                    R_len_t general_start = 0;
                    bool direct = false;
                    if (vectorized_core && !options.case_insensitive &&
                            !options.overlap && pattern_length == 1 &&
                            replacement_length == 1) {
                        DirectString direct_pattern;
                        DirectString direct_replacement;
                        direct = direct_string(
                            STRING_ELT(pattern, 0), direct_pattern
                        ) && !direct_pattern.missing &&
                            direct_pattern.length == 1 &&
                            static_cast<unsigned char>(
                                direct_pattern.data[0]
                            ) <= 0x7fU &&
                            direct_string(
                                STRING_ELT(replacement, 0),
                                direct_replacement
                            );
                        if (direct) {
                            const unsigned char pattern_byte =
                                static_cast<unsigned char>(
                                    direct_pattern.data[0]
                                );
                            for (R_len_t i = 0;
                                    i < vectorize_length; ++i) {
                                DirectString direct_subject;
                                if (!direct_string(
                                        STRING_ELT(str, i),
                                        direct_subject)) {
                                    general_start = i;
                                    direct = false;
                                    break;
                                }
                                const ReplacementRecord record =
                                    replace_direct_byte(
                                        direct_subject, pattern_byte,
                                        direct_replacement,
                                        ReplaceMode::all, output_scratch
                                    );
                                const shared::StringView normalized{
                                    direct_subject.data,
                                    direct_subject.missing
                                        ? shared::missing_string_length
                                        : direct_subject.length,
                                    direct_subject.missing
                                        ? shared::StringEncoding::missing
                                        : shared::StringEncoding::utf8
                                };
                                install_record(
                                    result, i, STRING_ELT(str, i),
                                    normalized, record
                                );
                            }
                        }
                    }

                    if (!direct) {
                        const SEXP* values = STRING_PTR_RO(str);
                        subjects.resize(
                            static_cast<std::size_t>(subject_length)
                        );
                        for (R_len_t i = 0; i < subject_length; ++i) {
                            subjects[static_cast<std::size_t>(i)] =
                                normalize_input(
                                    io::as_shared_view(values[i]),
                                    subject_converter, subject_storage
                                );
                        }

                        values = STRING_PTR_RO(replacement);
                        replacements.resize(
                            static_cast<std::size_t>(replacement_length)
                        );
                        for (R_len_t i = 0;
                                i < replacement_length; ++i) {
                            replacements[static_cast<std::size_t>(i)] =
                                normalize_input(
                                    io::as_shared_view(values[i]),
                                    replacement_converter,
                                    replacement_storage
                                );
                        }

                        values = STRING_PTR_RO(pattern);
                        patterns.resize(
                            static_cast<std::size_t>(pattern_length)
                        );
                        R_len_t normalized_empty_patterns = 0;
                        for (R_len_t i = 0; i < pattern_length; ++i) {
                            const shared::StringView normalized =
                                normalize_input(
                                    io::as_shared_view(values[i]),
                                    pattern_converter, pattern_storage
                                );
                            patterns[static_cast<std::size_t>(i)] =
                                normalized;
                            if (!normalized.is_na() && normalized.len <= 0)
                                ++normalized_empty_patterns;
                        }
                        empty_pattern_warnings =
                            normalized_empty_patterns;

                        if (vectorized_core) {
                            for (R_len_t lane = 0;
                                    lane < pattern_length; ++lane) {
                                R_len_t i = general_start > 0
                                    ? general_start
                                    : lane;
                                for (;;) {
                                    const R_len_t source_index =
                                        i % subject_length;
                                    const std::size_t subject_offset =
                                        static_cast<std::size_t>(
                                            source_index
                                        );
                                    const ReplacementRecord record =
                                        replace_record(
                                            subjects[subject_offset],
                                            patterns[
                                                static_cast<std::size_t>(lane)
                                            ],
                                            replacements[
                                                static_cast<std::size_t>(
                                                    i % replacement_length
                                                )
                                            ],
                                            ReplaceMode::all, options,
                                            matcher, ranges, output_scratch
                                        );
                                    install_record(
                                        result, i,
                                        STRING_ELT(str, source_index),
                                        subjects[subject_offset], record
                                    );

                                    if (pattern_length >=
                                            vectorize_length-i) {
                                        break;
                                    }
                                    i += pattern_length;
                                }
                            }
                        }
                        else {
                            compute_sequential(
                                subjects, patterns, replacements,
                                options, matcher, ranges,
                                sequential_output_storage,
                                sequential_output,
                                sequential_changed,
                                sequential_all_missing,
                                extra_empty_warnings
                            );

                            for (R_len_t i = 0;
                                    i < subject_length; ++i) {
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
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        const R_len_t deferred_empty_warnings =
            empty_pattern_warnings+extra_empty_warnings;
        emit_warnings(
            vectorize && recycling_warning,
            deferred_empty_warnings
        );
    );
}


CHARR_ENTRYPOINT SEXP ci_replace_first_fixed(
    SEXP str,
    SEXP pattern,
    SEXP replacement,
    SEXP opts_fixed
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const shared::FixedSearchOptions options = fixed::prepare_options(
        opts_fixed, false
    );
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    pattern = entry_protections.protect_one(ci__prepare_arg_string_r(pattern, "pattern"));
    replacement = entry_protections.protect_one(ci__prepare_arg_string_r(
        replacement, "replacement"
    ));

    const R_len_t subject_length = LENGTH(str);
    const R_len_t pattern_length = LENGTH(pattern);
    const R_len_t replacement_length = LENGTH(replacement);
    bool recycling_warning = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, replacement_length,
        recycling_warning
    );

    R_len_t empty_pattern_warnings = 0;

    try {
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::NativeToUtf8 replacement_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        shared::SliceArena replacement_storage;
        std::vector<shared::StringView> subjects;
        std::vector<shared::StringView> patterns;
        std::vector<shared::StringView> replacements;
        shared::FixedMatcher matcher;
        std::vector<shared::FixedRange> ranges;
        std::vector<char> output_scratch;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                result = entry_protections.reprotect_one(
                    Rf_allocVector(STRSXP, vectorize_length),
                    result_index
                );
                if (vectorize_length > 0) {
                    R_len_t general_start = 0;
                    bool direct = false;
                    if (!options.case_insensitive && !options.overlap &&
                            pattern_length == 1 &&
                            replacement_length == 1) {
                        DirectString direct_pattern;
                        DirectString direct_replacement;
                        direct = direct_string(
                            STRING_ELT(pattern, 0), direct_pattern
                        ) && !direct_pattern.missing &&
                            direct_pattern.length == 1 &&
                            static_cast<unsigned char>(
                                direct_pattern.data[0]
                            ) <= 0x7fU &&
                            direct_string(
                                STRING_ELT(replacement, 0),
                                direct_replacement
                            );
                        if (direct) {
                            const unsigned char pattern_byte =
                                static_cast<unsigned char>(
                                    direct_pattern.data[0]
                                );
                            for (R_len_t i = 0;
                                    i < vectorize_length; ++i) {
                                DirectString direct_subject;
                                if (!direct_string(
                                        STRING_ELT(str, i),
                                        direct_subject)) {
                                    general_start = i;
                                    direct = false;
                                    break;
                                }
                                const ReplacementRecord record =
                                    replace_direct_byte(
                                        direct_subject, pattern_byte,
                                        direct_replacement,
                                        ReplaceMode::first, output_scratch
                                    );
                                const shared::StringView normalized{
                                    direct_subject.data,
                                    direct_subject.missing
                                        ? shared::missing_string_length
                                        : direct_subject.length,
                                    direct_subject.missing
                                        ? shared::StringEncoding::missing
                                        : shared::StringEncoding::utf8
                                };
                                install_record(
                                    result, i, STRING_ELT(str, i),
                                    normalized, record
                                );
                            }
                        }
                    }

                    if (!direct) {
                        const SEXP* values = STRING_PTR_RO(str);
                        subjects.resize(
                            static_cast<std::size_t>(subject_length)
                        );
                        for (R_len_t i = 0; i < subject_length; ++i) {
                            subjects[static_cast<std::size_t>(i)] =
                                normalize_input(
                                    io::as_shared_view(values[i]),
                                    subject_converter, subject_storage
                                );
                        }

                        values = STRING_PTR_RO(replacement);
                        replacements.resize(
                            static_cast<std::size_t>(replacement_length)
                        );
                        for (R_len_t i = 0;
                                i < replacement_length; ++i) {
                            replacements[static_cast<std::size_t>(i)] =
                                normalize_input(
                                    io::as_shared_view(values[i]),
                                    replacement_converter,
                                    replacement_storage
                                );
                        }

                        values = STRING_PTR_RO(pattern);
                        patterns.resize(
                            static_cast<std::size_t>(pattern_length)
                        );
                        R_len_t normalized_empty_patterns = 0;
                        for (R_len_t i = 0; i < pattern_length; ++i) {
                            const shared::StringView normalized =
                                normalize_input(
                                    io::as_shared_view(values[i]),
                                    pattern_converter, pattern_storage
                                );
                            patterns[static_cast<std::size_t>(i)] =
                                normalized;
                            if (!normalized.is_na() && normalized.len <= 0)
                                ++normalized_empty_patterns;
                        }
                        empty_pattern_warnings =
                            normalized_empty_patterns;

                        for (R_len_t lane = 0;
                                lane < pattern_length; ++lane) {
                            R_len_t i = general_start > 0
                                ? general_start
                                : lane;
                            for (;;) {
                                const R_len_t source_index =
                                    i % subject_length;
                                const std::size_t subject_offset =
                                    static_cast<std::size_t>(source_index);
                                const ReplacementRecord record =
                                    replace_record(
                                        subjects[subject_offset],
                                        patterns[
                                            static_cast<std::size_t>(lane)
                                        ],
                                        replacements[
                                            static_cast<std::size_t>(
                                                i % replacement_length
                                            )
                                        ],
                                        ReplaceMode::first, options,
                                        matcher, ranges, output_scratch
                                    );
                                install_record(
                                    result, i,
                                    STRING_ELT(str, source_index),
                                    subjects[subject_offset], record
                                );

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
        emit_warnings(recycling_warning, empty_pattern_warnings);
    );
}

} } // namespace charr::base_backend
