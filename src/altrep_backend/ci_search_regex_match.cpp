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

#include "io/reader_utils.h"
#include "ci_parallel.h"
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

#include <charport.h>

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace charr { namespace altrep_backend {

namespace search_regex_match {

CHARR_NEUTRAL_HELPER R_len_t recycling_length(
    R_len_t subject_length,
    R_len_t pattern_length,
    bool& warning
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


CHARR_CXX_HELPER shared::StringView normalize_subject(
    const shared::StringView& source,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage
)
{
    if (source.enc == shared::StringEncoding::bytes)
        throw StriException(MSG__BYTESENC);
    return shared::normalize_utf8(source, converter, storage);
}


CHARR_CXX_HELPER shared::StringView normalize_pattern(
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


CHARR_CXX_HELPER void normalize_subjects(
    const charport::StrViews& source,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    std::vector<shared::StringView>& output
)
{
    output.resize(static_cast<std::size_t>(source.size()));
    for (R_xlen_t i = 0; i < source.size(); ++i) {
        output[static_cast<std::size_t>(i)] = normalize_subject(
            io::as_shared_view(source[i]), converter, storage
        );
    }
}


CHARR_CXX_HELPER void normalize_patterns(
    const charport::StrViews& source,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    shared::RegexPatterns& output
)
{
    output.resize(static_cast<std::size_t>(source.size()));
    for (R_xlen_t i = 0; i < source.size(); ++i) {
        output.set(
            static_cast<std::size_t>(i),
            normalize_pattern(
                io::as_shared_view(source[i]), converter, storage
            )
        );
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


CHARR_CXX_HELPER void read_capture_names(
    const shared::RegexMatcher& matcher,
    const shared::RegexPatterns& patterns,
    std::size_t pattern_index,
    std::vector<std::string>& names
)
{
    UErrorCode status = U_ZERO_ERROR;
    matcher.capture_names(names, status);
    if (U_FAILURE(status))
        throw_regex_error(status, false, patterns, pattern_index);
}


CHARR_CXX_HELPER bool find_first(
    shared::RegexMatcher& matcher,
    const shared::StringView& subject,
    const void* subject_identity,
    const shared::RegexPatterns& patterns,
    std::size_t pattern_index,
    shared::RegexRange& match,
    std::vector<shared::RegexRange>& captures
)
{
    UErrorCode status = U_ZERO_ERROR;
    const bool found = matcher.find_first_with_captures(
        subject, subject_identity, match, captures, status
    );
    if (U_FAILURE(status))
        throw_regex_error(status, false, patterns, pattern_index);
    return found;
}


CHARR_CXX_HELPER void find_all(
    shared::RegexMatcher& matcher,
    const shared::StringView& subject,
    const void* subject_identity,
    const shared::RegexPatterns& patterns,
    std::size_t pattern_index,
    std::vector<shared::RegexRange>& matches,
    std::vector<std::vector<shared::RegexRange> >& captures
)
{
    UErrorCode status = U_ZERO_ERROR;
    matcher.find_all_with_captures(
        subject, subject_identity, matches, captures, status
    );
    if (U_FAILURE(status))
        throw_regex_error(status, false, patterns, pattern_index);
}


CHARR_CXX_HELPER void ensure_capture_columns(
    std::vector<std::vector<shared::RegexRange> >& columns,
    std::size_t count,
    std::size_t length
)
{
    const std::size_t previous = columns.size();
    if (previous >= count)
        return;

    columns.resize(count);
    for (std::size_t i = previous; i < count; ++i) {
        columns[i].assign(
            length, shared::RegexRange{NA_INTEGER, NA_INTEGER}
        );
    }
}


CHARR_NEUTRAL_HELPER void stage_no_match(
    std::vector<shared::RegexRange>& matches,
    std::vector<std::vector<shared::RegexRange> >& captures,
    std::size_t group_count,
    std::size_t index
) noexcept
{
    matches[index] = shared::RegexRange{-1, -1};
    for (std::size_t i = 0; i < group_count; ++i)
        captures[i][index] = shared::RegexRange{-1, -1};
}


CHARR_NEUTRAL_HELPER void stage_match(
    const shared::RegexRange& match,
    const std::vector<shared::RegexRange>& current_captures,
    std::vector<shared::RegexRange>& matches,
    std::vector<std::vector<shared::RegexRange> >& captures,
    std::size_t index
) noexcept
{
    matches[index] = match;
    for (std::size_t i = 0; i < current_captures.size(); ++i)
        captures[i][index] = current_captures[i];
}


CHARR_NEUTRAL_HELPER bool capture_names_present(
    const std::vector<std::string>& names
) noexcept
{
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i].size() > 0)
            return true;
    }
    return false;
}


CHARR_CXX_HELPER R_xlen_t matrix_size(
    R_len_t rows, R_len_t columns
)
{
    if (rows < 0 || columns < 0 ||
            (rows > 0 &&
             static_cast<R_xlen_t>(columns) > R_XLEN_T_MAX/rows)) {
        throw std::length_error("matrix length exceeds R's vector limit");
    }
    return static_cast<R_xlen_t>(rows)*columns;
}


CHARR_CXX_HELPER void fill_na(
    io::OutputBuilder& output, R_xlen_t size
)
{
    for (R_xlen_t i = 0; i < size; ++i)
        output.set_na(i);
}


CHARR_NEUTRAL_HELPER charport::StrView matched_view(
    const shared::StringView& subject,
    const shared::RegexRange& range
) noexcept
{
    const int length = range.end-range.start;
    cetype_ext_t encoding = CETYPE_EXT_UTF8;
    if (subject.enc == shared::StringEncoding::ascii)
        encoding = CETYPE_EXT_ASCII;
    else if (subject.enc == shared::StringEncoding::ascii_or_utf8)
        encoding = CETYPE_EXT_ASCII_OR_UTF8;
    return charport::StrView{
        length == 0 ? "" : subject.ptr+range.start,
        length,
        encoding
    };
}


CHARR_CXX_HELPER void set_match_cell(
    io::OutputBuilder& output,
    R_xlen_t index,
    const shared::StringView& subject,
    const shared::RegexRange& range,
    const charport::StrView& capture_missing
)
{
    if (range.start == NA_INTEGER || range.end == NA_INTEGER) {
        output.set_na(index);
        return;
    }
    if (range.start < 0 || range.end < 0) {
        output.set_validated(index, capture_missing);
        return;
    }
    output.set_validated(index, matched_view(subject, range));
}


CHARR_CXX_HELPER void set_match_cell_parallel(
    io::ParallelOutputBuilder& output, unsigned worker, R_xlen_t index,
    const shared::StringView& subject, const shared::RegexRange& range,
    const charport::StrView& capture_missing
)
{
    if (range.start == NA_INTEGER || range.end == NA_INTEGER) {
        output.set_na(worker, index);
        return;
    }
    if (range.start < 0 || range.end < 0) {
        output.set_validated(worker, index, capture_missing);
        return;
    }
    output.set_validated(worker, index, matched_view(subject, range));
}


class ScalarFirstBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER ScalarFirstBody(
        const std::vector<shared::StringView>& subjects,
        const shared::RegexPatterns& patterns,
        const shared::RegexOptions& options, R_len_t rows,
        R_len_t columns, const charport::StrView& capture_missing,
        io::ParallelOutputBuilder& output
    ) noexcept
        : subjects_(subjects), patterns_(patterns), options_(options),
          rows_(rows), columns_(columns), capture_missing_(capture_missing),
          output_(output)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        const shared::RegexInput pattern = patterns_.get(0);
        const bool usable = !pattern.missing && pattern.length > 0;
        shared::RegexMatcher matcher(options_);
        if (usable)
            bind_pattern(matcher, pattern, patterns_, 0);
        shared::RegexRange match{0, 0};
        std::vector<shared::RegexRange> captures;
        while (context.next_chunk()) {
            for (R_xlen_t task = context.begin; task < context.end; ++task) {
                const R_len_t row = static_cast<R_len_t>(task);
                for (R_len_t column = 0; column < columns_; ++column) {
                    output_.set_na(
                        context.worker,
                        row+static_cast<R_xlen_t>(column)*rows_
                    );
                }
                const shared::StringView& subject = subjects_[
                    static_cast<std::size_t>(row)
                ];
                if (!usable || subject.is_na())
                    continue;
                const bool found = find_first(
                    matcher, subject, &subject, patterns_, 0, match, captures
                );
                if (!found) {
                    const shared::RegexRange absent{-1, -1};
                    for (R_len_t column = 0; column < columns_; ++column) {
                        set_match_cell_parallel(
                            output_, context.worker,
                            row+static_cast<R_xlen_t>(column)*rows_,
                            subject, absent, capture_missing_
                        );
                    }
                    continue;
                }
                set_match_cell_parallel(
                    output_, context.worker, row, subject,
                    match, capture_missing_
                );
                for (R_len_t column = 1; column < columns_; ++column) {
                    set_match_cell_parallel(
                        output_, context.worker,
                        row+static_cast<R_xlen_t>(column)*rows_, subject,
                        captures[static_cast<std::size_t>(column-1)],
                        capture_missing_
                    );
                }
            }
        }
    }

private:
    const std::vector<shared::StringView>& subjects_;
    const shared::RegexPatterns& patterns_;
    shared::RegexOptions options_;
    R_len_t rows_;
    R_len_t columns_;
    charport::StrView capture_missing_;
    io::ParallelOutputBuilder& output_;
};


class StageFirstBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER StageFirstBody(
        const std::vector<shared::StringView>& subjects,
        const shared::RegexPatterns& patterns,
        const shared::RegexOptions& options, R_len_t rows,
        std::vector<shared::RegexRange>& matches,
        std::vector<std::vector<shared::RegexRange> >& captures,
        std::vector<int>& warning_slots,
        std::vector<int>& failures
    ) noexcept
        : subjects_(subjects), patterns_(patterns), options_(options),
          rows_(rows), matches_(matches), captures_(captures),
          warning_slots_(warning_slots), failures_(failures)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        try {
            run_unchecked(context);
        }
        catch (...) {
            failures_[context.worker] = 1;
            throw;
        }
    }

private:
    CHARR_CXX_HELPER void run_unchecked(
        shared::WorkerContext& context
    )
    {
        const R_len_t subject_length =
            static_cast<R_len_t>(subjects_.size());
        const R_len_t pattern_length =
            static_cast<R_len_t>(patterns_.size());
        shared::RegexMatcher matcher(options_);
        shared::RegexRange match{0, 0};
        std::vector<shared::RegexRange> current_captures;
        while (context.next_chunk()) {
            for (R_xlen_t task = context.begin; task < context.end; ++task) {
                const R_len_t lane = static_cast<R_len_t>(task);
                const std::size_t pattern_index =
                    static_cast<std::size_t>(lane);
                const shared::RegexInput pattern =
                    patterns_.get(pattern_index);
                if (pattern.missing)
                    continue;
                if (pattern.length <= 0) {
                    for (R_len_t i = lane; i < rows_; i += pattern_length)
                        ++warning_slots_[context.worker];
                    continue;
                }
                bind_pattern(matcher, pattern, patterns_, pattern_index);
                const std::size_t group_count = static_cast<std::size_t>(
                    matcher.group_count()
                );
                for (R_len_t i = lane; i < rows_; i += pattern_length) {
                    std::vector<shared::RegexRange>& row_captures = captures_[
                        static_cast<std::size_t>(i)
                    ];
                    row_captures.assign(
                        group_count,
                        shared::RegexRange{NA_INTEGER, NA_INTEGER}
                    );
                    const std::size_t subject_index =
                        static_cast<std::size_t>(i % subject_length);
                    const shared::StringView& subject =
                        subjects_[subject_index];
                    if (subject.is_na())
                        continue;
                    const bool found = find_first(
                        matcher, subject, &subjects_[subject_index],
                        patterns_, pattern_index, match, current_captures
                    );
                    if (!found) {
                        matches_[static_cast<std::size_t>(i)] =
                            shared::RegexRange{-1, -1};
                        for (std::size_t column = 0;
                                column < group_count; ++column) {
                            row_captures[column] =
                                shared::RegexRange{-1, -1};
                        }
                    }
                    else {
                        matches_[static_cast<std::size_t>(i)] = match;
                        row_captures = current_captures;
                    }
                }
            }
        }
    }

    const std::vector<shared::StringView>& subjects_;
    const shared::RegexPatterns& patterns_;
    shared::RegexOptions options_;
    R_len_t rows_;
    std::vector<shared::RegexRange>& matches_;
    std::vector<std::vector<shared::RegexRange> >& captures_;
    std::vector<int>& warning_slots_;
    std::vector<int>& failures_;
};


class FillFirstBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER FillFirstBody(
        const std::vector<shared::StringView>& subjects,
        R_len_t subject_length, R_len_t rows, R_len_t columns,
        const std::vector<shared::RegexRange>& matches,
        const std::vector<std::vector<shared::RegexRange> >& captures,
        const charport::StrView& capture_missing,
        io::ParallelOutputBuilder& output
    ) noexcept
        : subjects_(subjects), subject_length_(subject_length),
          rows_(rows), columns_(columns), matches_(matches),
          captures_(captures), capture_missing_(capture_missing),
          output_(output)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        while (context.next_chunk()) {
            for (R_xlen_t task = context.begin; task < context.end; ++task) {
                const R_len_t row = static_cast<R_len_t>(task);
                const shared::StringView& subject = subjects_[
                    static_cast<std::size_t>(row % subject_length_)
                ];
                set_match_cell_parallel(
                    output_, context.worker, row, subject,
                    matches_[static_cast<std::size_t>(row)], capture_missing_
                );
                const std::vector<shared::RegexRange>& row_captures =
                    captures_[static_cast<std::size_t>(row)];
                for (R_len_t column = 1; column < columns_; ++column) {
                    const std::size_t capture =
                        static_cast<std::size_t>(column-1);
                    const shared::RegexRange range =
                        capture < row_captures.size()
                            ? row_captures[capture]
                            : shared::RegexRange{NA_INTEGER, NA_INTEGER};
                    set_match_cell_parallel(
                        output_, context.worker,
                        row+static_cast<R_xlen_t>(column)*rows_,
                        subject, range, capture_missing_
                    );
                }
            }
        }
    }

private:
    const std::vector<shared::StringView>& subjects_;
    R_len_t subject_length_;
    R_len_t rows_;
    R_len_t columns_;
    const std::vector<shared::RegexRange>& matches_;
    const std::vector<std::vector<shared::RegexRange> >& captures_;
    charport::StrView capture_missing_;
    io::ParallelOutputBuilder& output_;
};


CHARR_CXX_HELPER void fill_staged_first(
    io::OutputBuilder& output,
    R_len_t rows,
    R_len_t subject_length,
    const std::vector<shared::StringView>& subjects,
    const std::vector<shared::RegexRange>& matches,
    const std::vector<std::vector<shared::RegexRange> >& captures,
    const charport::StrView& capture_missing
)
{
    for (R_len_t row = 0; row < rows; ++row) {
        const shared::StringView& subject = subjects[
            static_cast<std::size_t>(row % subject_length)
        ];
        set_match_cell(
            output, row, subject,
            matches[static_cast<std::size_t>(row)], capture_missing
        );
        for (std::size_t column = 0; column < captures.size(); ++column) {
            set_match_cell(
                output,
                row+static_cast<R_xlen_t>(column+1)*rows,
                subject, captures[column][static_cast<std::size_t>(row)],
                capture_missing
            );
        }
    }
}


CHARR_CXX_HELPER void build_all_matches(
    io::OutputBuilder& output,
    const shared::StringView& subject,
    const std::vector<shared::RegexRange>& matches,
    const std::vector<std::vector<shared::RegexRange> >& captures,
    bool omit_no_match,
    const charport::StrView& capture_missing,
    R_len_t& rows,
    R_len_t& columns
)
{
    columns = static_cast<R_len_t>(captures.size()+1);
    const R_len_t count = static_cast<R_len_t>(matches.size());
    rows = count > 0 ? count : (omit_no_match ? 0 : 1);
    const R_xlen_t size = matrix_size(rows, columns);
    output.reset(size);
    fill_na(output, size);

    for (R_len_t row = 0; row < count; ++row) {
        set_match_cell(
            output, row, subject,
            matches[static_cast<std::size_t>(row)], capture_missing
        );
        for (R_len_t column = 1; column < columns; ++column) {
            set_match_cell(
                output, row+static_cast<R_xlen_t>(column)*rows,
                subject,
                captures[static_cast<std::size_t>(column-1)][
                    static_cast<std::size_t>(row)
                ],
                capture_missing
            );
        }
    }
}


class CHARR_OWNER_TYPE AllCaptureNames {
public:
    CHARR_CXX_HELPER AllCaptureNames() noexcept
        : rows_(nullptr) {}

    CHARR_CXX_HELPER ~AllCaptureNames() noexcept
    {
        delete[] rows_;
    }

    AllCaptureNames(const AllCaptureNames&) = delete;
    AllCaptureNames& operator=(const AllCaptureNames&) = delete;
    AllCaptureNames(AllCaptureNames&&) = delete;
    AllCaptureNames& operator=(AllCaptureNames&&) = delete;

    CHARR_CXX_HELPER void reset(std::size_t size)
    {
        std::vector<std::string>* replacement = size == 0
            ? nullptr : new std::vector<std::string>[size];
        delete[] rows_;
        rows_ = replacement;
    }

    CHARR_NEUTRAL_HELPER std::vector<std::string>& at(
        std::size_t index
    ) noexcept
    {
        return rows_[index];
    }

    CHARR_NEUTRAL_HELPER const std::vector<std::string>& at(
        std::size_t index
    ) const noexcept
    {
        return rows_[index];
    }

private:
    std::vector<std::string>* rows_;
};


class AllMatchBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER AllMatchBody(
        const std::vector<shared::StringView>& subjects,
        const shared::RegexPatterns& patterns,
        const shared::RegexOptions& options,
        R_len_t subject_length,
        R_len_t pattern_length,
        R_len_t vectorize_length,
        bool omit,
        const charport::StrView& capture_missing,
        std::vector<io::OutputStore>& stores,
        std::vector<int>& row_counts,
        std::vector<int>& column_counts,
        AllCaptureNames& capture_names,
        std::vector<int>& warning_slots,
        std::vector<int>& failures
    ) noexcept
        : subjects_(subjects), patterns_(patterns), options_(options),
          subject_length_(subject_length), pattern_length_(pattern_length),
          vectorize_length_(vectorize_length), omit_(omit),
          capture_missing_(capture_missing), stores_(stores),
          row_counts_(row_counts), column_counts_(column_counts),
          capture_names_(capture_names), warning_slots_(warning_slots),
          failures_(failures)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        try {
            run_unchecked(context);
        }
        catch (...) {
            failures_[context.worker] = 1;
            throw;
        }
    }

private:
    // What binding one pattern into the matcher established. Held across the
    // elements that pattern covers so the bind is not repeated.
    struct BoundPattern {
        bool unusable;
        bool empty;
        int group_count;
    };

    CHARR_CXX_HELPER void run_unchecked(
        shared::WorkerContext& context
    )
    {
        shared::RegexMatcher matcher(options_);
        std::vector<shared::RegexRange> matches;
        std::vector<std::vector<shared::RegexRange> > captures;
        io::OutputBuilder builder(0);
        matches.reserve(8);

        // One pattern for the whole vector: compile it once for this worker,
        // then let the worker draw element chunks against the bound matcher.
        if (pattern_length_ == 1) {
            const BoundPattern bound =
                bind_for_pattern(0, context.worker, matcher);
            while (context.next_chunk()) {
                const R_len_t begin = static_cast<R_len_t>(context.begin);
                const R_len_t end = static_cast<R_len_t>(context.end);
                for (R_len_t i = begin; i < end; ++i) {
                    stage_one(
                        i, bound.unusable, bound.empty,
                        bound.group_count, 0, context.worker,
                        matcher, matches, captures, builder
                    );
                }
            }
            return;
        }

        // A task is a recycling lane, so the chunk cuts the lanes and each
        // lane still strides the whole vector. The bind belongs to the lane,
        // not to the chunk.
        while (context.next_chunk()) {
            const R_len_t begin = static_cast<R_len_t>(context.begin);
            const R_len_t end = static_cast<R_len_t>(context.end);
            for (R_len_t lane = begin; lane < end; ++lane) {
                const std::size_t pattern_index =
                    static_cast<std::size_t>(lane);
                const BoundPattern bound = bind_for_pattern(
                    pattern_index, static_cast<unsigned>(lane), matcher
                );
                for (R_len_t i = lane; i < vectorize_length_;
                        i += pattern_length_) {
                    stage_one(
                        i, bound.unusable, bound.empty,
                        bound.group_count, pattern_index, context.worker,
                        matcher, matches, captures, builder
                    );
                }
            }
        }
    }

    CHARR_CXX_HELPER BoundPattern bind_for_pattern(
        std::size_t pattern_index,
        unsigned metadata_index,
        shared::RegexMatcher& matcher
    )
    {
        const shared::RegexInput pattern = patterns_.get(pattern_index);
        BoundPattern bound;
        bound.empty = !pattern.missing && pattern.length <= 0;
        bound.unusable = pattern.missing || bound.empty;
        bound.group_count = 0;

        if (!bound.unusable) {
            bind_pattern(matcher, pattern, patterns_, pattern_index);
            bound.group_count = matcher.group_count();
            read_capture_names(
                matcher, patterns_, pattern_index,
                capture_names_.at(metadata_index)
            );
        }
        return bound;
    }

    CHARR_CXX_HELPER void stage_one(
        R_len_t i,
        bool pattern_unusable,
        bool pattern_empty,
        int group_count,
        std::size_t pattern_index,
        unsigned warning_index,
        shared::RegexMatcher& matcher,
        std::vector<shared::RegexRange>& matches,
        std::vector<std::vector<shared::RegexRange> >& captures,
        io::OutputBuilder& builder
    )
    {
        R_len_t rows = 1;
        R_len_t columns = 1;
        if (pattern_unusable) {
            if (pattern_empty)
                ++warning_slots_[warning_index];
            builder.reset(1);
            builder.set_na(0);
        }
        else {
            columns = group_count+1;
            const std::size_t subject_index =
                static_cast<std::size_t>(i % subject_length_);
            const shared::StringView& subject = subjects_[subject_index];
            if (subject.is_na()) {
                const R_xlen_t size = matrix_size(1, columns);
                builder.reset(size);
                fill_na(builder, size);
            }
            else {
                find_all(
                    matcher, subject, &subjects_[subject_index],
                    patterns_, pattern_index, matches, captures
                );
                build_all_matches(
                    builder, subject, matches, captures,
                    omit_, capture_missing_, rows, columns
                );
            }
        }

        stores_[static_cast<std::size_t>(i)] = builder.release_store();
        row_counts_[static_cast<std::size_t>(i)] = rows;
        column_counts_[static_cast<std::size_t>(i)] = columns;
    }

    const std::vector<shared::StringView>& subjects_;
    const shared::RegexPatterns& patterns_;
    shared::RegexOptions options_;
    R_len_t subject_length_;
    R_len_t pattern_length_;
    R_len_t vectorize_length_;
    bool omit_;
    charport::StrView capture_missing_;
    std::vector<io::OutputStore>& stores_;
    std::vector<int>& row_counts_;
    std::vector<int>& column_counts_;
    AllCaptureNames& capture_names_;
    std::vector<int>& warning_slots_;
    std::vector<int>& failures_;
};


CHARR_R_HELPER SEXP capture_dimnames_r(
    const std::vector<std::string>& names
) noexcept
{
    if (!capture_names_present(names))
        return R_NilValue;

    SEXP dimnames = PROTECT(Rf_allocVector(VECSXP, 2));
    SEXP columns = PROTECT(Rf_allocVector(STRSXP, names.size()+1));
    SET_STRING_ELT(columns, 0, R_BlankString);
    for (std::size_t i = 0; i < names.size(); ++i) {
        SET_STRING_ELT(
            columns, i+1,
            Rf_mkCharLenCE(
                names[i].size() == 0 ? "" : names[i].data(),
                static_cast<int>(names[i].size()), CE_UTF8
            )
        );
    }
    SET_VECTOR_ELT(dimnames, 1, columns);
    UNPROTECT(2);
    return dimnames;
}


CHARR_R_HELPER void set_matrix_attributes_r(
    SEXP matrix,
    R_len_t rows,
    R_len_t columns,
    SEXP dimnames
) noexcept
{
    SEXP dimensions = PROTECT(Rf_allocVector(INTSXP, 2));
    INTEGER(dimensions)[0] = rows;
    INTEGER(dimensions)[1] = columns;
    Rf_setAttrib(matrix, R_DimSymbol, dimensions);
    if (dimnames != R_NilValue)
        Rf_setAttrib(matrix, R_DimNamesSymbol, dimnames);
    UNPROTECT(1);
}


CHARR_R_HELPER void emit_warnings_r(
    int empty_pattern_warnings
) noexcept
{
    for (int i = 0; i < empty_pattern_warnings; ++i)
        Rf_warning(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
}

} // namespace search_regex_match

using namespace search_regex_match;


/** Extract the first regular-expression match and its capture groups. */
CHARR_ENTRYPOINT SEXP ci_match_first_regex(
    SEXP str, SEXP pattern, SEXP cg_missing, SEXP opts_regex
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();


    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    pattern = entry_protections.protect_one(
        ci__prepare_arg_string_r(pattern, "pattern")
    );
    cg_missing = entry_protections.protect_one(
        ci__prepare_arg_string_1_r(
            cg_missing, "cg_missing"
        )
    );

    const R_xlen_t subject_xlength = XLENGTH(str);
    const R_xlen_t pattern_xlength = XLENGTH(pattern);
    if (subject_xlength > R_LEN_T_MAX || pattern_xlength > R_LEN_T_MAX)
        Rf_error("long character vectors are not supported");
    const R_len_t subject_length = static_cast<R_len_t>(subject_xlength);
    const R_len_t pattern_length = static_cast<R_len_t>(pattern_xlength);
    bool recycling_warning = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, recycling_warning
    );
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);
    const shared::RegexOptions options = regex::prepare_options(opts_regex);
    const R_len_t tasks = vectorize_length == 0
        ? 0 : pattern_length == 1
            ? vectorize_length : pattern_length;
    const shared::ParallelPlan plan = shared::parallel_plan(true, tasks);


    int empty_pattern_warnings = 0;
    R_len_t result_columns = 1;
    bool parallel_result = false;

    try {
        charport::Reader subject_reader;
        charport::Reader pattern_reader;
        charport::Reader missing_reader;
        charport::StrViews subject_views;
        charport::StrViews pattern_views;
        charport::StrViews missing_views;
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::NativeToUtf8 missing_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        shared::SliceArena missing_storage;
        std::vector<shared::StringView> subjects;
        shared::RegexPatterns patterns;
        shared::RegexMatcher matcher(options);
        shared::RegexRange current_match{0, 0};
        std::vector<shared::RegexRange> current_captures;
        std::vector<shared::RegexRange> staged_matches;
        std::vector<std::vector<shared::RegexRange> > staged_captures;
        std::vector<std::vector<shared::RegexRange> > row_captures;
        std::vector<std::string> capture_names;
        std::vector<int> worker_empty_pattern_warnings;
        std::vector<int> worker_failures;
        io::OutputBuilder output(0);
        io::ParallelOutputBuilder parallel_output;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                missing_reader.reset(cg_missing);
                if (missing_reader.size() != 1) {
                    throw std::runtime_error(
                        "Reader length changed for cg_missing"
                    );
                }
                missing_views.resize(1);
                missing_reader.views(
                    0, 1,
                    missing_views.ptrs(), missing_views.lengths(),
                    missing_views.encodings()
                );
                const charport::StrView capture_missing = missing_views[0];
                (void)normalize_subject(
                    io::as_shared_view(capture_missing),
                    missing_converter, missing_storage
                );

                if (vectorize_length > 0) {
                    subject_reader.reset(str);
                    if (subject_reader.size() != subject_length) {
                        throw std::runtime_error(
                            "Reader length changed during regex match"
                        );
                    }
                    subject_views.resize(subject_length);
                    subject_reader.views(
                        0, subject_length,
                        subject_views.ptrs(), subject_views.lengths(),
                        subject_views.encodings()
                    );
                    normalize_subjects(
                        subject_views, subject_converter,
                        subject_storage, subjects
                    );
                }

                patterns.resize(static_cast<std::size_t>(pattern_length));
                if (pattern_length > 0) {
                    pattern_reader.reset(pattern);
                    if (pattern_reader.size() != pattern_length) {
                        throw std::runtime_error(
                            "Reader length changed during regex match"
                        );
                    }
                    pattern_views.resize(pattern_length);
                    pattern_reader.views(
                        0, pattern_length,
                        pattern_views.ptrs(), pattern_views.lengths(),
                        pattern_views.encodings()
                    );
                    normalize_patterns(
                        pattern_views, pattern_converter,
                        pattern_storage, patterns
                    );
                }
                empty_pattern_warnings = patterns.empty_count();

                if (pattern_length == 1) {
                    const std::size_t pattern_index = 0;
                    const shared::RegexInput current_pattern =
                        patterns.get(pattern_index);
                    const bool pattern_empty =
                        !current_pattern.missing &&
                        current_pattern.length <= 0;
                    int group_count = 0;
                    if (!current_pattern.missing && !pattern_empty) {
                        bind_pattern(
                            matcher, current_pattern, patterns,
                            pattern_index
                        );
                        group_count = matcher.group_count();
                        read_capture_names(
                            matcher, patterns, pattern_index,
                            capture_names
                        );
                    }
                    if (pattern_empty) {
                        empty_pattern_warnings += vectorize_length > 0
                            ? vectorize_length
                            : 1;
                    }

                    result_columns = group_count+1;
                    const R_xlen_t size = matrix_size(
                        vectorize_length, result_columns
                    );
                    if (plan.workers > 1) {
                        parallel_output.reset(size, plan.workers);
                        ScalarFirstBody body(
                            subjects, patterns, options,
                            vectorize_length, result_columns,
                            capture_missing, parallel_output
                        );
                        shared::run_parallel(
                            plan, vectorize_length, body
                        );
                        parallel_result = true;
                    }
                    else {
                        output.reset(size);
                        fill_na(output, size);
                    }
                    if (plan.workers == 1 &&
                            !current_pattern.missing && !pattern_empty) {
                        for (R_len_t i = 0; i < vectorize_length; ++i) {
                            const shared::StringView& subject = subjects[
                                static_cast<std::size_t>(i)
                            ];
                            if (subject.is_na())
                                continue;

                            const bool found = find_first(
                                matcher, subject, &subject,
                                patterns, pattern_index,
                                current_match, current_captures
                            );
                            if (!found) {
                                current_match = shared::RegexRange{-1, -1};
                                for (int column = 0;
                                        column <= group_count; ++column) {
                                    set_match_cell(
                                        output,
                                        i+static_cast<R_xlen_t>(column)*
                                            vectorize_length,
                                        subject, current_match,
                                        capture_missing
                                    );
                                }
                                continue;
                            }

                            set_match_cell(
                                output, i, subject, current_match,
                                capture_missing
                            );
                            for (int column = 0;
                                    column < group_count; ++column) {
                                set_match_cell(
                                    output,
                                    i+static_cast<R_xlen_t>(column+1)*
                                        vectorize_length,
                                    subject,
                                    current_captures[
                                        static_cast<std::size_t>(column)
                                    ],
                                    capture_missing
                                );
                            }
                        }
                    }
                }
                else {
                    staged_matches.assign(
                        static_cast<std::size_t>(vectorize_length),
                        shared::RegexRange{NA_INTEGER, NA_INTEGER}
                    );

                    if (plan.workers > 1) {
                        row_captures.resize(
                            static_cast<std::size_t>(vectorize_length)
                        );
                        worker_empty_pattern_warnings.assign(
                            plan.workers, 0
                        );
                        worker_failures.assign(plan.workers, 0);
                        StageFirstBody body(
                            subjects, patterns, options, vectorize_length,
                            staged_matches, row_captures,
                            worker_empty_pattern_warnings, worker_failures
                        );
                        try {
                            shared::run_parallel(plan, pattern_length, body);
                        }
                        catch (...) {
                            std::size_t limit = 0;
                            while (limit < worker_failures.size() &&
                                    worker_failures[limit] == 0) {
                                ++limit;
                            }
                            if (limit < worker_failures.size())
                                ++limit;
                            for (std::size_t worker = 0;
                                    worker < limit; ++worker) {
                                empty_pattern_warnings +=
                                    worker_empty_pattern_warnings[worker];
                            }
                            throw;
                        }
                        for (std::size_t worker = 0;
                                worker < worker_empty_pattern_warnings.size();
                                ++worker) {
                            empty_pattern_warnings +=
                                worker_empty_pattern_warnings[worker];
                        }
                    }

                    if (plan.workers == 1) for (R_len_t lane = 0;
                            lane < pattern_length; ++lane) {
                        const std::size_t pattern_index =
                            static_cast<std::size_t>(lane);
                        const shared::RegexInput current_pattern =
                            patterns.get(pattern_index);
                        const bool pattern_empty =
                            !current_pattern.missing &&
                            current_pattern.length <= 0;
                        if (pattern_empty) {
                            if (vectorize_length <= 0) {
                                ++empty_pattern_warnings;
                            }
                            else {
                                for (R_len_t i = lane;
                                        i < vectorize_length;
                                        i += pattern_length) {
                                    ++empty_pattern_warnings;
                                }
                            }
                            continue;
                        }
                        if (current_pattern.missing)
                            continue;

                        bind_pattern(
                            matcher, current_pattern, patterns,
                            pattern_index
                        );
                        const std::size_t group_count =
                            static_cast<std::size_t>(
                                matcher.group_count()
                            );
                        ensure_capture_columns(
                            staged_captures, group_count,
                            static_cast<std::size_t>(vectorize_length)
                        );

                        for (R_len_t i = lane;
                                i < vectorize_length;
                                i += pattern_length) {
                            const std::size_t subject_index =
                                static_cast<std::size_t>(
                                    i % subject_length
                                );
                            const shared::StringView& subject =
                                subjects[subject_index];
                            if (subject.is_na())
                                continue;

                            const bool found = find_first(
                                matcher, subject,
                                &subjects[subject_index],
                                patterns, pattern_index,
                                current_match, current_captures
                            );
                            if (!found) {
                                stage_no_match(
                                    staged_matches, staged_captures,
                                    group_count,
                                    static_cast<std::size_t>(i)
                                );
                            }
                            else {
                                stage_match(
                                    current_match, current_captures,
                                    staged_matches, staged_captures,
                                    static_cast<std::size_t>(i)
                                );
                            }
                        }
                    }

                    if (plan.workers > 1) {
                        std::size_t maximum_captures = 0;
                        for (std::size_t i = 0;
                                i < row_captures.size(); ++i) {
                            if (row_captures[i].size() > maximum_captures)
                                maximum_captures = row_captures[i].size();
                        }
                        result_columns = static_cast<R_len_t>(
                            maximum_captures+1
                        );
                    }
                    else {
                        result_columns = static_cast<R_len_t>(
                            staged_captures.size()+1
                        );
                    }
                    const R_xlen_t size = matrix_size(
                        vectorize_length, result_columns
                    );
                    if (plan.workers > 1) {
                        const shared::ParallelPlan fill_plan =
                            shared::parallel_plan(true, vectorize_length);
                        parallel_output.reset(size, fill_plan.workers);
                        FillFirstBody body(
                            subjects, subject_length, vectorize_length,
                            result_columns, staged_matches, row_captures,
                            capture_missing, parallel_output
                        );
                        shared::run_parallel(
                            fill_plan, vectorize_length, body
                        );
                        parallel_result = true;
                    }
                    else {
                        output.reset(size);
                        fill_na(output, size);
                        fill_staged_first(
                            output, vectorize_length, subject_length,
                            subjects, staged_matches, staged_captures,
                            capture_missing
                        );
                    }
                }

                result = entry_protections.reprotect_one(
                    parallel_result
                        ? parallel_output.to_sexp()
                        : output.to_sexp(),
                    result_index
                );
                SEXP dimnames = R_NilValue;
                PROTECT_INDEX dimnames_index;
                callback_protections.protect_with_index(
                    dimnames, &dimnames_index
                );
                dimnames = callback_protections.reprotect_slot(
                    capture_dimnames_r(capture_names),
                    dimnames_index
                );
                set_matrix_attributes_r(
                    result, vectorize_length, result_columns, dimnames
                );

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_warnings_r(empty_pattern_warnings);
    );
}


/** Extract every regular-expression match and its capture groups. */
CHARR_ENTRYPOINT SEXP ci_match_all_regex(
    SEXP str,
    SEXP pattern,
    SEXP omit_no_match,
    SEXP cg_missing,
    SEXP opts_regex
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool omit = ci__prepare_arg_logical_1_notNA_r(
        omit_no_match, "omit_no_match"
    );
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    pattern = entry_protections.protect_one(
        ci__prepare_arg_string_r(pattern, "pattern")
    );
    cg_missing = entry_protections.protect_one(
        ci__prepare_arg_string_1_r(
            cg_missing, "cg_missing"
        )
    );

    const R_xlen_t subject_xlength = XLENGTH(str);
    const R_xlen_t pattern_xlength = XLENGTH(pattern);
    if (subject_xlength > R_LEN_T_MAX || pattern_xlength > R_LEN_T_MAX)
        Rf_error("long character vectors are not supported");
    const R_len_t subject_length = static_cast<R_len_t>(subject_xlength);
    const R_len_t pattern_length = static_cast<R_len_t>(pattern_xlength);
    bool recycling_warning = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, recycling_warning
    );
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);
    const shared::RegexOptions options = regex::prepare_options(opts_regex);


    int empty_pattern_warnings = 0;

    try {
        charport::Reader subject_reader;
        charport::Reader pattern_reader;
        charport::Reader missing_reader;
        charport::StrViews subject_views;
        charport::StrViews pattern_views;
        charport::StrViews missing_views;
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::NativeToUtf8 missing_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        shared::SliceArena missing_storage;
        std::vector<shared::StringView> subjects;
        shared::RegexPatterns patterns;
        shared::RegexMatcher matcher(options);
        std::vector<shared::RegexRange> matches;
        std::vector<std::vector<shared::RegexRange> > captures;
        std::vector<std::string> capture_names;
        io::OutputBuilder output(0);
        std::vector<io::OutputStore> stores;
        std::vector<int> row_counts;
        std::vector<int> column_counts;
        AllCaptureNames staged_capture_names;
        std::vector<int> worker_empty_pattern_warnings;
        std::vector<int> worker_failures;

        matches.reserve(8);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                missing_reader.reset(cg_missing);
                if (missing_reader.size() != 1) {
                    throw std::runtime_error(
                        "Reader length changed for cg_missing"
                    );
                }
                missing_views.resize(1);
                missing_reader.views(
                    0, 1,
                    missing_views.ptrs(), missing_views.lengths(),
                    missing_views.encodings()
                );
                const charport::StrView capture_missing = missing_views[0];
                (void)normalize_subject(
                    io::as_shared_view(capture_missing),
                    missing_converter, missing_storage
                );

                result = entry_protections.reprotect_one(
                    Rf_allocVector(VECSXP, vectorize_length),
                    result_index
                );
                if (vectorize_length > 0) {
                    subject_reader.reset(str);
                    if (subject_reader.size() != subject_length) {
                        throw std::runtime_error(
                            "Reader length changed during regex match"
                        );
                    }
                    subject_views.resize(subject_length);
                    subject_reader.views(
                        0, subject_length,
                        subject_views.ptrs(), subject_views.lengths(),
                        subject_views.encodings()
                    );
                    normalize_subjects(
                        subject_views, subject_converter,
                        subject_storage, subjects
                    );

                    pattern_reader.reset(pattern);
                    if (pattern_reader.size() != pattern_length) {
                        throw std::runtime_error(
                            "Reader length changed during regex match"
                        );
                    }
                    pattern_views.resize(pattern_length);
                    pattern_reader.views(
                        0, pattern_length,
                        pattern_views.ptrs(), pattern_views.lengths(),
                        pattern_views.encodings()
                    );
                    normalize_patterns(
                        pattern_views, pattern_converter,
                        pattern_storage, patterns
                    );
                    empty_pattern_warnings = patterns.empty_count();

                    SEXP current = R_NilValue;
                    PROTECT_INDEX current_index;
                    callback_protections.protect_with_index(current, &current_index);
                    SEXP dimnames = R_NilValue;
                    PROTECT_INDEX dimnames_index;
                    callback_protections.protect_with_index(
                        dimnames, &dimnames_index
                    );

                    const R_len_t tasks = pattern_length == 1
                        ? vectorize_length
                        : pattern_length;
                    const shared::ParallelPlan parallel_plan =
                        shared::parallel_plan(true, tasks);
                    if (parallel_plan.workers == 1) {
                    for (R_len_t lane = 0; lane < pattern_length; ++lane) {
                        const std::size_t pattern_index =
                            static_cast<std::size_t>(lane);
                        const shared::RegexInput current_pattern =
                            patterns.get(pattern_index);
                        const bool pattern_empty =
                            !current_pattern.missing &&
                            current_pattern.length <= 0;
                        int group_count = 0;

                        if (!current_pattern.missing && !pattern_empty) {
                            bind_pattern(
                                matcher, current_pattern, patterns,
                                pattern_index
                            );
                            group_count = matcher.group_count();
                            read_capture_names(
                                matcher, patterns, pattern_index,
                                capture_names
                            );
                            dimnames = callback_protections.reprotect_slot(
                                capture_dimnames_r(capture_names),
                                dimnames_index
                            );
                        }
                        else {
                            capture_names.clear();
                            dimnames = callback_protections.reprotect_slot(
                                R_NilValue, dimnames_index
                            );
                        }

                        for (R_len_t i = lane;
                                i < vectorize_length;
                                i += pattern_length) {
                            R_len_t rows = 1;
                            R_len_t columns = 1;
                            if (current_pattern.missing || pattern_empty) {
                                if (pattern_empty)
                                    ++empty_pattern_warnings;
                                output.reset(1);
                                output.set_na(0);
                            }
                            else {
                                columns = group_count+1;
                                const std::size_t subject_index =
                                    static_cast<std::size_t>(
                                        i % subject_length
                                    );
                                const shared::StringView& subject =
                                    subjects[subject_index];
                                if (subject.is_na()) {
                                    const R_xlen_t size = matrix_size(
                                        1, columns
                                    );
                                    output.reset(size);
                                    fill_na(output, size);
                                }
                                else {
                                    find_all(
                                        matcher, subject,
                                        &subjects[subject_index],
                                        patterns, pattern_index,
                                        matches, captures
                                    );
                                    build_all_matches(
                                        output, subject, matches, captures,
                                        omit, capture_missing,
                                        rows, columns
                                    );
                                }
                            }

                            current = callback_protections.reprotect_slot(
                                output.to_sexp(), current_index
                            );
                            set_matrix_attributes_r(
                                current, rows, columns, dimnames
                            );
                            SET_VECTOR_ELT(result, i, current);
                        }
                    }
                    }
                    else {
                        stores.reserve(
                            static_cast<std::size_t>(vectorize_length)
                        );
                        for (R_len_t i = 0;
                                i < vectorize_length; ++i) {
                            stores.emplace_back(0, 0);
                        }
                        row_counts.resize(
                            static_cast<std::size_t>(vectorize_length)
                        );
                        column_counts.resize(
                            static_cast<std::size_t>(vectorize_length)
                        );
                        const std::size_t metadata_count =
                            pattern_length == 1
                                ? static_cast<std::size_t>(
                                    parallel_plan.workers
                                )
                                : static_cast<std::size_t>(pattern_length);
                        staged_capture_names.reset(metadata_count);
                        worker_empty_pattern_warnings.assign(
                            static_cast<std::size_t>(
                                parallel_plan.workers
                            ),
                            0
                        );
                        worker_failures.assign(
                            static_cast<std::size_t>(
                                parallel_plan.workers
                            ),
                            0
                        );
                        AllMatchBody body(
                            subjects, patterns, options,
                            subject_length, pattern_length,
                            vectorize_length, omit, capture_missing,
                            stores, row_counts, column_counts,
                            staged_capture_names,
                            worker_empty_pattern_warnings,
                            worker_failures
                        );
                        try {
                            shared::run_parallel(
                                parallel_plan, tasks, body
                            );
                        }
                        catch (...) {
                            std::size_t limit = 0;
                            while (limit < worker_failures.size() &&
                                    worker_failures[limit] == 0) {
                                ++limit;
                            }
                            if (limit < worker_failures.size())
                                ++limit;
                            for (std::size_t i = 0; i < limit; ++i) {
                                empty_pattern_warnings +=
                                    worker_empty_pattern_warnings[i];
                            }
                            throw;
                        }
                        for (std::size_t i = 0;
                                i < worker_empty_pattern_warnings.size();
                                ++i) {
                            empty_pattern_warnings +=
                                worker_empty_pattern_warnings[i];
                        }

                        for (R_len_t lane = 0;
                                lane < pattern_length; ++lane) {
                            const std::size_t metadata_index =
                                pattern_length == 1
                                    ? 0
                                    : static_cast<std::size_t>(lane);
                            dimnames = callback_protections.reprotect_slot(
                                capture_dimnames_r(
                                    staged_capture_names.at(metadata_index)
                                ),
                                dimnames_index
                            );
                            for (R_len_t i = lane;
                                    i < vectorize_length;
                                    i += pattern_length) {
                                current = callback_protections.reprotect_slot(
                                    io::finalize(std::move(stores[
                                        static_cast<std::size_t>(i)
                                    ])),
                                    current_index
                                );
                                set_matrix_attributes_r(
                                    current,
                                    row_counts[
                                        static_cast<std::size_t>(i)
                                    ],
                                    column_counts[
                                        static_cast<std::size_t>(i)
                                    ],
                                    dimnames
                                );
                                SET_VECTOR_ELT(result, i, current);
                            }
                        }
                    }
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_warnings_r(empty_pattern_warnings);
    );
}

} } // namespace charr::altrep_backend
