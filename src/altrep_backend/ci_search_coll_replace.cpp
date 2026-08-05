
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
#include "ci_parallel.h"
#include "io/reader_utils.h"
#include "collator/options.h"
#include "io/string_view.h"
#include "io/utf8_output.h"
#include "../shared/collation_search.h"
#include "../shared/collator.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <charport.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <vector>

namespace charr { namespace altrep_backend {

namespace search_coll_replace {

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


CHARR_CXX_HELPER int count_empty_patterns(
    const shared::CollationInputs& patterns
) noexcept
{
    int result = 0;
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        const shared::CollationInput pattern = patterns.get(i);
        if (!pattern.missing && pattern.length <= 0)
            ++result;
    }
    return result;
}


CHARR_CXX_HELPER void set_utf16_output(
    const shared::CollationInput& value,
    R_len_t output_index,
    std::vector<char>& utf8_buffer,
    io::OutputBuilder& output
)
{
    UErrorCode status = U_ZERO_ERROR;
    const shared::CollationUtf8Slice utf8 =
        shared::collation_utf8_slice(
            value,
            shared::CollationRange{0, value.length},
            utf8_buffer, status
        );
    require_icu_success(status);
    output.set(
        output_index, utf8.data,
        static_cast<std::size_t>(utf8.length),
        utf8.ascii ? CETYPE_EXT_ASCII : CETYPE_EXT_UTF8
    );
}


CHARR_NEUTRAL_HELPER bool unchanged_utf8_is_lossless(
    const shared::StringView& value
) noexcept
{
    if (value.enc == shared::StringEncoding::ascii)
        return true;
    if (value.enc != shared::StringEncoding::utf8 &&
            value.enc != shared::StringEncoding::ascii_or_utf8) {
        return false;
    }

    const std::uint8_t* data =
        reinterpret_cast<const std::uint8_t*>(value.ptr);
    int offset = 0;
    while (offset < value.len) {
        UChar32 code_point = 0;
        U8_NEXT(data, offset, value.len, code_point);
        if (code_point < 0)
            return false;
    }
    return true;
}


CHARR_CXX_HELPER void replace_one(
    const shared::StringView& source,
    const void* source_identity,
    const shared::CollationInput& pattern,
    const shared::CollationInput& replacement,
    UCollator* collator,
    bool replace_all,
    R_len_t output_index,
    shared::CollationCursor& subject_cursor,
    shared::CollationMatcher& matcher,
    std::vector<shared::CollationRange>& ranges,
    icu::UnicodeString& replacement_scratch,
    std::vector<char>& utf8_buffer,
    io::OutputBuilder& output
)
{
    if (source.is_na() || pattern.missing || pattern.length <= 0) {
        output.set_na(output_index);
        return;
    }
    if (source.len <= 0) {
        output.set(output_index, io::as_charport_view(source));
        return;
    }

    const shared::CollationInput subject = subject_cursor.get(
        source_identity, source
    );
    UErrorCode status = U_ZERO_ERROR;
    ranges.clear();
    if (replace_all) {
        matcher.find_all(
            collator, subject, pattern, ranges, status
        );
    }
    else {
        shared::CollationRange first{0, 0};
        if (matcher.find_first(
                collator, subject, pattern, first, status)) {
            ranges.push_back(first);
        }
    }
    require_icu_success(status);

    if (ranges.size() == 0) {
        if (unchanged_utf8_is_lossless(source)) {
            output.set(output_index, io::as_charport_view(source));
        }
        else {
            set_utf16_output(
                subject, output_index, utf8_buffer, output
            );
        }
        return;
    }
    if (replacement.missing) {
        output.set_na(output_index);
        return;
    }

    shared::write_collation_replacement(
        subject, replacement, ranges, replacement_scratch
    );
    const shared::CollationInput replaced{
        &replacement_scratch,
        replacement_scratch.getBuffer(),
        replacement_scratch.length(),
        false
    };
    set_utf16_output(
        replaced, output_index, utf8_buffer, output
    );
}


CHARR_CXX_HELPER void set_utf16_output_parallel(
    const shared::CollationInput& value, unsigned worker,
    R_len_t output_index, std::vector<char>& utf8_buffer,
    io::ParallelOutputBuilder& output
)
{
    UErrorCode status = U_ZERO_ERROR;
    const shared::CollationUtf8Slice utf8 = shared::collation_utf8_slice(
        value, shared::CollationRange{0, value.length}, utf8_buffer, status
    );
    require_icu_success(status);
    output.set(
        worker, output_index, utf8.data,
        static_cast<std::size_t>(utf8.length),
        utf8.ascii ? CETYPE_EXT_ASCII : CETYPE_EXT_UTF8
    );
}


CHARR_CXX_HELPER void replace_one_parallel(
    const shared::StringView& source, const void* source_identity,
    const shared::CollationInput& pattern,
    const shared::CollationInput& replacement,
    UCollator* collator, bool replace_all, R_len_t output_index,
    shared::CollationCursor& subject_cursor,
    shared::CollationMatcher& matcher,
    std::vector<shared::CollationRange>& ranges,
    icu::UnicodeString& replacement_scratch,
    std::vector<char>& utf8_buffer,
    io::ParallelOutputBuilder& output, unsigned worker
)
{
    if (source.is_na() || pattern.missing || pattern.length <= 0) {
        output.set_na(worker, output_index);
        return;
    }
    if (source.len <= 0) {
        output.set(worker, output_index, io::as_charport_view(source));
        return;
    }
    const shared::CollationInput subject = subject_cursor.get(
        source_identity, source
    );
    UErrorCode status = U_ZERO_ERROR;
    ranges.clear();
    if (replace_all) {
        matcher.find_all(collator, subject, pattern, ranges, status);
    }
    else {
        shared::CollationRange first{0, 0};
        if (matcher.find_first(collator, subject, pattern, first, status))
            ranges.push_back(first);
    }
    require_icu_success(status);
    if (ranges.empty()) {
        if (unchanged_utf8_is_lossless(source))
            output.set(worker, output_index, io::as_charport_view(source));
        else
            set_utf16_output_parallel(
                subject, worker, output_index, utf8_buffer, output
            );
        return;
    }
    if (replacement.missing) {
        output.set_na(worker, output_index);
        return;
    }
    shared::write_collation_replacement(
        subject, replacement, ranges, replacement_scratch
    );
    const shared::CollationInput replaced{
        &replacement_scratch, replacement_scratch.getBuffer(),
        replacement_scratch.length(), false
    };
    set_utf16_output_parallel(
        replaced, worker, output_index, utf8_buffer, output
    );
}


class Body final : public ParallelBody {
public:
    CHARR_CXX_HELPER Body(
        const charport::StrViews& subjects,
        const shared::CollationInputs& patterns,
        const shared::CollationInputs& replacements,
        R_len_t output_length, const shared::CollatorOptions& options,
        bool replace_all, bool sequential,
        std::vector<unsigned char>& fallback_slots,
        std::vector<int>& warning_slots,
        io::ParallelOutputBuilder& output
    ) noexcept
        : subjects_(subjects), patterns_(patterns),
          replacements_(replacements), output_length_(output_length),
          options_(options), replace_all_(replace_all),
          sequential_(sequential), fallback_slots_(fallback_slots),
          warning_slots_(warning_slots), output_(output)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        shared::Collator collator;
        const shared::CollatorOpenResult opened = collator.reset(options_);
        fallback_slots_[context.worker] =
            static_cast<unsigned char>(opened.root_fallback);
        require_icu_success(opened.status);
        if (sequential_) {
            run_sequential(context, collator.get());
            return;
        }

        shared::CollationCursor subject_cursor;
        shared::CollationMatcher matcher;
        std::vector<shared::CollationRange> ranges;
        icu::UnicodeString replacement_scratch;
        std::vector<char> utf8_buffer;
        const R_len_t subject_length =
            static_cast<R_len_t>(subjects_.size());
        const R_len_t pattern_length =
            static_cast<R_len_t>(patterns_.size());
        const R_len_t replacement_length =
            static_cast<R_len_t>(replacements_.size());
        if (pattern_length == 1) {
            const shared::CollationInput pattern = patterns_.get(0);
            while (context.next_chunk()) {
                for (R_xlen_t task = context.begin;
                        task < context.end; ++task) {
                    const R_len_t i = static_cast<R_len_t>(task);
                    const R_len_t subject_index = i % subject_length;
                    replace_one_parallel(
                        io::as_shared_view(subjects_[subject_index]),
                        static_cast<const void*>(
                            subjects_.ptrs()+subject_index
                        ),
                        pattern, replacements_.get(static_cast<std::size_t>(
                            i % replacement_length
                        )), collator.get(), replace_all_, i, subject_cursor,
                        matcher, ranges, replacement_scratch, utf8_buffer,
                        output_, context.worker
                    );
                }
            }
            return;
        }

        while (context.next_chunk()) {
            for (R_xlen_t task = context.begin;
                    task < context.end; ++task) {
                const R_len_t lane = static_cast<R_len_t>(task);
                const shared::CollationInput pattern = patterns_.get(
                    static_cast<std::size_t>(lane)
                );
                R_len_t i = lane;
                for (;;) {
                    const R_len_t subject_index = i % subject_length;
                    replace_one_parallel(
                        io::as_shared_view(subjects_[subject_index]),
                        static_cast<const void*>(
                            subjects_.ptrs()+subject_index
                        ),
                        pattern, replacements_.get(static_cast<std::size_t>(
                            i % replacement_length
                        )), collator.get(), replace_all_, i, subject_cursor,
                        matcher, ranges, replacement_scratch, utf8_buffer,
                        output_, context.worker
                    );
                    if (pattern_length >= output_length_-i)
                        break;
                    i += pattern_length;
                }
            }
        }
    }

private:
    CHARR_CXX_HELPER void run_sequential(
        shared::WorkerContext& context, UCollator* collator
    )
    {
        // Per-worker owners. The staged subjects are refilled for each chunk
        // but keep their UTF-16 buffers, and the matcher, the range vector,
        // and both scratch buffers are the same resources the vectorized
        // path above builds once.
        shared::CollationInputs subjects;
        shared::CollationMatcher matcher;
        std::vector<shared::CollationRange> ranges;
        icu::UnicodeString replacement_scratch;
        std::vector<char> utf8_buffer;
        // Every chunk scans the same patterns and so reaches the same count.
        // The worker reports the largest, which is what the caller reduces
        // across workers.
        int worker_warnings = 0;

        while (context.next_chunk()) {
            const std::size_t size = static_cast<std::size_t>(
                context.end-context.begin
            );
            subjects.resize(size);
            for (R_xlen_t i = context.begin; i < context.end; ++i) {
                subjects.set(
                    static_cast<std::size_t>(i-context.begin),
                    io::as_shared_view(subjects_[i])
                );
            }
            bool all_missing = false;
            int warnings = 0;
            for (std::size_t pattern_index = 0;
                    pattern_index < patterns_.size(); ++pattern_index) {
                const shared::CollationInput pattern =
                    patterns_.get(pattern_index);
                if (pattern.missing) {
                    all_missing = true;
                    break;
                }
                if (pattern.length <= 0) {
                    ++warnings;
                    all_missing = true;
                    break;
                }
                const shared::CollationInput replacement =
                    replacements_.get(
                        pattern_index % replacements_.size()
                    );
                for (std::size_t i = 0; i < size; ++i) {
                    const shared::CollationInput subject = subjects.get(i);
                    if (subject.missing || subject.length <= 0)
                        continue;
                    UErrorCode status = U_ZERO_ERROR;
                    matcher.find_all(
                        collator, subject, pattern, ranges, status
                    );
                    require_icu_success(status);
                    if (ranges.empty())
                        continue;
                    if (replacement.missing) {
                        subjects.set_missing(i);
                        continue;
                    }
                    shared::write_collation_replacement(
                        subject, replacement, ranges, replacement_scratch
                    );
                    subjects.swap_value(i, replacement_scratch);
                }
            }
            if (worker_warnings < warnings)
                worker_warnings = warnings;
            warning_slots_[context.worker] = worker_warnings;

            for (R_xlen_t i = context.begin; i < context.end; ++i) {
                const shared::CollationInput subject = subjects.get(
                    static_cast<std::size_t>(i-context.begin)
                );
                if (all_missing || subject.missing)
                    output_.set_na(context.worker, i);
                else
                    set_utf16_output_parallel(
                        subject, context.worker, i, utf8_buffer, output_
                    );
            }
        }
    }

    const charport::StrViews& subjects_;
    const shared::CollationInputs& patterns_;
    const shared::CollationInputs& replacements_;
    R_len_t output_length_;
    shared::CollatorOptions options_;
    bool replace_all_;
    bool sequential_;
    std::vector<unsigned char>& fallback_slots_;
    std::vector<int>& warning_slots_;
    io::ParallelOutputBuilder& output_;
};


CHARR_CXX_HELPER void replace_vectorized(
    const charport::StrViews& subjects,
    const shared::CollationInputs& patterns,
    const shared::CollationInputs& replacements,
    R_len_t output_length,
    UCollator* collator,
    bool replace_all,
    shared::CollationCursor& subject_cursor,
    shared::CollationMatcher& matcher,
    std::vector<shared::CollationRange>& ranges,
    icu::UnicodeString& replacement_scratch,
    std::vector<char>& utf8_buffer,
    io::OutputBuilder& output
)
{
    const R_len_t subject_length =
        static_cast<R_len_t>(subjects.size());
    const R_len_t pattern_length =
        static_cast<R_len_t>(patterns.size());
    const R_len_t replacement_length =
        static_cast<R_len_t>(replacements.size());

    for (R_len_t lane = 0; lane < pattern_length; ++lane) {
        const shared::CollationInput pattern = patterns.get(
            static_cast<std::size_t>(lane)
        );
        R_len_t i = lane;
        for (;;) {
            const R_len_t raw_subject = i % subject_length;
            replace_one(
                io::as_shared_view(subjects[raw_subject]),
                static_cast<const void*>(subjects.ptrs() + raw_subject),
                pattern,
                replacements.get(
                    static_cast<std::size_t>(i % replacement_length)
                ),
                collator, replace_all, i, subject_cursor, matcher,
                ranges, replacement_scratch, utf8_buffer, output
            );

            if (pattern_length >= output_length-i)
                break;
            i += pattern_length;
        }
    }
}


CHARR_CXX_HELPER void replace_sequential(
    shared::CollationInputs& subjects,
    const shared::CollationInputs& patterns,
    const shared::CollationInputs& replacements,
    UCollator* collator,
    shared::CollationMatcher& matcher,
    std::vector<shared::CollationRange>& ranges,
    icu::UnicodeString& replacement_scratch,
    std::vector<char>& utf8_buffer,
    io::OutputBuilder& output,
    int& additional_empty_warnings
)
{
    bool all_missing = false;
    for (std::size_t pattern_index = 0;
            pattern_index < patterns.size(); ++pattern_index) {
        const shared::CollationInput pattern = patterns.get(pattern_index);
        if (pattern.missing) {
            all_missing = true;
            break;
        }
        if (pattern.length <= 0) {
            ++additional_empty_warnings;
            all_missing = true;
            break;
        }

        const shared::CollationInput replacement = replacements.get(
            pattern_index % replacements.size()
        );
        for (std::size_t subject_index = 0;
                subject_index < subjects.size(); ++subject_index) {
            const shared::CollationInput subject = subjects.get(
                subject_index
            );
            if (subject.missing || subject.length <= 0)
                continue;

            UErrorCode status = U_ZERO_ERROR;
            matcher.find_all(
                collator, subject, pattern, ranges, status
            );
            require_icu_success(status);
            if (ranges.size() == 0)
                continue;
            if (replacement.missing) {
                subjects.set_missing(subject_index);
                continue;
            }

            shared::write_collation_replacement(
                subject, replacement, ranges, replacement_scratch
            );
            subjects.swap_value(subject_index, replacement_scratch);
        }
    }

    for (std::size_t i = 0; i < subjects.size(); ++i) {
        const shared::CollationInput subject = subjects.get(i);
        if (all_missing || subject.missing) {
            output.set_na(static_cast<R_len_t>(i));
        }
        else {
            set_utf16_output(
                subject, static_cast<R_len_t>(i),
                utf8_buffer, output
            );
        }
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


CHARR_R_HELPER void emit_warnings(
    bool root_fallback_warning,
    bool recycling_warning,
    int empty_pattern_warnings
) noexcept
{
    if (root_fallback_warning) {
        Rf_warning(
            "%s", ICUError::getICUerrorName(U_USING_DEFAULT_WARNING)
        );
    }
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);
    for (int i = 0; i < empty_pattern_warnings; ++i)
        Rf_warning(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
}

} // namespace search_coll_replace

using namespace search_coll_replace;


/**
 * Replace first occurrence of a fixed pattern [with collation]
 *
 * @param str character vector
 * @param pattern character vector
 * @param replacement character vector
 * @param opts_collator list
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-26)
 *          use ci__replace_allfirstlast_fixed
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          new fun: ci_replace_first_coll (opts_collator == NA not allowed)
 */
CHARR_ENTRYPOINT SEXP ci_replace_first_coll(
    SEXP str,
    SEXP pattern,
    SEXP replacement,
    SEXP opts_collator
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
    const shared::CollatorOptions options =
        collator::prepare_options(opts_collator);

    const R_len_t subject_length = LENGTH(str);
    const R_len_t pattern_length = LENGTH(pattern);
    const R_len_t replacement_length = LENGTH(replacement);
    bool recycling_needed = false;
    const R_len_t output_length = recycling_length(
        subject_length, pattern_length, replacement_length,
        recycling_needed
    );
    const R_len_t tasks = output_length == 0
        ? 0 : pattern_length == 1
            ? output_length : pattern_length;
    const shared::ParallelPlan plan = shared::parallel_plan(true, tasks);


    bool root_fallback_warning = false;
    bool recycling_warning = false;
    int empty_pattern_warnings = 0;

    try {
        shared::Collator collator_owner;
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
        shared::CollationInputs patterns;
        shared::CollationInputs replacements;
        shared::CollationCursor subject_cursor;
        shared::CollationMatcher matcher;
        std::vector<shared::CollationRange> ranges;
        icu::UnicodeString replacement_scratch;
        std::vector<char> utf8_buffer;
        io::OutputBuilder output(0);
        io::ParallelOutputBuilder parallel_output;
        std::vector<unsigned char> fallback_slots;
        std::vector<int> warning_slots;

        ranges.reserve(4);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const shared::CollatorOpenResult opened =
                    collator_owner.reset(options);
                root_fallback_warning = opened.root_fallback;
                require_icu_success(opened.status);
                recycling_warning = recycling_needed;

                if (plan.workers == 1)
                    output.reset(output_length);
                if (output_length > 0) {
                    subject_reader.reset(str);
                    if (subject_reader.size() != subject_length) {
                        throw std::runtime_error(
                            "Reader length changed during collation replacement"
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
                            "Reader length changed during collation replacement"
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
                    empty_pattern_warnings = count_empty_patterns(patterns);

                    replacement_reader.reset(replacement);
                    if (replacement_reader.size() != replacement_length) {
                        throw std::runtime_error(
                            "Reader length changed during collation replacement"
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
                        replacement_storage
                    );
                    stage_utf16(replacement_views, replacements);

                    if (plan.workers > 1) {
                        fallback_slots.assign(plan.workers, 0);
                        warning_slots.assign(plan.workers, 0);
                        parallel_output.reset(
                            output_length, plan.workers
                        );
                        Body body(
                            subject_views, patterns, replacements,
                            output_length, options, false, false,
                            fallback_slots, warning_slots, parallel_output
                        );
                        shared::run_parallel(plan, tasks, body);
                        for (unsigned worker = 0;
                                worker < plan.workers; ++worker) {
                            root_fallback_warning = root_fallback_warning ||
                                fallback_slots[worker] != 0;
                        }
                        result = entry_protections.reprotect_one(
                            parallel_output.to_sexp(), result_index
                        );
                    }
                    else {
                        replace_vectorized(
                            subject_views, patterns, replacements,
                            output_length, collator_owner.get(), false,
                            subject_cursor, matcher, ranges,
                            replacement_scratch, utf8_buffer, output
                        );
                    }
                }

                if (plan.workers == 1) {
                    result = entry_protections.reprotect_one(
                        output.to_sexp(), result_index
                    );
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


/**
 * Replace all occurrences of a fixed pattern [with collation]
 *
 * @param str character vector
 * @param pattern character vector
 * @param replacement character vector
 * @param opts_collator list
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-26)
 *          use ci__replace_allfirstlast_fixed
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          new fun: ci_replace_all_coll (opts_collator == NA not allowed)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *          vectorize_all arg added
 */
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

    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    R_len_t subject_length = 0;
    bool empty_sequential = false;
    if (!vectorize) {
        subject_length = LENGTH(str);
        empty_sequential = subject_length <= 0;
    }

    SEXP first_prepared;
    first_prepared = entry_protections.protect_one(
        empty_sequential
                ? R_NilValue
                : ci__prepare_arg_string_r(
                    vectorize ? replacement : pattern,
                    vectorize ? "replacement" : "pattern"
                )
    );
    SEXP second_prepared;
    second_prepared = entry_protections.protect_one(
        empty_sequential
                ? R_NilValue
                : ci__prepare_arg_string_r(
                    vectorize ? pattern : replacement,
                    vectorize ? "pattern" : "replacement"
                )
    );
    if (vectorize) {
        replacement = first_prepared;
        pattern = second_prepared;
    }
    else {
        pattern = first_prepared;
        replacement = second_prepared;
    }

    shared::CollatorOptions options{};
    R_len_t pattern_length = 0;
    R_len_t replacement_length = 0;
    bool recycling_needed = false;
    R_len_t output_length = subject_length;

    if (vectorize) {
        options = collator::prepare_options(opts_collator);
        subject_length = LENGTH(str);
        pattern_length = LENGTH(pattern);
        replacement_length = LENGTH(replacement);
        output_length = recycling_length(
            subject_length, pattern_length, replacement_length,
            recycling_needed
        );
    }
    else if (!empty_sequential) {
        pattern_length = LENGTH(pattern);
        replacement_length = LENGTH(replacement);
        require_sequential_lengths(pattern_length, replacement_length);
        if (pattern_length % replacement_length != 0)
            Rf_warning(MSG__WARN_RECYCLING_RULE);
        options = collator::prepare_options(opts_collator);
    }
    const bool vectorized_core = vectorize || pattern_length == 1;
    const bool sequential = !vectorized_core;
    const R_len_t tasks = output_length == 0
        ? 0 : sequential || pattern_length == 1
            ? output_length : pattern_length;
    const shared::ParallelPlan plan = shared::parallel_plan(true, tasks);


    bool root_fallback_warning = false;
    bool recycling_warning = false;
    int empty_pattern_warnings = 0;

    try {
        shared::Collator collator_owner;
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
        shared::CollationInputs subjects;
        shared::CollationInputs patterns;
        shared::CollationInputs replacements;
        shared::CollationCursor subject_cursor;
        shared::CollationMatcher matcher;
        std::vector<shared::CollationRange> ranges;
        icu::UnicodeString replacement_scratch;
        std::vector<char> utf8_buffer;
        io::OutputBuilder output(0);
        io::ParallelOutputBuilder parallel_output;
        std::vector<unsigned char> fallback_slots;
        std::vector<int> warning_slots;

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
                if (plan.workers == 1)
                    output.reset(output_length);

                if (!empty_sequential && output_length > 0) {
                    subject_reader.reset(str);
                    if (subject_reader.size() != subject_length) {
                        throw std::runtime_error(
                            "Reader length changed during collation replacement"
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
                    if (!vectorized_core && plan.workers == 1)
                        stage_utf16(subject_views, subjects);

                    pattern_reader.reset(pattern);
                    if (pattern_reader.size() != pattern_length) {
                        throw std::runtime_error(
                            "Reader length changed during collation replacement"
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
                    empty_pattern_warnings = count_empty_patterns(patterns);

                    replacement_reader.reset(replacement);
                    if (replacement_reader.size() != replacement_length) {
                        throw std::runtime_error(
                            "Reader length changed during collation replacement"
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
                        replacement_storage
                    );
                    stage_utf16(replacement_views, replacements);

                    if (plan.workers > 1) {
                        fallback_slots.assign(plan.workers, 0);
                        warning_slots.assign(plan.workers, 0);
                        parallel_output.reset(
                            output_length, plan.workers
                        );
                        Body body(
                            subject_views, patterns, replacements,
                            output_length, options, true, sequential,
                            fallback_slots, warning_slots, parallel_output
                        );
                        shared::run_parallel(plan, tasks, body);
                        int additional_empty_warnings = 0;
                        for (unsigned worker = 0;
                                worker < plan.workers; ++worker) {
                            root_fallback_warning = root_fallback_warning ||
                                fallback_slots[worker] != 0;
                            if (warning_slots[worker] >
                                    additional_empty_warnings) {
                                additional_empty_warnings =
                                    warning_slots[worker];
                            }
                        }
                        empty_pattern_warnings +=
                            additional_empty_warnings;
                        result = entry_protections.reprotect_one(
                            parallel_output.to_sexp(), result_index
                        );
                    }

                    if (plan.workers == 1 && vectorized_core) {
                        replace_vectorized(
                            subject_views, patterns, replacements,
                            output_length, collator_owner.get(), true,
                            subject_cursor, matcher, ranges,
                            replacement_scratch, utf8_buffer, output
                        );
                    }
                    else if (plan.workers == 1) {
                        int additional_empty_warnings = 0;
                        replace_sequential(
                            subjects, patterns, replacements,
                            collator_owner.get(), matcher, ranges,
                            replacement_scratch, utf8_buffer, output,
                            additional_empty_warnings
                        );
                        empty_pattern_warnings +=
                            additional_empty_warnings;
                    }
                }

                if (plan.workers == 1) {
                    result = entry_protections.reprotect_one(
                        output.to_sexp(), result_index
                    );
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

} } // namespace charr::altrep_backend
