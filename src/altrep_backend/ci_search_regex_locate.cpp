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

#include "ci_parallel.h"
#include "ci_stringi.h"
#include "io/reader_utils.h"
#include "io/string_view.h"
#include "regex/options_r.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/r_matrix.h"
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

namespace search_regex_locate {

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


CHARR_CXX_HELPER void normalize_inputs(
    const charport::StrViews& source,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    std::vector<shared::StringView>& output
)
{
    output.resize(static_cast<std::size_t>(source.size()));
    for (R_xlen_t i = 0; i < source.size(); ++i) {
        output[static_cast<std::size_t>(i)] = normalize_input(
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
            normalize_input(
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


CHARR_CXX_HELPER bool locate_first(
    shared::RegexMatcher& matcher,
    const shared::StringView& subject,
    const void* subject_identity,
    bool capture_groups,
    bool return_length,
    const shared::RegexPatterns& patterns,
    std::size_t pattern_index,
    shared::RegexRange& match,
    std::vector<shared::RegexRange>& captures
)
{
    UErrorCode status = U_ZERO_ERROR;
    const bool found = capture_groups
        ? matcher.find_first_with_captures(
            subject, subject_identity, match, captures, status
        )
        : matcher.find_first(
            subject, subject_identity, match, status
        );
    if (U_FAILURE(status))
        throw_regex_error(status, false, patterns, pattern_index);
    if (!found)
        return false;

    shared::regex_range_to_positions(subject, match, return_length);
    if (capture_groups) {
        for (std::size_t i = 0; i < captures.size(); ++i) {
            shared::regex_range_to_positions(
                subject, captures[i], return_length
            );
        }
    }
    return true;
}


CHARR_CXX_HELPER void locate_all(
    shared::RegexMatcher& matcher,
    const shared::StringView& subject,
    const void* subject_identity,
    bool capture_groups,
    bool return_length,
    const shared::RegexPatterns& patterns,
    std::size_t pattern_index,
    std::vector<shared::RegexRange>& matches,
    std::vector<std::vector<shared::RegexRange> >& captures
)
{
    UErrorCode status = U_ZERO_ERROR;
    if (capture_groups) {
        matcher.find_all_with_captures(
            subject, subject_identity, matches, captures, status
        );
    }
    else {
        matcher.find_all(subject, subject_identity, matches, status);
    }
    if (U_FAILURE(status))
        throw_regex_error(status, false, patterns, pattern_index);

    shared::regex_ranges_to_positions(
        subject, matches, return_length
    );
    if (capture_groups) {
        for (std::size_t i = 0; i < captures.size(); ++i) {
            shared::regex_ranges_to_positions(
                subject, captures[i], return_length
            );
        }
    }
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


CHARR_NEUTRAL_HELPER void set_no_match_captures(
    std::vector<std::vector<shared::RegexRange> >& columns,
    std::size_t count,
    std::size_t index,
    bool return_length
) noexcept
{
    const int value = return_length ? -1 : NA_INTEGER;
    for (std::size_t i = 0; i < count; ++i)
        columns[i][index] = shared::RegexRange{value, value};
}


CHARR_NEUTRAL_HELPER void store_captures(
    const std::vector<shared::RegexRange>& captures,
    std::vector<std::vector<shared::RegexRange> >& columns,
    std::size_t index,
    bool return_length
) noexcept
{
    for (std::size_t i = 0; i < captures.size(); ++i) {
        shared::RegexRange value = captures[i];
        if (value.start < 0 || value.end < 0) {
            const int missing = return_length ? -1 : NA_INTEGER;
            value = shared::RegexRange{missing, missing};
        }
        columns[i][index] = value;
    }
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


CHARR_R_HELPER SEXP ranges_matrix_r(
    const std::vector<shared::RegexRange>& ranges,
    bool subject_missing,
    bool omit_no_match,
    bool return_length
) noexcept
{
    if (subject_missing)
        return shared::filled_integer_matrix_r(1, 2);

    const R_len_t count = static_cast<R_len_t>(ranges.size());
    if (count <= 0) {
        return shared::filled_integer_matrix_r(
            omit_no_match ? 0 : 1, 2,
            return_length ? -1 : NA_INTEGER
        );
    }

    SEXP result = Rf_allocMatrix(INTSXP, count, 2);
    int* output = INTEGER(result);
    for (R_len_t i = 0; i < count; ++i) {
        const shared::RegexRange& range = ranges[
            static_cast<std::size_t>(i)
        ];
        if (range.start < 0 || range.end < 0) {
            const int missing = return_length ? -1 : NA_INTEGER;
            output[i] = missing;
            output[i+count] = missing;
        }
        else {
            output[i] = range.start;
            output[i+count] = range.end;
        }
    }
    return result;
}


CHARR_R_HELPER void fill_capture_matrix_r(
    SEXP output,
    const std::vector<shared::RegexRange>& values
) noexcept
{
    const R_len_t count = static_cast<R_len_t>(values.size());
    int* data = INTEGER(output);
    for (R_len_t i = 0; i < count; ++i) {
        const shared::RegexRange& value = values[
            static_cast<std::size_t>(i)
        ];
        data[i] = value.start;
        data[i+count] = value.end;
    }
}


CHARR_R_HELPER SEXP capture_names_r(
    const std::vector<std::string>& names
) noexcept
{
    if (!capture_names_present(names))
        return R_NilValue;

    const R_len_t count = static_cast<R_len_t>(names.size());
    SEXP result = Rf_allocVector(STRSXP, count);
    for (R_len_t i = 0; i < count; ++i) {
        const std::string& name = names[static_cast<std::size_t>(i)];
        SET_STRING_ELT(
            result, i,
            Rf_mkCharLenCE(
                name.size() == 0 ? "" : name.data(),
                static_cast<int>(name.size()), CE_UTF8
            )
        );
    }
    return result;
}


struct CHARR_OWNER_TYPE AllRow {
    bool pattern_unusable;
    bool subject_missing;
    int group_count;
    std::vector<shared::RegexRange> matches;
    std::vector<std::vector<shared::RegexRange> > captures;

    CHARR_CXX_HELPER AllRow() noexcept
        : pattern_unusable(false), subject_missing(false),
          group_count(0), matches(), captures() {}
};


class CHARR_OWNER_TYPE AllRows {
public:
    CHARR_CXX_HELPER AllRows() noexcept
        : rows_(nullptr) {}

    CHARR_CXX_HELPER ~AllRows() noexcept
    {
        delete[] rows_;
    }

    AllRows(const AllRows&) = delete;
    AllRows& operator=(const AllRows&) = delete;
    AllRows(AllRows&&) = delete;
    AllRows& operator=(AllRows&&) = delete;

    CHARR_CXX_HELPER void reset(std::size_t size)
    {
        AllRow* replacement = size == 0 ? nullptr : new AllRow[size];
        delete[] rows_;
        rows_ = replacement;
    }

    CHARR_NEUTRAL_HELPER AllRow& at(std::size_t index) noexcept
    {
        return rows_[index];
    }

    CHARR_NEUTRAL_HELPER const AllRow& at(
        std::size_t index
    ) const noexcept
    {
        return rows_[index];
    }

private:
    AllRow* rows_;
};


class CHARR_OWNER_TYPE CaptureNameRows {
public:
    CHARR_CXX_HELPER CaptureNameRows() noexcept
        : rows_(nullptr) {}

    CHARR_CXX_HELPER ~CaptureNameRows() noexcept
    {
        delete[] rows_;
    }

    CaptureNameRows(const CaptureNameRows&) = delete;
    CaptureNameRows& operator=(const CaptureNameRows&) = delete;
    CaptureNameRows(CaptureNameRows&&) = delete;
    CaptureNameRows& operator=(CaptureNameRows&&) = delete;

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


CHARR_R_HELPER void materialize_all_row_r(
    const AllRow& row,
    const std::vector<std::string>& capture_names,
    bool omit,
    bool capture,
    bool return_length,
    SEXP capture_symbol,
    shared::ProtHelper& protections,
    PROTECT_INDEX current_index,
    PROTECT_INDEX capture_result_index,
    PROTECT_INDEX capture_matrix_index,
    PROTECT_INDEX names_index,
    SEXP& current,
    SEXP& capture_result,
    SEXP& capture_matrix,
    SEXP& names,
    SEXP result,
    R_len_t output_index
) noexcept
{
    if (row.pattern_unusable) {
        current = protections.reprotect_slot(
            shared::filled_integer_matrix_r(1, 2), current_index
        );
        if (capture) {
            capture_result = protections.reprotect_slot(
                Rf_allocVector(VECSXP, 0), capture_result_index
            );
            Rf_setAttrib(current, capture_symbol, capture_result);
        }
        SET_VECTOR_ELT(result, output_index, current);
        return;
    }

    current = protections.reprotect_slot(
        ranges_matrix_r(
            row.matches, row.subject_missing, omit, return_length
        ),
        current_index
    );
    if (capture) {
        capture_result = protections.reprotect_slot(
            Rf_allocVector(VECSXP, row.group_count),
            capture_result_index
        );
        for (int j = 0; j < row.group_count; ++j) {
            capture_matrix = protections.reprotect_slot(
                ranges_matrix_r(
                    row.captures[static_cast<std::size_t>(j)],
                    row.subject_missing, omit, return_length
                ),
                capture_matrix_index
            );
            SET_VECTOR_ELT(capture_result, j, capture_matrix);
        }
        ci__locate_set_dimnames_list(capture_result, return_length);
        if (capture_names_present(capture_names)) {
            names = protections.reprotect_slot(
                capture_names_r(capture_names), names_index
            );
            Rf_setAttrib(capture_result, R_NamesSymbol, names);
        }
        Rf_setAttrib(current, capture_symbol, capture_result);
    }
    SET_VECTOR_ELT(result, output_index, current);
}


class AllBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER AllBody(
        shared::RegexOptions options,
        const std::vector<shared::StringView>& subjects,
        const shared::RegexPatterns& patterns,
        R_len_t subject_length,
        R_len_t pattern_length,
        R_len_t vectorize_length,
        bool capture,
        bool return_length,
        AllRows& rows,
        CaptureNameRows& capture_names,
        std::vector<int>& warning_slots,
        std::vector<int>& failures
    ) noexcept
        : options_(options), subjects_(subjects), patterns_(patterns),
          subject_length_(subject_length), pattern_length_(pattern_length),
          vectorize_length_(vectorize_length), capture_(capture),
          return_length_(return_length), rows_(rows),
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
    // What binding a pattern established, carried from the bind to every
    // range staged against it.
    struct StagedPattern {
        bool unusable;
        bool empty;
        int group_count;
    };

    CHARR_CXX_HELPER void run_unchecked(
        shared::WorkerContext& context
    )
    {
        shared::RegexMatcher matcher(options_);
        if (pattern_length_ == 1) {
            // One pattern covers the whole vector, so it is bound once for
            // this worker and every chunk it draws stages against that bind.
            const StagedPattern staged = bind_staged_pattern(
                0, context.worker, matcher
            );
            while (context.next_chunk()) {
                stage_range(
                    static_cast<R_len_t>(context.begin),
                    static_cast<R_len_t>(context.end),
                    staged, 0, context.worker, matcher
                );
            }
            return;
        }
        while (context.next_chunk()) {
            const R_len_t begin = static_cast<R_len_t>(context.begin);
            const R_len_t end = static_cast<R_len_t>(context.end);
            for (R_len_t lane = begin; lane < end; ++lane) {
                const StagedPattern staged = bind_staged_pattern(
                    static_cast<std::size_t>(lane),
                    static_cast<unsigned>(lane), matcher
                );
                stage_range(
                    lane, lane+1, staged,
                    static_cast<std::size_t>(lane), context.worker, matcher
                );
            }
        }
    }

    CHARR_CXX_HELPER StagedPattern bind_staged_pattern(
        std::size_t pattern_index,
        unsigned metadata_index,
        shared::RegexMatcher& matcher
    )
    {
        const shared::RegexInput pattern = patterns_.get(pattern_index);
        StagedPattern staged;
        staged.empty = !pattern.missing && pattern.length <= 0;
        staged.unusable = pattern.missing || staged.empty;
        staged.group_count = 0;

        if (!staged.unusable) {
            bind_pattern(matcher, pattern, patterns_, pattern_index);
            staged.group_count = matcher.group_count();
            if (capture_) {
                read_capture_names(
                    matcher, patterns_, pattern_index,
                    capture_names_.at(metadata_index)
                );
            }
        }
        return staged;
    }

    CHARR_CXX_HELPER void stage_range(
        R_len_t begin,
        R_len_t end,
        const StagedPattern& staged,
        std::size_t pattern_index,
        unsigned warning_index,
        shared::RegexMatcher& matcher
    )
    {
        if (pattern_length_ == 1) {
            for (R_len_t i = begin; i < end; ++i) {
                stage_one(
                    i, staged.unusable, staged.empty,
                    staged.group_count, pattern_index, warning_index,
                    matcher
                );
            }
            return;
        }
        for (R_len_t i = begin; i < vectorize_length_;
                i += pattern_length_) {
            stage_one(
                i, staged.unusable, staged.empty,
                staged.group_count, pattern_index, warning_index, matcher
            );
        }
    }

    CHARR_CXX_HELPER void stage_one(
        R_len_t i,
        bool pattern_unusable,
        bool pattern_empty,
        int group_count,
        std::size_t pattern_index,
        unsigned warning_index,
        shared::RegexMatcher& matcher
    )
    {
        AllRow& row = rows_.at(static_cast<std::size_t>(i));
        row.pattern_unusable = pattern_unusable;
        row.subject_missing = false;
        row.group_count = group_count;
        if (pattern_unusable) {
            if (pattern_empty)
                ++warning_slots_[warning_index];
            return;
        }

        const std::size_t subject_index =
            static_cast<std::size_t>(i % subject_length_);
        const shared::StringView& subject = subjects_[subject_index];
        row.subject_missing = subject.is_na();
        if (row.subject_missing) {
            row.matches.clear();
            row.captures.clear();
            if (capture_) {
                row.captures.resize(
                    static_cast<std::size_t>(group_count)
                );
            }
            return;
        }
        locate_all(
            matcher, subject, &subjects_[subject_index],
            capture_, return_length_, patterns_, pattern_index,
            row.matches, row.captures
        );
    }

    shared::RegexOptions options_;
    const std::vector<shared::StringView>& subjects_;
    const shared::RegexPatterns& patterns_;
    R_len_t subject_length_;
    R_len_t pattern_length_;
    R_len_t vectorize_length_;
    bool capture_;
    bool return_length_;
    AllRows& rows_;
    CaptureNameRows& capture_names_;
    std::vector<int>& warning_slots_;
    std::vector<int>& failures_;
};


CHARR_R_HELPER void emit_warnings_r(
    bool recycling_warning,
    int empty_pattern_warnings
) noexcept
{
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);
    for (int i = 0; i < empty_pattern_warnings; ++i)
        Rf_warning(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
}


class Body final : public ParallelBody {
public:
    CHARR_CXX_HELPER Body(
        shared::RegexOptions options,
        const std::vector<shared::StringView>& subjects,
        const shared::RegexPatterns& patterns,
        R_len_t subject_length,
        R_len_t pattern_length,
        R_len_t vectorize_length,
        bool capture,
        bool return_length,
        int* output,
        std::vector<std::vector<shared::RegexRange> >& capture_columns,
        std::vector<std::string>& capture_names,
        int& serial_empty_pattern_warnings,
        std::vector<int>& worker_empty_pattern_warnings,
        std::vector<int>& worker_failures
    ) noexcept
        : options_(options), subjects_(subjects), patterns_(patterns),
          subject_length_(subject_length), pattern_length_(pattern_length),
          vectorize_length_(vectorize_length), capture_(capture),
          return_length_(return_length), output_(output),
          capture_columns_(capture_columns), capture_names_(capture_names),
          serial_empty_pattern_warnings_(serial_empty_pattern_warnings),
          worker_empty_pattern_warnings_(worker_empty_pattern_warnings),
          worker_failures_(worker_failures)
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
            if (context.workers > 1) {
                worker_failures_[
                    static_cast<std::size_t>(context.worker)
                ] = 1;
            }
            throw;
        }
    }

private:
    CHARR_CXX_HELPER void run_unchecked(
        shared::WorkerContext& context
    )
    {
        shared::RegexMatcher matcher(options_);
        std::vector<shared::RegexRange> captures;
        int& empty_pattern_warnings = context.workers == 1
            ? serial_empty_pattern_warnings_
            : worker_empty_pattern_warnings_[
                static_cast<std::size_t>(context.worker)
            ];

        if (pattern_length_ == 1) {
            const std::size_t pattern_index = 0;
            const shared::RegexInput current_pattern =
                patterns_.get(pattern_index);
            const bool pattern_empty =
                !current_pattern.missing && current_pattern.length <= 0;
            const bool pattern_unusable =
                current_pattern.missing || pattern_empty;

            if (!pattern_unusable) {
                bind_pattern(
                    matcher, current_pattern, patterns_, pattern_index
                );
                if (capture_) {
                    ensure_capture_columns(
                        capture_columns_,
                        static_cast<std::size_t>(matcher.group_count()),
                        static_cast<std::size_t>(vectorize_length_)
                    );
                    read_capture_names(
                        matcher, patterns_, pattern_index, capture_names_
                    );
                }
            }

            while (context.next_chunk()) {
                const R_len_t begin = static_cast<R_len_t>(context.begin);
                const R_len_t end = static_cast<R_len_t>(context.end);
                for (R_len_t i = begin; i < end; ++i) {
                    if (pattern_unusable) {
                        if (pattern_empty)
                            ++empty_pattern_warnings;
                        continue;
                    }

                    const shared::StringView& subject = subjects_[
                        static_cast<std::size_t>(i % subject_length_)
                    ];
                    if (subject.is_na())
                        continue;

                    shared::RegexRange match{0, 0};
                    const bool found = locate_first(
                        matcher, subject, &subject,
                        capture_, return_length_, patterns_, pattern_index,
                        match, captures
                    );
                    const std::size_t group_count =
                        static_cast<std::size_t>(matcher.group_count());
                    if (!found) {
                        if (return_length_) {
                            output_[i] = -1;
                            output_[i+vectorize_length_] = -1;
                        }
                        if (capture_) {
                            set_no_match_captures(
                                capture_columns_, group_count,
                                static_cast<std::size_t>(i), return_length_
                            );
                        }
                        continue;
                    }

                    output_[i] = match.start;
                    output_[i+vectorize_length_] = match.end;
                    if (capture_) {
                        store_captures(
                            captures, capture_columns_,
                            static_cast<std::size_t>(i), return_length_
                        );
                    }
                }
            }
            return;
        }

        while (context.next_chunk()) {
            const R_len_t begin = static_cast<R_len_t>(context.begin);
            const R_len_t end = static_cast<R_len_t>(context.end);
            for (R_len_t lane = begin; lane < end; ++lane) {
                const std::size_t pattern_index =
                    static_cast<std::size_t>(lane);
                const shared::RegexInput current_pattern =
                    patterns_.get(pattern_index);
                const bool pattern_empty =
                    !current_pattern.missing && current_pattern.length <= 0;
                const bool pattern_unusable =
                    current_pattern.missing || pattern_empty;

                if (!pattern_unusable) {
                    bind_pattern(
                        matcher, current_pattern, patterns_, pattern_index
                    );
                    if (capture_) {
                        ensure_capture_columns(
                            capture_columns_,
                            static_cast<std::size_t>(matcher.group_count()),
                            static_cast<std::size_t>(vectorize_length_)
                        );
                    }
                }

                for (R_len_t i = lane; i < vectorize_length_;
                        i += pattern_length_) {
                    if (pattern_unusable) {
                        if (pattern_empty)
                            ++empty_pattern_warnings;
                        continue;
                    }

                    const shared::StringView& subject = subjects_[
                        static_cast<std::size_t>(i % subject_length_)
                    ];
                    if (subject.is_na())
                        continue;

                    shared::RegexRange match{0, 0};
                    const bool found = locate_first(
                        matcher, subject, &subject,
                        capture_, return_length_, patterns_, pattern_index,
                        match, captures
                    );
                    const std::size_t group_count =
                        static_cast<std::size_t>(matcher.group_count());
                    if (!found) {
                        if (return_length_) {
                            output_[i] = -1;
                            output_[i+vectorize_length_] = -1;
                        }
                        if (capture_) {
                            set_no_match_captures(
                                capture_columns_, group_count,
                                static_cast<std::size_t>(i), return_length_
                            );
                        }
                        continue;
                    }

                    output_[i] = match.start;
                    output_[i+vectorize_length_] = match.end;
                    if (capture_) {
                        store_captures(
                            captures, capture_columns_,
                            static_cast<std::size_t>(i), return_length_
                        );
                    }
                }
            }
        }
    }

    shared::RegexOptions options_;
    const std::vector<shared::StringView>& subjects_;
    const shared::RegexPatterns& patterns_;
    R_len_t subject_length_;
    R_len_t pattern_length_;
    R_len_t vectorize_length_;
    bool capture_;
    bool return_length_;
    int* output_;
    std::vector<std::vector<shared::RegexRange> >& capture_columns_;
    std::vector<std::string>& capture_names_;
    int& serial_empty_pattern_warnings_;
    std::vector<int>& worker_empty_pattern_warnings_;
    std::vector<int>& worker_failures_;
};

} // namespace search_regex_locate

using namespace search_regex_locate;


/** Locate the first regular-expression match in each string. */
CHARR_ENTRYPOINT SEXP ci_locate_first_regex(
    SEXP str,
    SEXP pattern,
    SEXP opts_regex,
    SEXP capture_groups,
    SEXP get_length
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool capture = ci__prepare_arg_logical_1_notNA_r(
        capture_groups, "capture_groups"
    );
    const bool return_length = ci__prepare_arg_logical_1_notNA_r(
        get_length, "get_length"
    );
    const shared::RegexOptions options = regex::prepare_options(opts_regex);
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    pattern = entry_protections.protect_one(
        ci__prepare_arg_string_r(pattern, "pattern")
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
    const SEXP capture_symbol = capture
        ? Rf_install("capture_groups")
        : R_NilValue;


    int empty_pattern_warnings = 0;

    try {
        charport::Reader subject_reader;
        charport::Reader pattern_reader;
        charport::StrViews subject_views;
        charport::StrViews pattern_views;
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        std::vector<shared::StringView> subjects;
        shared::RegexPatterns patterns;
        std::vector<std::vector<shared::RegexRange> > capture_columns;
        std::vector<std::string> capture_names;
        std::vector<int> worker_empty_pattern_warnings;
        std::vector<int> worker_failures;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                if (vectorize_length > 0) {
                    pattern_reader.reset(pattern);
                    if (pattern_reader.size() != pattern_length) {
                        throw std::runtime_error(
                            "Reader length changed during regex locate"
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

                    subject_reader.reset(str);
                    if (subject_reader.size() != subject_length) {
                        throw std::runtime_error(
                            "Reader length changed during regex locate"
                        );
                    }
                    subject_views.resize(subject_length);
                    subject_reader.views(
                        0, subject_length,
                        subject_views.ptrs(), subject_views.lengths(),
                        subject_views.encodings()
                    );
                    normalize_inputs(
                        subject_views, subject_converter,
                        subject_storage, subjects
                    );
                }

                result = entry_protections.reprotect_one(
                    Rf_allocMatrix(INTSXP, vectorize_length, 2),
                    result_index
                );
                int* output = INTEGER(result);
                for (R_len_t i = 0; i < vectorize_length; ++i) {
                    output[i] = NA_INTEGER;
                    output[i+vectorize_length] = NA_INTEGER;
                }

                if (vectorize_length > 0) {
                    const R_xlen_t tasks = pattern_length == 1
                        ? vectorize_length
                        : pattern_length;
                    const shared::ParallelPlan plan = shared::parallel_plan(
                        !capture, tasks
                    );
                    if (plan.workers > 1) {
                        worker_empty_pattern_warnings.resize(plan.workers);
                        worker_failures.resize(plan.workers);
                    }
                    Body body(
                        options, subjects, patterns,
                        subject_length, pattern_length, vectorize_length,
                        capture, return_length, output,
                        capture_columns, capture_names,
                        empty_pattern_warnings,
                        worker_empty_pattern_warnings,
                        worker_failures
                    );
                    try {
                        shared::run_parallel(plan, tasks, body);
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
                            i < worker_empty_pattern_warnings.size(); ++i) {
                        empty_pattern_warnings +=
                            worker_empty_pattern_warnings[i];
                    }
                }

                if (capture) {
                    SEXP capture_result = R_NilValue;
                    PROTECT_INDEX capture_result_index;
                    callback_protections.protect_with_index(
                        capture_result, &capture_result_index
                    );
                    SEXP capture_matrix = R_NilValue;
                    PROTECT_INDEX capture_matrix_index;
                    callback_protections.protect_with_index(
                        capture_matrix, &capture_matrix_index
                    );
                    SEXP names = R_NilValue;
                    PROTECT_INDEX names_index;
                    callback_protections.protect_with_index(names, &names_index);

                    const R_len_t group_count = static_cast<R_len_t>(
                        capture_columns.size()
                    );
                    capture_result = callback_protections.reprotect_slot(
                        Rf_allocVector(VECSXP, group_count),
                        capture_result_index
                    );
                    for (R_len_t i = 0; i < group_count; ++i) {
                        capture_matrix = callback_protections.reprotect_slot(
                            Rf_allocMatrix(
                                INTSXP, vectorize_length, 2
                            ),
                            capture_matrix_index
                        );
                        fill_capture_matrix_r(
                            capture_matrix,
                            capture_columns[static_cast<std::size_t>(i)]
                        );
                        SET_VECTOR_ELT(capture_result, i, capture_matrix);
                    }
                    ci__locate_set_dimnames_list(
                        capture_result, return_length
                    );
                    if (capture_names_present(capture_names)) {
                        names = callback_protections.reprotect_slot(
                            capture_names_r(capture_names), names_index
                        );
                        Rf_setAttrib(
                            capture_result, R_NamesSymbol, names
                        );
                    }
                    Rf_setAttrib(
                        result, capture_symbol, capture_result
                    );
                }

                ci__locate_set_dimnames_matrix(result, return_length);
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_warnings_r(recycling_warning, empty_pattern_warnings);
    );
}


/** Locate all regular-expression matches in each string. */
CHARR_ENTRYPOINT SEXP ci_locate_all_regex(
    SEXP str,
    SEXP pattern,
    SEXP omit_no_match,
    SEXP opts_regex,
    SEXP capture_groups,
    SEXP get_length
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool omit = ci__prepare_arg_logical_1_notNA_r(
        omit_no_match, "omit_no_match"
    );
    const bool capture = ci__prepare_arg_logical_1_notNA_r(
        capture_groups, "capture_groups"
    );
    const bool return_length = ci__prepare_arg_logical_1_notNA_r(
        get_length, "get_length"
    );
    const shared::RegexOptions options = regex::prepare_options(opts_regex);
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    pattern = entry_protections.protect_one(
        ci__prepare_arg_string_r(pattern, "pattern")
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
    const SEXP capture_symbol = capture
        ? Rf_install("capture_groups")
        : R_NilValue;


    int empty_pattern_warnings = 0;

    try {
        charport::Reader subject_reader;
        charport::Reader pattern_reader;
        charport::StrViews subject_views;
        charport::StrViews pattern_views;
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        std::vector<shared::StringView> subjects;
        shared::RegexPatterns patterns;
        shared::RegexMatcher matcher(options);
        std::vector<shared::RegexRange> matches;
        std::vector<std::vector<shared::RegexRange> > captures;
        std::vector<std::string> capture_names;
        AllRows staged_rows;
        CaptureNameRows staged_capture_names;
        std::vector<int> worker_empty_pattern_warnings;
        std::vector<int> worker_failures;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                if (vectorize_length > 0) {
                    pattern_reader.reset(pattern);
                    if (pattern_reader.size() != pattern_length) {
                        throw std::runtime_error(
                            "Reader length changed during regex locate"
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

                    subject_reader.reset(str);
                    if (subject_reader.size() != subject_length) {
                        throw std::runtime_error(
                            "Reader length changed during regex locate"
                        );
                    }
                    subject_views.resize(subject_length);
                    subject_reader.views(
                        0, subject_length,
                        subject_views.ptrs(), subject_views.lengths(),
                        subject_views.encodings()
                    );
                    normalize_inputs(
                        subject_views, subject_converter,
                        subject_storage, subjects
                    );
                }

                result = entry_protections.reprotect_one(
                    Rf_allocVector(VECSXP, vectorize_length),
                    result_index
                );
                SEXP current = R_NilValue;
                PROTECT_INDEX current_index;
                callback_protections.protect_with_index(current, &current_index);

                SEXP capture_result = R_NilValue;
                PROTECT_INDEX capture_result_index;
                SEXP capture_matrix = R_NilValue;
                PROTECT_INDEX capture_matrix_index;
                SEXP names = R_NilValue;
                PROTECT_INDEX names_index;
                callback_protections.protect_with_index(
                    capture_result, &capture_result_index
                );
                callback_protections.protect_with_index(
                    capture_matrix, &capture_matrix_index
                );
                callback_protections.protect_with_index(names, &names_index);

                const R_len_t tasks = vectorize_length == 0
                    ? 0 : pattern_length == 1
                        ? vectorize_length : pattern_length;
                const shared::ParallelPlan parallel_plan =
                    shared::parallel_plan(true, tasks);
                if (parallel_plan.workers == 1) {
                for (R_len_t lane = 0;
                        lane < (vectorize_length > 0
                            ? pattern_length : 0);
                        ++lane) {
                    const std::size_t pattern_index =
                        static_cast<std::size_t>(lane);
                    const shared::RegexInput current_pattern =
                        patterns.get(pattern_index);
                    const bool pattern_empty =
                        !current_pattern.missing &&
                        current_pattern.length <= 0;
                    const bool pattern_unusable =
                        current_pattern.missing || pattern_empty;
                    int group_count = 0;

                    if (!pattern_unusable) {
                        bind_pattern(
                            matcher, current_pattern, patterns,
                            pattern_index
                        );
                        group_count = matcher.group_count();
                        if (capture) {
                            read_capture_names(
                                matcher, patterns, pattern_index,
                                capture_names
                            );
                        }
                    }

                    for (R_len_t i = lane; i < vectorize_length;
                            i += pattern_length) {
                        if (pattern_unusable) {
                            if (pattern_empty)
                                ++empty_pattern_warnings;
                            current = callback_protections.reprotect_slot(
                                shared::filled_integer_matrix_r(1, 2),
                                current_index
                            );
                            if (capture) {
                                capture_result =
                                    callback_protections.reprotect_slot(
                                        Rf_allocVector(VECSXP, 0),
                                        capture_result_index
                                    );
                                Rf_setAttrib(
                                    current, capture_symbol,
                                    capture_result
                                );
                            }
                            SET_VECTOR_ELT(result, i, current);
                            continue;
                        }

                        const shared::StringView& subject = subjects[
                            static_cast<std::size_t>(i % subject_length)
                        ];
                        const bool subject_missing = subject.is_na();
                        if (subject_missing) {
                            matches.clear();
                            captures.clear();
                            if (capture) {
                                captures.resize(
                                    static_cast<std::size_t>(group_count)
                                );
                            }
                        }
                        else {
                            locate_all(
                                matcher, subject, &subject,
                                capture, return_length,
                                patterns, pattern_index,
                                matches, captures
                            );
                        }

                        current = callback_protections.reprotect_slot(
                            ranges_matrix_r(
                                matches, subject_missing, omit,
                                return_length
                            ),
                            current_index
                        );

                        if (capture) {
                            capture_result = callback_protections.reprotect_slot(
                                Rf_allocVector(VECSXP, group_count),
                                capture_result_index
                            );
                            for (int j = 0; j < group_count; ++j) {
                                capture_matrix =
                                    callback_protections.reprotect_slot(
                                        ranges_matrix_r(
                                            captures[
                                                static_cast<std::size_t>(j)
                                            ],
                                            subject_missing, omit,
                                            return_length
                                        ),
                                        capture_matrix_index
                                    );
                                SET_VECTOR_ELT(
                                    capture_result, j, capture_matrix
                                );
                            }
                            ci__locate_set_dimnames_list(
                                capture_result, return_length
                            );
                            if (capture_names_present(capture_names)) {
                                names = callback_protections.reprotect_slot(
                                    capture_names_r(capture_names),
                                    names_index
                                );
                                Rf_setAttrib(
                                    capture_result, R_NamesSymbol,
                                    names
                                );
                            }
                            Rf_setAttrib(
                                current, capture_symbol, capture_result
                            );
                        }
                        SET_VECTOR_ELT(result, i, current);
                    }
                }
                }
                else {
                    staged_rows.reset(
                        static_cast<std::size_t>(vectorize_length)
                    );
                    const std::size_t metadata_count = pattern_length == 1
                        ? static_cast<std::size_t>(parallel_plan.workers)
                        : static_cast<std::size_t>(pattern_length);
                    staged_capture_names.reset(metadata_count);
                    worker_empty_pattern_warnings.assign(
                        static_cast<std::size_t>(parallel_plan.workers), 0
                    );
                    worker_failures.assign(
                        static_cast<std::size_t>(parallel_plan.workers), 0
                    );
                    AllBody body(
                        options, subjects, patterns,
                        subject_length, pattern_length, vectorize_length,
                        capture, return_length,
                        staged_rows, staged_capture_names,
                        worker_empty_pattern_warnings, worker_failures
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
                            i < worker_empty_pattern_warnings.size(); ++i) {
                        empty_pattern_warnings +=
                            worker_empty_pattern_warnings[i];
                    }

                    for (R_len_t lane = 0;
                            lane < pattern_length; ++lane) {
                        const std::size_t metadata_index =
                            pattern_length == 1
                                ? 0
                                : static_cast<std::size_t>(lane);
                        for (R_len_t i = lane;
                                i < vectorize_length;
                                i += pattern_length) {
                            materialize_all_row_r(
                                staged_rows.at(
                                    static_cast<std::size_t>(i)
                                ),
                                staged_capture_names.at(metadata_index),
                                omit, capture, return_length,
                                capture_symbol, callback_protections,
                                current_index, capture_result_index,
                                capture_matrix_index, names_index,
                                current, capture_result,
                                capture_matrix, names, result, i
                            );
                        }
                    }
                }

                ci__locate_set_dimnames_list(result, return_length);
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_warnings_r(recycling_warning, empty_pattern_warnings);
    );
}

} } // namespace charr::altrep_backend
