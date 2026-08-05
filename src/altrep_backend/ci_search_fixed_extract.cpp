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
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <charport.h>

#include <cstddef>
#include <cstring>
#include <exception>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>


namespace charr { namespace altrep_backend {

namespace search_fixed_extract {

CHARR_NEUTRAL_HELPER R_len_t recycling_length(
    R_len_t subject_length,
    R_len_t pattern_length,
    bool& warning
) noexcept
{
    warning = false;
    if (subject_length <= 0 || pattern_length <= 0)
        return 0;

    const R_len_t output = subject_length > pattern_length
        ? subject_length : pattern_length;
    warning = output % subject_length != 0 ||
        output % pattern_length != 0;
    return output;
}


CHARR_CXX_HELPER void normalize_views(
    const charport::StrViews& source,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    std::vector<shared::StringView>& output
)
{
    output.clear();
    output.reserve(static_cast<std::size_t>(source.size()));
    for (R_xlen_t i = 0; i < source.size(); ++i) {
        const shared::StringView value = io::as_shared_view(source[i]);
        if (value.enc == shared::StringEncoding::bytes)
            throw StriException(MSG__BYTESENC);
        output.push_back(shared::normalize_utf8(
            value, converter, storage
        ));
    }
}


CHARR_CXX_HELPER R_len_t count_empty_patterns(
    const std::vector<shared::StringView>& patterns
)
{
    R_len_t result = 0;
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        if (!patterns[i].is_na() && patterns[i].len <= 0)
            ++result;
    }
    return result;
}


CHARR_CXX_HELPER void set_missing_store(
    io::OutputStore& output
)
{
    output = io::OutputStore::scalar(
        nullptr, 0, CETYPE_EXT_NA
    );
}


CHARR_CXX_HELPER void set_empty_store(
    io::OutputStore& output
)
{
    output = io::OutputStore(0, 0);
}


CHARR_CXX_HELPER void set_repeated_store(
    const shared::StringView& pattern,
    R_len_t count,
    io::OutputStore& output
)
{
    output = io::OutputStore(
        static_cast<std::size_t>(count),
        static_cast<std::size_t>(pattern.len)
    );
    if (count <= 0)
        return;

    char* payload = output.slices.front_data();
    if (pattern.len > 0) {
        std::memcpy(
            payload, pattern.ptr,
            static_cast<std::size_t>(pattern.len)
        );
    }
    const charport::StrView value = io::as_charport_view(pattern);
    for (R_len_t i = 0; i < count; ++i) {
        output.records.set(
            static_cast<std::size_t>(i),
            pattern.len == 0 ? "" : payload,
            pattern.len, value.enc
        );
    }
}


CHARR_CXX_HELPER void set_matched_store(
    const shared::FixedExtractPlan& plan,
    const shared::FixedExtractRow& row,
    const std::vector<shared::StringView>& patterns,
    io::OutputBuilder& builder,
    io::OutputStore& output
)
{
    if (row.forced_na) {
        set_missing_store(output);
        return;
    }
    if (row.count <= 0) {
        set_empty_store(output);
        return;
    }
    if (plan.matches_are_patterns) {
        set_repeated_store(
            patterns[row.begin], row.count, output
        );
        return;
    }
    if (row.count == 1) {
        const shared::StringView& match = plan.matches[row.begin];
        const charport::StrView value = io::as_charport_view(match);
        output = io::OutputStore::scalar(
            match.len == 0 ? "" : match.ptr,
            static_cast<std::size_t>(match.len),
            value.enc
        );
        return;
    }

    builder.reset(row.count);
    for (R_len_t i = 0; i < row.count; ++i) {
        builder.set_validated(
            i,
            io::as_charport_view(plan.matches[
                row.begin+static_cast<std::size_t>(i)
            ])
        );
    }
    output = builder.release_store();
}


class CHARR_OWNER_TYPE FixedExtractPlans {
public:
    CHARR_CXX_HELPER FixedExtractPlans() noexcept
        : plans_(nullptr) {}

    CHARR_CXX_HELPER ~FixedExtractPlans() noexcept
    {
        delete[] plans_;
    }

    FixedExtractPlans(const FixedExtractPlans&) = delete;
    FixedExtractPlans& operator=(const FixedExtractPlans&) = delete;
    FixedExtractPlans(FixedExtractPlans&&) = delete;
    FixedExtractPlans& operator=(FixedExtractPlans&&) = delete;

    CHARR_CXX_HELPER void reset(std::size_t size)
    {
        shared::FixedExtractPlan* replacement = size == 0
            ? nullptr
            : new shared::FixedExtractPlan[size];
        delete[] plans_;
        plans_ = replacement;
    }

    CHARR_NEUTRAL_HELPER shared::FixedExtractPlan* data() noexcept
    {
        return plans_;
    }

private:
    shared::FixedExtractPlan* plans_;
};


/*
 * One run of output rows a worker planned from a single claimed chunk. A
 * worker draws chunks from a shared cursor rather than holding one contiguous
 * slice, so the runs it accumulates are ascending but not adjacent, and each
 * has to carry the output index its rows belong at.
 */
struct PlanSegment {
    R_len_t begin;
    int count;
};


class Body final : public ParallelBody {
public:
    CHARR_CXX_HELPER Body(
        const std::vector<shared::StringView>& subjects,
        const std::vector<shared::StringView>& patterns,
        shared::FixedSearchOptions options,
        bool omit_no_match,
        shared::FixedExtractPlan* plans,
        std::vector<std::vector<PlanSegment> >& segments
    ) noexcept
        : subjects_(subjects), patterns_(patterns), options_(options),
          omit_no_match_(omit_no_match), plans_(plans),
          segments_(segments)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        // The matcher, the range scratch, and the chunk plan the planner
        // refills on every claim are all built once per worker. The worker's
        // own plan accumulates the chunks it draws, because
        // plan_fixed_extract clears the plan it writes into.
        shared::FixedMatcher matcher;
        std::vector<shared::FixedRange> scratch;
        scratch.reserve(16);
        shared::FixedExtractPlan chunk;
        shared::FixedExtractPlan& plan = plans_[context.worker];
        std::vector<PlanSegment>& segments = segments_[context.worker];

        while (context.next_chunk()) {
            shared::plan_fixed_extract(
                subjects_, patterns_,
                static_cast<int>(context.begin),
                static_cast<int>(context.end),
                options_, omit_no_match_, matcher, scratch, chunk
            );
            append_chunk(chunk, plan);
            segments.push_back(PlanSegment{
                static_cast<R_len_t>(context.begin),
                static_cast<int>(context.end-context.begin)
            });
        }
    }

private:
    /*
     * Copy one chunk's rows onto the end of the worker's plan. A row's match
     * index is relative to the plan it was written into, so it shifts by what
     * the worker has already accumulated -- the same arithmetic merge_plans
     * applies once more when it concatenates the workers.
     */
    CHARR_CXX_HELPER static void append_chunk(
        const shared::FixedExtractPlan& chunk,
        shared::FixedExtractPlan& plan
    )
    {
        // Both appends grow the plan geometrically. Reserving the exact size
        // a chunk needs would reallocate on every claim, and a worker claims
        // many.
        const std::size_t match_offset = plan.matches.size();
        if (!chunk.matches_are_patterns) {
            if (chunk.matches.size() >
                    std::numeric_limits<std::size_t>::max()-match_offset) {
                throw std::length_error(
                    "fixed extraction result is too large"
                );
            }
            plan.matches.insert(
                plan.matches.end(),
                chunk.matches.begin(), chunk.matches.end()
            );
        }

        for (std::size_t i = 0; i < chunk.rows.size(); ++i) {
            shared::FixedExtractRow row = chunk.rows[i];
            if (!chunk.matches_are_patterns)
                row.begin += match_offset;
            plan.rows.push_back(row);
        }

        if (plan.max_columns < chunk.max_columns)
            plan.max_columns = chunk.max_columns;
        plan.matches_are_patterns = chunk.matches_are_patterns;
    }

    const std::vector<shared::StringView>& subjects_;
    const std::vector<shared::StringView>& patterns_;
    shared::FixedSearchOptions options_;
    bool omit_no_match_;
    shared::FixedExtractPlan* plans_;
    std::vector<std::vector<PlanSegment> >& segments_;
};


CHARR_CXX_HELPER void merge_plans(
    shared::FixedExtractPlan* sources,
    unsigned source_count,
    const std::vector<std::vector<PlanSegment> >& segments,
    std::size_t pattern_count,
    R_len_t output_length,
    bool matches_are_patterns,
    shared::FixedExtractPlan& output
)
{
    output.rows.clear();
    output.matches.clear();
    output.max_columns = 0;
    output.matches_are_patterns = matches_are_patterns;
    output.rows.resize(static_cast<std::size_t>(output_length));

    const std::size_t expected_rows =
        static_cast<std::size_t>(output_length);
    std::size_t merged_rows = 0;
    for (unsigned source_index = 0;
            source_index < source_count; ++source_index) {
        shared::FixedExtractPlan& source = sources[source_index];
        const std::vector<PlanSegment>& runs = segments[source_index];
        // A worker that never drew a chunk holds a plan nothing wrote to,
        // down to the mode flag, so there is nothing to check or copy.
        if (runs.empty())
            continue;
        if (source.matches_are_patterns != matches_are_patterns) {
            throw std::logic_error(
                "inconsistent fixed extraction plan mode"
            );
        }

        const std::size_t match_offset = output.matches.size();
        if (!matches_are_patterns) {
            if (source.matches.size() >
                    std::numeric_limits<std::size_t>::max()-match_offset) {
                throw std::length_error(
                    "fixed extraction result is too large"
                );
            }
            output.matches.reserve(
                match_offset+source.matches.size()
            );
            for (std::size_t i = 0;
                    i < source.matches.size(); ++i) {
                output.matches.push_back(source.matches[i]);
            }
        }

        // The worker's rows are its runs concatenated in claim order, so
        // walking the runs in that order consumes them exactly once and
        // lands each one at the output index it was planned for.
        std::size_t consumed = 0;
        for (std::size_t run = 0; run < runs.size(); ++run) {
            const PlanSegment& segment = runs[run];
            if (segment.begin < 0 || segment.count < 0) {
                throw std::logic_error(
                    "invalid fixed extraction chunk size"
                );
            }
            const std::size_t row_offset =
                static_cast<std::size_t>(segment.begin);
            const std::size_t row_count =
                static_cast<std::size_t>(segment.count);
            if (row_offset > expected_rows ||
                    row_count > expected_rows-row_offset) {
                throw std::length_error(
                    "fixed extraction row merge is too large"
                );
            }
            if (row_count > source.rows.size()-consumed) {
                throw std::logic_error(
                    "incomplete fixed extraction row merge"
                );
            }

            for (std::size_t i = 0; i < row_count; ++i) {
                shared::FixedExtractRow row = source.rows[consumed+i];
                if (row.count < 0) {
                    throw std::logic_error(
                        "invalid fixed extraction row count"
                    );
                }
                if (matches_are_patterns) {
                    if (row.begin >= pattern_count) {
                        throw std::logic_error(
                            "invalid fixed extraction pattern row"
                        );
                    }
                }
                else {
                    const std::size_t count =
                        static_cast<std::size_t>(row.count);
                    if (row.begin > source.matches.size() ||
                            count > source.matches.size()-row.begin) {
                        throw std::logic_error(
                            "invalid fixed extraction match row"
                        );
                    }
                    if (row.begin >
                            std::numeric_limits<std::size_t>::max()-
                                match_offset) {
                        throw std::length_error(
                            "fixed extraction result is too large"
                        );
                    }
                    row.begin += match_offset;
                }
                output.rows[row_offset+i] = row;
            }
            consumed += row_count;
            merged_rows += row_count;
        }
        if (consumed != source.rows.size()) {
            throw std::logic_error(
                "incomplete fixed extraction row merge"
            );
        }
        if (output.max_columns < source.max_columns)
            output.max_columns = source.max_columns;
    }

    if (merged_rows != expected_rows) {
        throw std::logic_error(
            "incomplete fixed extraction row merge"
        );
    }
}


CHARR_R_HELPER void emit_warnings_r(
    bool recycling_warning,
    R_len_t empty_pattern_warnings
) noexcept
{
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);
    for (R_len_t i = 0; i < empty_pattern_warnings; ++i)
        Rf_warning(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
}

} // namespace search_fixed_extract

using namespace search_fixed_extract;


/** Extract all fixed-pattern matches. */
CHARR_ENTRYPOINT SEXP ci_extract_all_fixed(
    SEXP str,
    SEXP pattern,
    SEXP simplify,
    SEXP omit_no_match,
    SEXP opts_fixed
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const shared::FixedSearchOptions options =
        fixed::prepare_options(opts_fixed, true);
    const bool omit_no_match_value =
        ci__prepare_arg_logical_1_notNA_r(
            omit_no_match, "omit_no_match"
        );
    simplify = entry_protections.protect_one(
        ci__prepare_arg_logical_1_r(
            simplify, "simplify"
        )
    );
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    pattern = entry_protections.protect_one(
        ci__prepare_arg_string_r(
            pattern, "pattern"
        )
    );
    const int simplify_value = LOGICAL_RO(simplify)[0];
    const bool simplifying =
        simplify_value == NA_LOGICAL || simplify_value != 0;

    SEXP temporary = R_NilValue;
    PROTECT_INDEX temporary_index;

    bool recycling_warning = false;
    R_len_t empty_pattern_warnings = 0;

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
        std::vector<shared::StringView> patterns;
        shared::FixedMatcher matcher;
        std::vector<shared::FixedRange> scratch;
        shared::FixedExtractPlan plan;
        FixedExtractPlans worker_plans;
        std::vector<std::vector<PlanSegment> > worker_segments;
        io::OutputBuilder child_builder(0);
        io::OutputBuilder matrix_builder(0);
        io::OutputStore child_store(0, 0);
        std::unordered_map<
            shared::FixedExtractRepeatKey,
            SEXP,
            shared::FixedExtractRepeatHash
        > repeated_children;

        scratch.reserve(16);
        repeated_children.reserve(16);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t subject_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                const R_len_t pattern_length = io::checked_r_len(
                    XLENGTH(pattern), "character vectors"
                );
                const R_len_t vectorize_length = recycling_length(
                    subject_length, pattern_length,
                    recycling_warning
                );

                if (vectorize_length > 0) {
                    subject_reader.reset(str);
                    if (subject_reader.size() != subject_length) {
                        throw std::runtime_error(
                            "Reader length changed during fixed extraction"
                        );
                    }
                    subject_views.resize(subject_length);
                    subject_reader.views(
                        0, subject_length,
                        subject_views.ptrs(), subject_views.lengths(),
                        subject_views.encodings()
                    );
                    normalize_views(
                        subject_views,
                        subject_converter, subject_storage,
                        subjects
                    );

                    pattern_reader.reset(pattern);
                    if (pattern_reader.size() != pattern_length) {
                        throw std::runtime_error(
                            "Reader length changed during fixed extraction"
                        );
                    }
                    pattern_views.resize(pattern_length);
                    pattern_reader.views(
                        0, pattern_length,
                        pattern_views.ptrs(), pattern_views.lengths(),
                        pattern_views.encodings()
                    );
                    normalize_views(
                        pattern_views,
                        pattern_converter, pattern_storage,
                        patterns
                    );
                    empty_pattern_warnings =
                        count_empty_patterns(patterns);
                }

                const shared::ParallelPlan parallel_plan =
                    shared::parallel_plan(true, vectorize_length);
                if (parallel_plan.workers == 1) {
                    shared::plan_fixed_extract(
                        subjects, patterns, vectorize_length,
                        options, omit_no_match_value,
                        matcher, scratch, plan
                    );
                }
                else {
                    worker_plans.reset(
                        static_cast<std::size_t>(parallel_plan.workers)
                    );
                    worker_segments.resize(
                        static_cast<std::size_t>(parallel_plan.workers)
                    );
                    Body body(
                        subjects, patterns, options,
                        omit_no_match_value, worker_plans.data(),
                        worker_segments
                    );
                    shared::run_parallel(
                        parallel_plan, vectorize_length, body
                    );
                    merge_plans(
                        worker_plans.data(), parallel_plan.workers,
                        worker_segments,
                        patterns.size(), vectorize_length,
                        !options.case_insensitive, plan
                    );
                }

                callback_protections.protect_with_index(
                    temporary, &temporary_index
                );
                if (!simplifying) {
                    result = entry_protections.reprotect_one(
                        Rf_allocVector(VECSXP, vectorize_length),
                        result_index
                    );
                    SEXP missing_child = R_NilValue;
                    SEXP empty_child = R_NilValue;

                    for (R_len_t i = 0;
                            i < vectorize_length; ++i) {
                        const shared::FixedExtractRow& row =
                            plan.rows[static_cast<std::size_t>(i)];
                        SEXP child = R_NilValue;

                        if (row.forced_na &&
                                missing_child != R_NilValue) {
                            child = missing_child;
                        }
                        else if (!row.forced_na && row.count == 0 &&
                                empty_child != R_NilValue) {
                            child = empty_child;
                        }
                        else if (plan.matches_are_patterns &&
                                !row.forced_na && row.count > 0) {
                            const shared::FixedExtractRepeatKey key{
                                patterns[row.begin], row.count
                            };
                            const auto found =
                                repeated_children.find(key);
                            if (found != repeated_children.end())
                                child = found->second;
                        }

                        if (child == R_NilValue) {
                            set_matched_store(
                                plan, row, patterns,
                                child_builder, child_store
                            );
                            temporary = callback_protections.reprotect_slot(
                                io::finalize(std::move(child_store)),
                                temporary_index
                            );
                            child = temporary;
                            SET_VECTOR_ELT(result, i, child);

                            if (row.forced_na) {
                                missing_child = child;
                            }
                            else if (row.count == 0) {
                                empty_child = child;
                            }
                            else if (plan.matches_are_patterns) {
                                const shared::FixedExtractRepeatKey key{
                                    patterns[row.begin], row.count
                                };
                                repeated_children.emplace(
                                    key, child
                                );
                            }
                        }
                        else {
                            temporary = callback_protections.reprotect_slot(
                                child, temporary_index
                            );
                            SET_VECTOR_ELT(result, i, temporary);
                        }
                    }
                }
                else {
                    const R_xlen_t rows = vectorize_length;
                    const R_xlen_t columns = plan.max_columns;
                    if (rows > 0 &&
                            columns > R_XLEN_T_MAX/rows) {
                        throw std::length_error(
                            "matrix length exceeds R's vector limit"
                        );
                    }

                    matrix_builder.reset(rows*columns);
                    for (R_xlen_t i = 0; i < rows; ++i) {
                        const shared::FixedExtractRow& row =
                            plan.rows[static_cast<std::size_t>(i)];
                        R_xlen_t j = 0;
                        if (row.forced_na) {
                            matrix_builder.set_na(i);
                            j = 1;
                        }
                        else if (plan.matches_are_patterns) {
                            const charport::StrView value =
                                io::as_charport_view(
                                    patterns[row.begin]
                                );
                            for (; j < row.count; ++j) {
                                matrix_builder.set_validated(
                                    i+j*rows, value
                                );
                            }
                        }
                        else {
                            for (; j < row.count; ++j) {
                                matrix_builder.set_validated(
                                    i+j*rows,
                                    io::as_charport_view(
                                        plan.matches[
                                            row.begin+
                                            static_cast<std::size_t>(j)
                                        ]
                                    )
                                );
                            }
                        }
                        for (; j < columns; ++j) {
                            if (simplify_value == NA_LOGICAL) {
                                matrix_builder.set_na(i+j*rows);
                            }
                            else {
                                matrix_builder.set(
                                    i+j*rows, "", 0,
                                    CETYPE_EXT_ASCII
                                );
                            }
                        }
                    }

                    result = entry_protections.reprotect_one(
                        matrix_builder.to_sexp(), result_index
                    );
                    temporary = callback_protections.reprotect_slot(
                        Rf_allocVector(INTSXP, 2),
                        temporary_index
                    );
                    INTEGER(temporary)[0] = vectorize_length;
                    INTEGER(temporary)[1] = plan.max_columns;
                    result = entry_protections.reprotect_one(
                        Rf_setAttrib(
                            result, R_DimSymbol, temporary
                        ),
                        result_index
                    );
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_warnings_r(
            recycling_warning, empty_pattern_warnings
        );
    );
}

} } // namespace charr::altrep_backend
