
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
#include "fixed/options.h"
#include "io/string_view.h"
#include "io/utf8_output.h"
#include "../shared/entrypoint.h"
#include "../shared/fixed_search.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/replacement.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <charport.h>

#include <cstddef>
#include <cstring>
#include <exception>
#include <limits>
#include <stdexcept>
#include <vector>

namespace charr { namespace altrep_backend {

namespace search_fixed_replace {

struct DirectView {
    const char* data;
    R_len_t length;
    bool missing;
    cetype_ext_t encoding;
};


CHARR_NEUTRAL_HELPER bool direct_view(
    const charport::StrView& value,
    DirectView& output
) noexcept
{
    if (value.is_na()) {
        output = DirectView{
            nullptr, NA_INTEGER, true, CETYPE_EXT_NA
        };
        return true;
    }
    if (value.len < 0 || (value.ptr == nullptr && value.len != 0) ||
            (value.enc != CETYPE_EXT_ASCII &&
             value.enc != CETYPE_EXT_UTF8 &&
             value.enc != CETYPE_EXT_ASCII_OR_UTF8)) {
        return false;
    }

    output = DirectView{value.ptr, value.len, false, value.enc};
    if (value.enc != CETYPE_EXT_ASCII &&
            STRI__ENC_HAS_BOM_UTF8(output.data, output.length)) {
        output.data += 3;
        output.length -= 3;
    }
    return true;
}


CHARR_NEUTRAL_HELPER bool span_is_ascii(
    const char* data,
    R_len_t length
) noexcept
{
    for (R_len_t i = 0; i < length; ++i) {
        if (static_cast<unsigned char>(data[i]) > 0x7fU)
            return false;
    }
    return true;
}


CHARR_CXX_HELPER std::size_t checked_direct_size(
    R_len_t subject_length,
    R_len_t replacement_length,
    std::size_t count,
    std::size_t matched_bytes
)
{
    const std::size_t subject_size =
        static_cast<std::size_t>(subject_length);
    if (matched_bytes > subject_size)
        throw std::length_error(MSG__CHARSXP_2147483647);

    const std::size_t unmatched = subject_size-matched_bytes;
    const std::size_t replacement_size =
        static_cast<std::size_t>(replacement_length);
    const std::size_t maximum = static_cast<std::size_t>(R_LEN_T_MAX);
    if (replacement_size > 0 && count >
            (maximum-unmatched)/replacement_size) {
        throw std::length_error(MSG__CHARSXP_2147483647);
    }
    return unmatched+count*replacement_size;
}


CHARR_NEUTRAL_HELPER R_len_t find_first_byte(
    const DirectView& subject,
    unsigned char pattern
) noexcept
{
    const void* found = std::memchr(
        subject.data, pattern,
        static_cast<std::size_t>(subject.length)
    );
    return found == nullptr
        ? -1
        : static_cast<R_len_t>(
            static_cast<const char*>(found)-subject.data
        );
}


CHARR_NEUTRAL_HELPER std::size_t scan_all_bytes(
    const DirectView& subject,
    unsigned char pattern,
    bool& output_ascii
) noexcept
{
    std::size_t count = 0;
    for (R_len_t i = 0; i < subject.length; ++i) {
        const unsigned char value =
            static_cast<unsigned char>(subject.data[i]);
        if (value == pattern)
            ++count;
        else if (value > 0x7fU)
            output_ascii = false;
    }
    return count;
}


CHARR_NEUTRAL_HELPER void write_first_byte(
    char* output,
    const DirectView& subject,
    R_len_t match,
    const DirectView& replacement
) noexcept
{
    if (match > 0) {
        std::memcpy(
            output, subject.data, static_cast<std::size_t>(match)
        );
    }
    if (replacement.length > 0) {
        std::memcpy(
            output+match, replacement.data,
            static_cast<std::size_t>(replacement.length)
        );
    }
    const R_len_t suffix = subject.length-match-1;
    if (suffix > 0) {
        std::memcpy(
            output+match+replacement.length, subject.data+match+1,
            static_cast<std::size_t>(suffix)
        );
    }
}


CHARR_NEUTRAL_HELPER void write_all_bytes(
    char* output,
    const DirectView& subject,
    unsigned char pattern,
    const DirectView& replacement
) noexcept
{
    if (replacement.length == 1) {
        if (subject.length > 0) {
            std::memcpy(
                output, subject.data,
                static_cast<std::size_t>(subject.length)
            );
        }
        for (R_len_t i = 0; i < subject.length; ++i) {
            if (static_cast<unsigned char>(output[i]) == pattern)
                output[i] = replacement.data[0];
        }
        return;
    }

    std::size_t used = 0;
    R_len_t previous = 0;
    for (R_len_t i = 0; i < subject.length; ++i) {
        if (static_cast<unsigned char>(subject.data[i]) != pattern)
            continue;

        const std::size_t prefix =
            static_cast<std::size_t>(i-previous);
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
    }

    const std::size_t suffix =
        static_cast<std::size_t>(subject.length-previous);
    if (suffix > 0)
        std::memcpy(output+used, subject.data+previous, suffix);
}


/*
 * Everything one direct attempt resolves before it reads a subject: the
 * single pattern byte and the replacement spliced in for it. Holding it in
 * one value keeps the pattern, the replacement, and the options out of the
 * element kernel, so the serial driver and the parallel body can share that
 * kernel instead of transcribing it twice.
 */
struct ReplaceDirectState {
    DirectView replacement;
    unsigned char pattern_byte;
    bool replacement_ascii;
    bool replace_all;
};


// Everything the direct path can decide before looking at a subject.
CHARR_NEUTRAL_HELPER bool replace_direct_eligible(
    const charport::StrViews& subjects,
    const charport::StrView& pattern,
    const charport::StrView& replacement,
    R_len_t output_length,
    shared::FixedSearchOptions options,
    bool replace_all,
    ReplaceDirectState& state
) noexcept
{
    if (options.case_insensitive || options.overlap ||
            subjects.size() != output_length) {
        return false;
    }

    DirectView pattern_value;
    DirectView replacement_value;
    if (!direct_view(pattern, pattern_value) || pattern_value.missing ||
            pattern_value.length != 1 ||
            !direct_view(replacement, replacement_value)) {
        return false;
    }

    state.replacement = replacement_value;
    state.pattern_byte =
        static_cast<unsigned char>(pattern_value.data[0]);
    state.replacement_ascii = !replacement_value.missing &&
        span_is_ascii(
            replacement_value.data, replacement_value.length
        );
    state.replace_all = replace_all;
    return true;
}


/*
 * One output element of the direct path, written through a sink that reaches
 * either builder, so that the serial and the threaded run share one kernel.
 * Returns false when this subject is not representable by the byte path,
 * having written nothing. The element depends on its own subject alone, and
 * the body touches no R API and no R allocation: charport's builders take
 * their payload from ::operator new and their record arrays from new[]. A
 * worker may run it.
 */
CHARR_CXX_HELPER bool replace_direct_one(
    const ReplaceDirectState& state,
    const charport::StrView& value,
    R_len_t i,
    io::OutputSink& sink
)
{
    DirectView subject;
    if (!direct_view(value, subject))
        return false;
    if (subject.missing) {
        sink.set_na(i);
        return true;
    }
    if (subject.length <= 0) {
        sink.set(
            i, subject.data, 0,
            subject.encoding
        );
        return true;
    }

    if (!state.replace_all) {
        const R_len_t match = find_first_byte(subject, state.pattern_byte);
        if (match < 0) {
            sink.set(
                i, subject.data,
                static_cast<std::size_t>(subject.length),
                subject.encoding
            );
            return true;
        }
        if (state.replacement.missing) {
            sink.set_na(i);
            return true;
        }

        const bool ascii = state.replacement_ascii &&
            span_is_ascii(subject.data, match) &&
            span_is_ascii(
                subject.data+match+1,
                subject.length-match-1
            );
        const std::size_t size = checked_direct_size(
            subject.length, state.replacement.length, 1, 1
        );
        char* destination = sink.reserve(
            i, size,
            ascii ? CETYPE_EXT_ASCII : CETYPE_EXT_UTF8
        );
        write_first_byte(
            destination, subject, match, state.replacement
        );
        return true;
    }

    bool ascii = state.replacement_ascii;
    const std::size_t count = scan_all_bytes(
        subject, state.pattern_byte, ascii
    );
    if (count == 0) {
        sink.set(
            i, subject.data,
            static_cast<std::size_t>(subject.length),
            subject.encoding
        );
        return true;
    }
    if (state.replacement.missing) {
        sink.set_na(i);
        return true;
    }

    const std::size_t size = checked_direct_size(
        subject.length, state.replacement.length, count, count
    );
    char* destination = sink.reserve(
        i, size,
        ascii ? CETYPE_EXT_ASCII : CETYPE_EXT_UTF8
    );
    write_all_bytes(
        destination, subject, state.pattern_byte, state.replacement
    );
    return true;
}


CHARR_CXX_HELPER bool replace_scalar_byte_direct(
    const charport::StrViews& subjects,
    const charport::StrView& pattern,
    const charport::StrView& replacement,
    R_len_t output_length,
    shared::FixedSearchOptions options,
    bool replace_all,
    io::OutputBuilder& output,
    R_len_t& general_start
)
{
    if (output_length <= 0)
        return true;

    ReplaceDirectState state;
    if (!replace_direct_eligible(
            subjects, pattern, replacement, output_length, options,
            replace_all, state)) {
        return false;
    }

    io::OutputSink sink(output);
    for (R_len_t i = 0; i < output_length; ++i) {
        if (!replace_direct_one(state, subjects[i], i, sink)) {
            general_start = i;
            return false;
        }
    }
    return true;
}


/*
 * The lowest element index any worker refused, or `none` when the direct
 * attempt covered every element. Chunks are contiguous and ordered, so the
 * lowest refusal is the one a serial scan would have reached first: below it
 * every element was written by the direct kernel, and at or above it every
 * element is either unwritten or rewritten. The minimum therefore reports
 * exactly what a serial direct scan would have stored in general_start.
 *
 * The threaded fallback does not resume from that index. The general Body
 * always rebuilds [0, output_length), so the caller resets the sharded
 * builder rather than keeping the direct prefix.
 */
CHARR_NEUTRAL_HELPER R_len_t lowest_ineligible(
    const std::vector<R_len_t>& slots, R_len_t none
) noexcept
{
    R_len_t result = none;
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (slots[i] < result)
            result = slots[i];
    }
    return result;
}


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


CHARR_CXX_HELPER void normalize_views(
    const charport::StrViews& source,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    std::vector<shared::StringView>& output
)
{
    output.resize(static_cast<std::size_t>(source.size()));
    for (R_xlen_t i = 0; i < source.size(); ++i) {
        output[static_cast<std::size_t>(i)] = shared::normalize_utf8(
            io::as_shared_view(source[i]), converter, storage
        );
    }
}


CHARR_NEUTRAL_HELPER int count_empty_patterns(
    const std::vector<shared::StringView>& patterns
) noexcept
{
    int count = 0;
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        if (!patterns[i].is_na() && patterns[i].len <= 0)
            ++count;
    }
    return count;
}


CHARR_CXX_HELPER std::size_t matched_byte_count(
    const std::vector<shared::FixedRange>& ranges
)
{
    std::size_t result = 0;
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        const std::size_t width = static_cast<std::size_t>(
            ranges[i].end-ranges[i].start
        );
        if (width > std::numeric_limits<std::size_t>::max()-result)
            throw std::length_error("fixed matched byte count overflow");
        result += width;
    }
    return result;
}


CHARR_CXX_HELPER void replace_one(
    const shared::StringView& subject,
    const shared::StringView& pattern,
    const shared::StringView& replacement,
    shared::FixedSearchOptions options,
    bool replace_all,
    shared::FixedMatcher& matcher,
    std::vector<shared::FixedRange>& ranges,
    io::OutputBuilder& output,
    R_len_t output_index
)
{
    if (subject.is_na() || pattern.is_na() || pattern.len <= 0) {
        output.set_na(output_index);
        return;
    }
    if (subject.len <= 0) {
        output.set(output_index, io::as_charport_view(subject));
        return;
    }

    ranges.clear();
    if (replace_all) {
        matcher.find_all(subject, pattern, options, ranges);
    }
    else {
        shared::FixedRange range;
        if (matcher.find_first(subject, pattern, options, range))
            ranges.push_back(range);
    }

    if (ranges.size() == 0) {
        output.set(output_index, io::as_charport_view(subject));
        return;
    }
    if (replacement.is_na()) {
        output.set_na(output_index);
        return;
    }

    const std::size_t matched_bytes = matched_byte_count(ranges);
    const std::size_t size = shared::checked_replacement_size(
        subject, matched_bytes, ranges, replacement
    );
    const bool ascii = shared::replacement_is_ascii(
        subject, ranges, replacement
    );
    char* destination = output.reserve(
        output_index, size,
        ascii ? CETYPE_EXT_ASCII : CETYPE_EXT_UTF8
    );
    shared::write_replacement(
        subject, ranges, replacement, destination, size
    );
}


CHARR_CXX_HELPER void replace_one_parallel(
    const shared::StringView& subject,
    const shared::StringView& pattern,
    const shared::StringView& replacement,
    shared::FixedSearchOptions options, bool replace_all,
    shared::FixedMatcher& matcher,
    std::vector<shared::FixedRange>& ranges,
    io::ParallelOutputBuilder& output, unsigned worker,
    R_len_t output_index
)
{
    if (subject.is_na() || pattern.is_na() || pattern.len <= 0) {
        output.set_na(worker, output_index);
        return;
    }
    if (subject.len <= 0) {
        output.set(worker, output_index, io::as_charport_view(subject));
        return;
    }
    ranges.clear();
    if (replace_all) {
        matcher.find_all(subject, pattern, options, ranges);
    }
    else {
        shared::FixedRange range;
        if (matcher.find_first(subject, pattern, options, range))
            ranges.push_back(range);
    }
    if (ranges.empty()) {
        output.set(worker, output_index, io::as_charport_view(subject));
        return;
    }
    if (replacement.is_na()) {
        output.set_na(worker, output_index);
        return;
    }
    const std::size_t matched_bytes = matched_byte_count(ranges);
    const std::size_t size = shared::checked_replacement_size(
        subject, matched_bytes, ranges, replacement
    );
    const bool ascii = shared::replacement_is_ascii(
        subject, ranges, replacement
    );
    char* destination = output.reserve(
        worker, output_index, size,
        ascii ? CETYPE_EXT_ASCII : CETYPE_EXT_UTF8
    );
    shared::write_replacement(
        subject, ranges, replacement, destination, size
    );
}


/*
 * The direct byte path on worker threads. A task is one output element; the
 * direct path only runs with a scalar pattern and a scalar replacement, so
 * the element range and the recycling lanes Body splits are the same range.
 * The plan carries only the worker count, so the task basis belongs to the
 * run_parallel call.
 */
class DirectBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER DirectBody(
        const charport::StrViews& subjects,
        const ReplaceDirectState& state,
        std::vector<R_len_t>& first_ineligible,
        io::ParallelOutputBuilder& output
    ) noexcept
        : subjects_(subjects), state_(state),
          first_ineligible_(first_ineligible), output_(output)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        io::OutputSink sink(output_, context.worker);
        while (context.next_chunk()) {
            for (R_xlen_t task = context.begin; task < context.end; ++task) {
                const R_len_t i = static_cast<R_len_t>(task);
                if (!replace_direct_one(state_, subjects_[i], i, sink)) {
                    // Stop here and claim no further chunk, leaving the
                    // rest unwritten. The caller discards every shard
                    // before the general path runs.
                    first_ineligible_[context.worker] = i;
                    context.stop_early();
                    return;
                }
            }
        }
    }

private:
    const charport::StrViews& subjects_;
    ReplaceDirectState state_;
    std::vector<R_len_t>& first_ineligible_;
    io::ParallelOutputBuilder& output_;
};


class Body final : public ParallelBody {
public:
    CHARR_CXX_HELPER Body(
        const std::vector<shared::StringView>& subjects,
        const std::vector<shared::StringView>& patterns,
        const std::vector<shared::StringView>& replacements,
        R_len_t output_length, shared::FixedSearchOptions options,
        bool replace_all, bool sequential,
        std::vector<int>& warning_slots,
        io::ParallelOutputBuilder& output
    ) noexcept
        : subjects_(subjects), patterns_(patterns),
          replacements_(replacements), output_length_(output_length),
          options_(options), replace_all_(replace_all),
          sequential_(sequential), warning_slots_(warning_slots),
          output_(output)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        if (sequential_) {
            // The matcher caches the pattern it prepared and the scratch
            // vectors keep their capacity, so a worker builds them once and
            // every chunk it draws reuses them.
            shared::FixedMatcher sequential_matcher;
            std::vector<shared::FixedRange> sequential_ranges;
            std::vector<shared::StringView> working;
            while (context.next_chunk()) {
                run_sequential(
                    context, sequential_matcher, sequential_ranges, working
                );
            }
            return;
        }

        shared::FixedMatcher matcher;
        std::vector<shared::FixedRange> ranges;
        const R_len_t subject_length =
            static_cast<R_len_t>(subjects_.size());
        const R_len_t pattern_length =
            static_cast<R_len_t>(patterns_.size());
        const R_len_t replacement_length =
            static_cast<R_len_t>(replacements_.size());
        if (pattern_length == 1) {
            while (context.next_chunk()) {
                for (R_xlen_t task = context.begin;
                        task < context.end; ++task) {
                    const R_len_t i = static_cast<R_len_t>(task);
                    replace_one_parallel(
                        subjects_[static_cast<std::size_t>(
                            i % subject_length
                        )],
                        patterns_[0], replacements_[static_cast<std::size_t>(
                            i % replacement_length
                        )], options_, replace_all_, matcher, ranges,
                        output_, context.worker, i
                    );
                }
            }
            return;
        }

        while (context.next_chunk()) {
            for (R_xlen_t task = context.begin;
                    task < context.end; ++task) {
                const R_len_t lane = static_cast<R_len_t>(task);
                const shared::StringView& pattern = patterns_[
                    static_cast<std::size_t>(lane)
                ];
                R_len_t i = lane;
                for (;;) {
                    replace_one_parallel(
                        subjects_[static_cast<std::size_t>(
                            i % subject_length
                        )],
                        pattern, replacements_[static_cast<std::size_t>(
                            i % replacement_length
                        )], options_, replace_all_, matcher, ranges,
                        output_, context.worker, i
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
        const shared::WorkerContext& context,
        shared::FixedMatcher& matcher,
        std::vector<shared::FixedRange>& ranges,
        std::vector<shared::StringView>& working
    )
    {
        const std::size_t size = static_cast<std::size_t>(
            context.end-context.begin
        );
        working.resize(size);
        for (R_xlen_t i = context.begin; i < context.end; ++i) {
            working[static_cast<std::size_t>(i-context.begin)] =
                subjects_[static_cast<std::size_t>(i)];
        }
        // The arena stays per chunk: every value it holds is copied into the
        // output before this returns, so a worker never carries more than one
        // chunk of intermediate results.
        shared::SliceArena storage;
        bool all_missing = false;
        int warnings = 0;
        for (std::size_t pattern_index = 0;
                pattern_index < patterns_.size(); ++pattern_index) {
            const shared::StringView& pattern = patterns_[pattern_index];
            if (pattern.is_na()) {
                all_missing = true;
                break;
            }
            if (pattern.len <= 0) {
                ++warnings;
                all_missing = true;
                break;
            }
            const shared::StringView& replacement = replacements_[
                pattern_index % replacements_.size()
            ];
            for (std::size_t i = 0; i < working.size(); ++i) {
                const shared::StringView subject = working[i];
                if (subject.is_na() || subject.len <= 0)
                    continue;
                matcher.find_all(subject, pattern, options_, ranges);
                if (ranges.empty())
                    continue;
                if (replacement.is_na()) {
                    working[i] = shared::StringView{
                        nullptr, shared::missing_string_length,
                        shared::StringEncoding::missing
                    };
                    continue;
                }
                const std::size_t matched_bytes = matched_byte_count(ranges);
                const std::size_t output_size =
                    shared::checked_replacement_size(
                        subject, matched_bytes, ranges, replacement
                    );
                const bool ascii = shared::replacement_is_ascii(
                    subject, ranges, replacement
                );
                char* destination = output_size > 0
                    ? storage.allocate(output_size) : nullptr;
                shared::write_replacement(
                    subject, ranges, replacement,
                    destination, output_size
                );
                working[i] = shared::StringView{
                    output_size > 0 ? destination : replacement.ptr,
                    static_cast<R_len_t>(output_size),
                    ascii ? shared::StringEncoding::ascii
                          : shared::StringEncoding::utf8
                };
            }
        }
        warning_slots_[context.worker] = warnings;
        for (R_xlen_t i = context.begin; i < context.end; ++i) {
            const shared::StringView& value = working[
                static_cast<std::size_t>(i-context.begin)
            ];
            if (all_missing || value.is_na())
                output_.set_na(context.worker, i);
            else
                output_.set(
                    context.worker, i, io::as_charport_view(value)
                );
        }
    }

    const std::vector<shared::StringView>& subjects_;
    const std::vector<shared::StringView>& patterns_;
    const std::vector<shared::StringView>& replacements_;
    R_len_t output_length_;
    shared::FixedSearchOptions options_;
    bool replace_all_;
    bool sequential_;
    std::vector<int>& warning_slots_;
    io::ParallelOutputBuilder& output_;
};


CHARR_CXX_HELPER void replace_vectorized(
    const std::vector<shared::StringView>& subjects,
    const std::vector<shared::StringView>& patterns,
    const std::vector<shared::StringView>& replacements,
    R_len_t output_length,
    R_len_t general_start,
    shared::FixedSearchOptions options,
    bool replace_all,
    shared::FixedMatcher& matcher,
    std::vector<shared::FixedRange>& ranges,
    io::OutputBuilder& output
)
{
    const R_len_t subject_length =
        static_cast<R_len_t>(subjects.size());
    const R_len_t pattern_length =
        static_cast<R_len_t>(patterns.size());
    const R_len_t replacement_length =
        static_cast<R_len_t>(replacements.size());

    if (general_start > 0) {
        if (pattern_length != 1 || replacement_length != 1)
            throw std::logic_error("fixed replacement direct prefix mismatch");
        for (R_len_t i = general_start; i < output_length; ++i) {
            replace_one(
                subjects[static_cast<std::size_t>(i)], patterns[0],
                replacements[0], options, replace_all, matcher, ranges,
                output, i
            );
        }
        return;
    }

    for (R_len_t lane = 0; lane < pattern_length; ++lane) {
        R_len_t i = lane;
        while (i < output_length) {
            replace_one(
                subjects[static_cast<std::size_t>(i % subject_length)],
                patterns[static_cast<std::size_t>(lane)],
                replacements[
                    static_cast<std::size_t>(i % replacement_length)
                ],
                options, replace_all, matcher, ranges, output, i
            );
            if (output_length-i <= pattern_length)
                break;
            i += pattern_length;
        }
    }
}


CHARR_CXX_HELPER void replace_sequential(
    const std::vector<shared::StringView>& subjects,
    const std::vector<shared::StringView>& patterns,
    const std::vector<shared::StringView>& replacements,
    shared::FixedSearchOptions options,
    std::vector<shared::StringView>& working,
    shared::FixedMatcher& matcher,
    std::vector<shared::FixedRange>& ranges,
    shared::SliceArena& replacement_storage,
    io::OutputBuilder& output,
    int& additional_empty_warnings
)
{
    working.resize(subjects.size());
    for (std::size_t i = 0; i < subjects.size(); ++i)
        working[i] = subjects[i];

    bool all_missing = false;
    for (std::size_t pattern_index = 0;
            pattern_index < patterns.size(); ++pattern_index) {
        const shared::StringView& pattern = patterns[pattern_index];
        if (pattern.is_na()) {
            all_missing = true;
            break;
        }
        if (pattern.len <= 0) {
            ++additional_empty_warnings;
            all_missing = true;
            break;
        }

        const shared::StringView& replacement = replacements[
            pattern_index % replacements.size()
        ];
        for (std::size_t subject_index = 0;
                subject_index < working.size(); ++subject_index) {
            const shared::StringView subject = working[subject_index];
            if (subject.is_na() || subject.len <= 0)
                continue;

            matcher.find_all(subject, pattern, options, ranges);
            if (ranges.size() == 0)
                continue;
            if (replacement.is_na()) {
                working[subject_index] = shared::StringView{
                    nullptr, shared::missing_string_length,
                    shared::StringEncoding::missing
                };
                continue;
            }

            const std::size_t matched_bytes = matched_byte_count(ranges);
            const std::size_t size = shared::checked_replacement_size(
                subject, matched_bytes, ranges, replacement
            );
            const bool ascii = shared::replacement_is_ascii(
                subject, ranges, replacement
            );
            char* destination = size > 0
                ? replacement_storage.allocate(size)
                : nullptr;
            shared::write_replacement(
                subject, ranges, replacement, destination, size
            );
            working[subject_index] = shared::StringView{
                size > 0 ? destination : replacement.ptr,
                static_cast<R_len_t>(size),
                ascii
                    ? shared::StringEncoding::ascii
                    : shared::StringEncoding::utf8
            };
        }
    }

    output.reset(static_cast<R_len_t>(subjects.size()));
    for (std::size_t i = 0; i < working.size(); ++i) {
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


CHARR_R_HELPER void emit_warnings(
    bool recycling_warning,
    int empty_pattern_warnings
) noexcept
{
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);
    for (int i = 0; i < empty_pattern_warnings; ++i)
        Rf_warning(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
}

} // namespace search_fixed_replace

using namespace search_fixed_replace;


/** Replace the first occurrence of a fixed pattern. */
CHARR_ENTRYPOINT SEXP ci_replace_first_fixed(
    SEXP str,
    SEXP pattern,
    SEXP replacement,
    SEXP opts_fixed
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();


    const shared::FixedSearchOptions options =
        fixed::prepare_options(opts_fixed, false);
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    pattern = entry_protections.protect_one(
        ci__prepare_arg_string_r(pattern, "pattern")
    );
    replacement = entry_protections.protect_one(
        ci__prepare_arg_string_r(
            replacement, "replacement"
        )
    );

    const R_len_t subject_length = LENGTH(str);
    const R_len_t pattern_length = LENGTH(pattern);
    const R_len_t replacement_length = LENGTH(replacement);
    bool recycling_warning = false;
    const R_len_t output_length = recycling_length(
        subject_length, pattern_length, replacement_length,
        recycling_warning
    );
    const R_len_t tasks = output_length == 0
        ? 0 : pattern_length == 1
            ? output_length : pattern_length;
    const shared::ParallelPlan plan = shared::parallel_plan(true, tasks);


    int empty_pattern_warnings = 0;

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
        std::vector<shared::StringView> subjects;
        std::vector<shared::StringView> patterns;
        std::vector<shared::StringView> replacements;
        shared::FixedMatcher matcher;
        std::vector<shared::FixedRange> ranges;
        io::OutputBuilder output(0);
        io::ParallelOutputBuilder parallel_output;
        std::vector<int> warning_slots;
        std::vector<R_len_t> first_ineligible;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                if (plan.workers == 1)
                    output.reset(output_length);
                if (output_length > 0) {
                    subject_reader.reset(str);
                    pattern_reader.reset(pattern);
                    replacement_reader.reset(replacement);
                    if (subject_reader.size() != subject_length ||
                            pattern_reader.size() != pattern_length ||
                            replacement_reader.size() != replacement_length) {
                        throw std::runtime_error(
                            "Reader length changed during fixed replacement"
                        );
                    }

                    subject_views.resize(subject_length);
                    subject_reader.views(
                        0, subject_length,
                        subject_views.ptrs(), subject_views.lengths(),
                        subject_views.encodings()
                    );
                    pattern_views.resize(pattern_length);
                    pattern_reader.views(
                        0, pattern_length,
                        pattern_views.ptrs(), pattern_views.lengths(),
                        pattern_views.encodings()
                    );
                    replacement_views.resize(replacement_length);
                    replacement_reader.views(
                        0, replacement_length,
                        replacement_views.ptrs(), replacement_views.lengths(),
                        replacement_views.encodings()
                    );

                    /*
                     * A threaded request must not lose the byte path.
                     * Running the general kernel instead would pay for a
                     * whole normalization pass and a several times more
                     * expensive element, which more than cancels the split.
                     * Both plans therefore try the same direct kernel, and
                     * only the sink differs.
                     *
                     * A threaded refusal abandons the whole attempt: the
                     * general path below rebuilds every element from index 0,
                     * and it resets the sharded builder before it writes, so
                     * partially filled shards are destroyed with the store
                     * they belonged to and can never reach the result. Only
                     * the serial path resumes at general_start.
                     */
                    R_len_t general_start = 0;
                    const bool direct_shape = pattern_length == 1 &&
                        replacement_length == 1;
                    ReplaceDirectState direct_state;
                    bool direct = false;
                    if (plan.workers == 1) {
                        direct = direct_shape &&
                            replace_scalar_byte_direct(
                                subject_views, pattern_views[0],
                                replacement_views[0], output_length,
                                options, false, output, general_start
                            );
                    }
                    else if (direct_shape && replace_direct_eligible(
                            subject_views, pattern_views[0],
                            replacement_views[0], output_length,
                            options, false, direct_state)) {
                        parallel_output.reset(output_length, plan.workers);
                        first_ineligible.assign(
                            plan.workers, output_length
                        );
                        DirectBody direct_body(
                            subject_views, direct_state, first_ineligible,
                            parallel_output
                        );
                        shared::run_parallel(
                            plan, output_length, direct_body
                        );
                        direct = lowest_ineligible(
                            first_ineligible, output_length
                        ) == output_length;
                        if (direct) {
                            result = entry_protections.reprotect_one(
                                parallel_output.to_sexp(), result_index
                            );
                        }
                    }
                    if (!direct) {
                        normalize_views(
                            subject_views, subject_converter,
                            subject_storage, subjects
                        );
                        normalize_views(
                            replacement_views, replacement_converter,
                            replacement_storage, replacements
                        );
                        normalize_views(
                            pattern_views, pattern_converter,
                            pattern_storage, patterns
                        );
                        empty_pattern_warnings =
                            count_empty_patterns(patterns);
                        if (plan.workers > 1) {
                            warning_slots.assign(plan.workers, 0);
                            parallel_output.reset(
                                output_length, plan.workers
                            );
                            Body body(
                                subjects, patterns, replacements,
                                output_length, options, false, false,
                                warning_slots, parallel_output
                            );
                            shared::run_parallel(plan, tasks, body);
                            result = entry_protections.reprotect_one(
                                parallel_output.to_sexp(), result_index
                            );
                        }
                        else {
                            replace_vectorized(
                                subjects, patterns, replacements,
                                output_length, general_start, options, false,
                                matcher, ranges, output
                            );
                        }
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
        emit_warnings(recycling_warning, empty_pattern_warnings);
    );
}


/** Replace every occurrence of a fixed pattern. */
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
    R_len_t output_length = subject_length;
    bool recycling_warning = false;
    if (vectorize) {
        output_length = recycling_length(
            subject_length, pattern_length, replacement_length,
            recycling_warning
        );
    }
    else if (!empty_sequential) {
        require_sequential_lengths(
            pattern_length, replacement_length
        );
        if (pattern_length % replacement_length != 0)
            Rf_warning(MSG__WARN_RECYCLING_RULE);
        options = fixed::prepare_options(opts_fixed, false);
    }
    const bool sequential = !vectorize && pattern_length != 1;
    const R_len_t tasks = output_length == 0
        ? 0 : sequential || pattern_length == 1
            ? output_length : pattern_length;
    const shared::ParallelPlan plan = shared::parallel_plan(true, tasks);


    int empty_pattern_warnings = 0;

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
        std::vector<shared::StringView> sequential_working;
        shared::FixedMatcher matcher;
        std::vector<shared::FixedRange> ranges;
        io::OutputBuilder output(0);
        io::ParallelOutputBuilder parallel_output;
        std::vector<int> warning_slots;
        std::vector<R_len_t> first_ineligible;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                if (plan.workers == 1)
                    output.reset(output_length);
                if (!empty_sequential && output_length > 0) {
                    subject_reader.reset(str);
                    pattern_reader.reset(pattern);
                    replacement_reader.reset(replacement);
                    if (subject_reader.size() != subject_length ||
                            pattern_reader.size() != pattern_length ||
                            replacement_reader.size() != replacement_length) {
                        throw std::runtime_error(
                            "Reader length changed during fixed replacement"
                        );
                    }

                    subject_views.resize(subject_length);
                    subject_reader.views(
                        0, subject_length,
                        subject_views.ptrs(), subject_views.lengths(),
                        subject_views.encodings()
                    );
                    pattern_views.resize(pattern_length);
                    pattern_reader.views(
                        0, pattern_length,
                        pattern_views.ptrs(), pattern_views.lengths(),
                        pattern_views.encodings()
                    );
                    replacement_views.resize(replacement_length);
                    replacement_reader.views(
                        0, replacement_length,
                        replacement_views.ptrs(), replacement_views.lengths(),
                        replacement_views.encodings()
                    );

                    /*
                     * A threaded request must not lose the byte path.
                     * Running the general kernel instead would pay for a
                     * whole normalization pass and a several times more
                     * expensive element, which more than cancels the split.
                     * Both plans therefore try the same direct kernel, and
                     * only the sink differs.
                     *
                     * A threaded refusal abandons the whole attempt: the
                     * general path below rebuilds every element from index 0,
                     * and it resets the sharded builder before it writes, so
                     * partially filled shards are destroyed with the store
                     * they belonged to and can never reach the result. Only
                     * the serial path resumes at general_start.
                     */
                    R_len_t general_start = 0;
                    const bool vectorized_core =
                        vectorize || pattern_length == 1;
                    const bool direct_shape = vectorized_core &&
                        pattern_length == 1 && replacement_length == 1;
                    ReplaceDirectState direct_state;
                    bool direct = false;
                    if (plan.workers == 1) {
                        direct = direct_shape &&
                            replace_scalar_byte_direct(
                                subject_views, pattern_views[0],
                                replacement_views[0], output_length,
                                options, true, output, general_start
                            );
                    }
                    else if (direct_shape && replace_direct_eligible(
                            subject_views, pattern_views[0],
                            replacement_views[0], output_length,
                            options, true, direct_state)) {
                        parallel_output.reset(output_length, plan.workers);
                        first_ineligible.assign(
                            plan.workers, output_length
                        );
                        DirectBody direct_body(
                            subject_views, direct_state, first_ineligible,
                            parallel_output
                        );
                        shared::run_parallel(
                            plan, output_length, direct_body
                        );
                        direct = lowest_ineligible(
                            first_ineligible, output_length
                        ) == output_length;
                        if (direct) {
                            result = entry_protections.reprotect_one(
                                parallel_output.to_sexp(), result_index
                            );
                        }
                    }
                    if (!direct) {
                        normalize_views(
                            subject_views, subject_converter,
                            subject_storage, subjects
                        );
                        normalize_views(
                            replacement_views, replacement_converter,
                            replacement_storage, replacements
                        );
                        normalize_views(
                            pattern_views, pattern_converter,
                            pattern_storage, patterns
                        );
                        empty_pattern_warnings =
                            count_empty_patterns(patterns);

                        if (plan.workers > 1) {
                            warning_slots.assign(plan.workers, 0);
                            parallel_output.reset(
                                output_length, plan.workers
                            );
                            Body body(
                                subjects, patterns, replacements,
                                output_length, options, true, sequential,
                                warning_slots, parallel_output
                            );
                            shared::run_parallel(plan, tasks, body);
                            if (sequential) {
                                int additional_empty_warnings = 0;
                                for (unsigned worker = 0;
                                        worker < plan.workers; ++worker) {
                                    if (warning_slots[worker] >
                                            additional_empty_warnings) {
                                        additional_empty_warnings =
                                            warning_slots[worker];
                                    }
                                }
                                empty_pattern_warnings +=
                                    additional_empty_warnings;
                            }
                            result = entry_protections.reprotect_one(
                                parallel_output.to_sexp(), result_index
                            );
                        }

                        if (plan.workers == 1 && vectorized_core) {
                            replace_vectorized(
                                subjects, patterns, replacements,
                                output_length, general_start, options, true,
                                matcher, ranges, output
                            );
                        }
                        else if (plan.workers == 1) {
                            int additional_empty_warnings = 0;
                            replace_sequential(
                                subjects, patterns, replacements, options,
                                sequential_working, matcher, ranges,
                                sequential_replacement_storage, output,
                                additional_empty_warnings
                            );
                            empty_pattern_warnings +=
                                additional_empty_warnings;
                        }
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
        emit_warnings(recycling_warning, empty_pattern_warnings);
    );
}

} } // namespace charr::altrep_backend
