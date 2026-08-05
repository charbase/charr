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
#include <utility>
#include <vector>


namespace charr { namespace altrep_backend {

namespace search_regex_extract {

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


CHARR_CXX_HELPER void normalize_subjects(
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


CHARR_CXX_HELPER void normalize_patterns(
    const charport::StrViews& source,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    shared::RegexPatterns& output
)
{
    output.resize(static_cast<std::size_t>(source.size()));
    for (R_xlen_t i = 0; i < source.size(); ++i) {
        const shared::StringView value = io::as_shared_view(source[i]);
        if (value.enc == shared::StringEncoding::bytes)
            throw StriException(MSG__BYTESENC);
        output.set(
            static_cast<std::size_t>(i),
            shared::normalize_utf8_preserve_bom(
                value, converter, storage
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


CHARR_CXX_HELPER void set_scalar_missing(
    io::OutputStore& output
)
{
    output = io::OutputStore::scalar(
        nullptr, 0, CETYPE_EXT_NA
    );
}


CHARR_CXX_HELPER CHARR_ALWAYS_INLINE void build_store(
    const shared::StringView& subject,
    const std::vector<shared::RegexRange>& matches,
    bool subject_is_ascii,
    io::OutputBuilder& builder,
    io::OutputStore& output
)
{
    const R_xlen_t size = static_cast<R_xlen_t>(matches.size());
    const cetype_ext_t encoding = subject_is_ascii
        ? CETYPE_EXT_ASCII
        : CETYPE_EXT_UTF8;

    if (size == 1) {
        const shared::RegexRange& match = matches[0];
        const int length = match.end-match.start;
        output = io::OutputStore::scalar(
            length == 0 ? "" : subject.ptr+match.start,
            static_cast<std::size_t>(length), encoding
        );
        return;
    }

    builder.reset(size);
    for (R_xlen_t i = 0; i < size; ++i) {
        const shared::RegexRange& match = matches[
            static_cast<std::size_t>(i)
        ];
        const int length = match.end-match.start;
        builder.set_validated(i, make_strview(
            length == 0 ? "" : subject.ptr+match.start,
            length, encoding
        ));
    }
    output = builder.release_store();
}


CHARR_R_HELPER void emit_empty_pattern_warnings_r(int count) noexcept
{
    for (int i = 0; i < count; ++i)
        Rf_warning(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
}


class FirstBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER FirstBody(
        const std::vector<shared::StringView>& subjects,
        const shared::RegexPatterns& patterns,
        R_len_t vectorize_length, const shared::RegexOptions& options,
        io::ParallelOutputBuilder& output
    ) noexcept
        : subjects_(subjects), patterns_(patterns),
          vectorize_length_(vectorize_length), options_(options),
          output_(output)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        const R_len_t subject_length =
            static_cast<R_len_t>(subjects_.size());
        const R_len_t pattern_length =
            static_cast<R_len_t>(patterns_.size());
        shared::RegexMatcher matcher(options_);
        if (pattern_length == 1) {
            // The one pattern is bound on first use and stays bound for
            // every chunk this worker draws.
            const shared::RegexInput pattern = patterns_.get(0);
            bool pattern_bound = false;
            while (context.next_chunk()) {
                for (R_xlen_t task = context.begin;
                        task < context.end; ++task) {
                    const R_len_t i = static_cast<R_len_t>(task);
                    extract_one(
                        i, i, subjects_[static_cast<std::size_t>(i)],
                        pattern, 0, pattern_bound, matcher, output_,
                        context.worker
                    );
                }
            }
            return;
        }

        while (context.next_chunk()) {
            for (R_xlen_t task = context.begin;
                    task < context.end; ++task) {
                const R_len_t lane = static_cast<R_len_t>(task);
                const std::size_t pattern_index =
                    static_cast<std::size_t>(lane);
                const shared::RegexInput pattern =
                    patterns_.get(pattern_index);
                bool pattern_bound = false;
                R_len_t i = lane;
                for (;;) {
                    const std::size_t subject_index =
                        static_cast<std::size_t>(i % subject_length);
                    extract_one(
                        i, static_cast<R_len_t>(subject_index),
                        subjects_[subject_index], pattern, pattern_index,
                        pattern_bound, matcher, output_, context.worker
                    );
                    if (pattern_length >= vectorize_length_-i)
                        break;
                    i += pattern_length;
                }
            }
        }
    }

private:
    CHARR_CXX_HELPER void extract_one(
        R_len_t output_index, R_len_t subject_index,
        const shared::StringView& subject,
        const shared::RegexInput& pattern, std::size_t pattern_index,
        bool& pattern_bound, shared::RegexMatcher& matcher,
        io::ParallelOutputBuilder& output, unsigned worker
    ) const
    {
        if (subject.is_na() || pattern.missing || pattern.length <= 0) {
            output.set_na(worker, output_index);
            return;
        }
        if (!pattern_bound) {
            bind_pattern(matcher, pattern, patterns_, pattern_index);
            pattern_bound = true;
        }
        shared::RegexRange match{0, 0};
        UErrorCode status = U_ZERO_ERROR;
        const bool found = matcher.find_first(
            subject, &subjects_[static_cast<std::size_t>(subject_index)],
            match, status
        );
        if (U_FAILURE(status))
            throw_regex_error(status, false, patterns_, pattern_index);
        if (!found) {
            output.set_na(worker, output_index);
            return;
        }
        const int length = match.end-match.start;
        output.set_validated(worker, output_index, make_strview(
            length == 0 ? "" : subject.ptr+match.start,
            length, matcher.subject_is_ascii()
                ? CETYPE_EXT_ASCII : CETYPE_EXT_UTF8
        ));
    }

    const std::vector<shared::StringView>& subjects_;
    const shared::RegexPatterns& patterns_;
    R_len_t vectorize_length_;
    shared::RegexOptions options_;
    io::ParallelOutputBuilder& output_;
};


class AllBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER AllBody(
        const std::vector<shared::StringView>& subjects,
        const shared::RegexPatterns& patterns,
        R_len_t vectorize_length,
        const shared::RegexOptions& options,
        bool omit_no_match,
        bool simplifying,
        std::vector<io::OutputStore>& stores,
        std::vector<R_len_t>& max_columns
    ) noexcept
        : subjects_(subjects), patterns_(patterns),
          vectorize_length_(vectorize_length), options_(options),
          omit_no_match_(omit_no_match), simplifying_(simplifying),
          stores_(stores), max_columns_(max_columns)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        const R_len_t subject_length =
            static_cast<R_len_t>(subjects_.size());
        const R_len_t pattern_length =
            static_cast<R_len_t>(patterns_.size());
        shared::RegexMatcher matcher(options_);
        std::vector<shared::RegexRange> matches;
        io::OutputBuilder builder(0);
        matches.reserve(8);
        // Runs over every chunk this worker draws, so the slot it reports
        // still holds the widest row the worker saw.
        R_len_t local_max_columns = 0;

        if (pattern_length == 1) {
            // The one pattern is bound on first use and stays bound for
            // every chunk this worker draws.
            const shared::RegexInput pattern = patterns_.get(0);
            bool pattern_bound = false;
            while (context.next_chunk()) {
                const R_len_t begin = static_cast<R_len_t>(context.begin);
                const R_len_t end = static_cast<R_len_t>(context.end);
                for (R_len_t i = begin; i < end; ++i) {
                    extract_one(
                        i, static_cast<std::size_t>(i), pattern, 0,
                        pattern_bound, matcher, matches, builder,
                        local_max_columns
                    );
                }
            }
        }
        else {
            while (context.next_chunk()) {
                const R_len_t begin = static_cast<R_len_t>(context.begin);
                const R_len_t end = static_cast<R_len_t>(context.end);
                for (R_len_t lane = begin; lane < end; ++lane) {
                    const std::size_t pattern_index =
                        static_cast<std::size_t>(lane);
                    const shared::RegexInput pattern =
                        patterns_.get(pattern_index);
                    bool pattern_bound = false;
                    R_len_t i = lane;
                    for (;;) {
                        extract_one(
                            i,
                            static_cast<std::size_t>(i % subject_length),
                            pattern, pattern_index, pattern_bound,
                            matcher, matches, builder, local_max_columns
                        );
                        if (pattern_length >= vectorize_length_-i)
                            break;
                        i += pattern_length;
                    }
                }
            }
        }

        if (simplifying_)
            max_columns_[context.worker] = local_max_columns;
    }

private:
    CHARR_CXX_HELPER void extract_one(
        R_len_t output_index,
        std::size_t subject_index,
        const shared::RegexInput& pattern,
        std::size_t pattern_index,
        bool& pattern_bound,
        shared::RegexMatcher& matcher,
        std::vector<shared::RegexRange>& matches,
        io::OutputBuilder& builder,
        R_len_t& max_columns
    )
    {
        io::OutputStore& output = stores_[
            static_cast<std::size_t>(output_index)
        ];
        const shared::StringView& subject = subjects_[subject_index];
        const bool pattern_unusable =
            pattern.missing || pattern.length <= 0;

        if (subject.is_na() || pattern_unusable) {
            set_scalar_missing(output);
        }
        else {
            if (!pattern_bound) {
                bind_pattern(
                    matcher, pattern, patterns_, pattern_index
                );
                pattern_bound = true;
            }

            UErrorCode status = U_ZERO_ERROR;
            matcher.find_all(
                subject, &subjects_[subject_index], matches, status
            );
            if (U_FAILURE(status)) {
                throw_regex_error(
                    status, false, patterns_, pattern_index
                );
            }

            if (matches.size() == 0) {
                if (!omit_no_match_)
                    set_scalar_missing(output);
            }
            else {
                build_store(
                    subject, matches, matcher.subject_is_ascii(),
                    builder, output
                );
            }
        }

        if (simplifying_) {
            const R_len_t current_size =
                static_cast<R_len_t>(output.size());
            if (max_columns < current_size)
                max_columns = current_size;
        }
    }

    const std::vector<shared::StringView>& subjects_;
    const shared::RegexPatterns& patterns_;
    R_len_t vectorize_length_;
    shared::RegexOptions options_;
    bool omit_no_match_;
    bool simplifying_;
    std::vector<io::OutputStore>& stores_;
    std::vector<R_len_t>& max_columns_;
};

} // namespace search_regex_extract

using namespace search_regex_extract;


/** Extract the first regular-expression match from each string. */
CHARR_ENTRYPOINT SEXP ci_extract_first_regex(
    SEXP str, SEXP pattern, SEXP opts_regex
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();


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
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);

    const shared::RegexOptions options = regex::prepare_options(opts_regex);
    const R_len_t tasks = vectorize_length == 0
        ? 0 : pattern_length == 1
            ? vectorize_length : pattern_length;
    const shared::ParallelPlan plan = shared::parallel_plan(true, tasks);


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
        io::OutputBuilder output(0);
        io::ParallelOutputBuilder parallel_output;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                if (plan.workers == 1)
                    output.reset(vectorize_length);
                if (vectorize_length > 0) {
                    subject_reader.reset(str);
                    if (subject_reader.size() != subject_length) {
                        throw std::runtime_error(
                            "Reader length changed during regex extraction"
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
                            "Reader length changed during regex extraction"
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
                }

                if (plan.workers > 1) {
                    parallel_output.reset(vectorize_length, plan.workers);
                    FirstBody body(
                        subjects, patterns, vectorize_length,
                        options, parallel_output
                    );
                    shared::run_parallel(plan, tasks, body);
                    result = entry_protections.reprotect_one(
                        parallel_output.to_sexp(), result_index
                    );
                }

                if (plan.workers == 1 &&
                        pattern_length == 1 && vectorize_length > 0) {
                    const std::size_t pattern_index = 0;
                    const shared::RegexInput current_pattern =
                        patterns.get(pattern_index);
                    const bool pattern_unusable =
                        current_pattern.missing ||
                        current_pattern.length <= 0;
                    bool pattern_bound = false;

                    for (R_len_t i = 0; i < vectorize_length; ++i) {
                        const shared::StringView& subject = subjects[
                            static_cast<std::size_t>(i)
                        ];
                        if (subject.is_na() || pattern_unusable) {
                            output.set_na(i);
                            continue;
                        }
                        if (!pattern_bound) {
                            bind_pattern(
                                matcher, current_pattern, patterns,
                                pattern_index
                            );
                            pattern_bound = true;
                        }

                        shared::RegexRange match{0, 0};
                        UErrorCode status = U_ZERO_ERROR;
                        const bool found = matcher.find_first(
                            subject, &subject, match, status
                        );
                        if (U_FAILURE(status)) {
                            throw_regex_error(
                                status, false, patterns, pattern_index
                            );
                        }
                        if (!found) {
                            output.set_na(i);
                            continue;
                        }

                        const int length = match.end-match.start;
                        output.set_validated(i, make_strview(
                            length == 0
                                ? ""
                                : subject.ptr+match.start,
                            length,
                            matcher.subject_is_ascii()
                                ? CETYPE_EXT_ASCII
                                : CETYPE_EXT_UTF8
                        ));
                    }
                }
                else if (plan.workers == 1) {
                    for (R_len_t lane = 0;
                            lane < (vectorize_length > 0
                                ? pattern_length : 0);
                            ++lane) {
                        const std::size_t pattern_index =
                            static_cast<std::size_t>(lane);
                        const shared::RegexInput current_pattern =
                            patterns.get(pattern_index);
                        const bool pattern_unusable =
                            current_pattern.missing ||
                            current_pattern.length <= 0;
                        bool pattern_bound = false;

                        R_len_t i = lane;
                        for (;;) {
                            const std::size_t subject_index =
                                static_cast<std::size_t>(
                                    i % subject_length
                                );
                            const shared::StringView& subject =
                                subjects[subject_index];
                            if (subject.is_na() || pattern_unusable) {
                                output.set_na(i);
                            }
                            else {
                                if (!pattern_bound) {
                                    bind_pattern(
                                        matcher, current_pattern,
                                        patterns, pattern_index
                                    );
                                    pattern_bound = true;
                                }

                                shared::RegexRange match{0, 0};
                                UErrorCode status = U_ZERO_ERROR;
                                const bool found = matcher.find_first(
                                    subject, &subjects[subject_index],
                                    match, status
                                );
                                if (U_FAILURE(status)) {
                                    throw_regex_error(
                                        status, false, patterns,
                                        pattern_index
                                    );
                                }
                                if (!found) {
                                    output.set_na(i);
                                }
                                else {
                                    const int length =
                                        match.end-match.start;
                                    output.set_validated(i, make_strview(
                                        length == 0
                                            ? ""
                                            : subject.ptr+match.start,
                                        length,
                                        matcher.subject_is_ascii()
                                            ? CETYPE_EXT_ASCII
                                            : CETYPE_EXT_UTF8
                                    ));
                                }
                            }

                            if (pattern_length >= vectorize_length-i)
                                break;
                            i += pattern_length;
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
        emit_empty_pattern_warnings_r(empty_pattern_warnings);
    );
}


/** Extract every regular-expression match from each string. */
CHARR_ENTRYPOINT SEXP ci_extract_all_regex(
    SEXP str,
    SEXP pattern,
    SEXP simplify,
    SEXP omit_no_match,
    SEXP opts_regex
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const shared::RegexOptions options = regex::prepare_options(opts_regex);
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
        ci__prepare_arg_string_r(pattern, "pattern")
    );

    const int simplify_value = LOGICAL_RO(simplify)[0];
    const bool simplifying =
        simplify_value == NA_LOGICAL || simplify_value != 0;
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

    SEXP temporary = R_NilValue;
    PROTECT_INDEX temporary_index;

    int empty_pattern_warnings = 0;
    R_len_t max_columns = 0;

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
        std::vector<io::OutputStore> stores;
        io::OutputBuilder child_builder(0);
        io::OutputBuilder matrix_builder(0);
        std::vector<R_len_t> worker_max_columns;

        matches.reserve(8);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                if (vectorize_length > 0) {
                    subject_reader.reset(str);
                    if (subject_reader.size() != subject_length) {
                        throw std::runtime_error(
                            "Reader length changed during regex extraction"
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
                            "Reader length changed during regex extraction"
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
                }

                stores.reserve(
                    static_cast<std::size_t>(vectorize_length)
                );
                for (R_len_t i = 0; i < vectorize_length; ++i)
                    stores.emplace_back(0, 0);

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
                    const bool pattern_unusable =
                        current_pattern.missing ||
                        current_pattern.length <= 0;
                    bool pattern_bound = false;

                    R_len_t i = lane;
                    for (;;) {
                        io::OutputStore& output = stores[
                            static_cast<std::size_t>(i)
                        ];
                        const std::size_t subject_index =
                            pattern_length == 1
                                ? static_cast<std::size_t>(i)
                                : static_cast<std::size_t>(
                                    i % subject_length
                                );
                        const shared::StringView& subject =
                            subjects[subject_index];

                        if (subject.is_na() || pattern_unusable) {
                            set_scalar_missing(output);
                        }
                        else {
                            if (!pattern_bound) {
                                bind_pattern(
                                    matcher, current_pattern, patterns,
                                    pattern_index
                                );
                                pattern_bound = true;
                            }

                            UErrorCode status = U_ZERO_ERROR;
                            matcher.find_all(
                                subject, &subjects[subject_index],
                                matches, status
                            );
                            if (U_FAILURE(status)) {
                                throw_regex_error(
                                    status, false, patterns,
                                    pattern_index
                                );
                            }

                            if (matches.size() == 0) {
                                if (!omit_no_match_value)
                                    set_scalar_missing(output);
                            }
                            else {
                                build_store(
                                    subject, matches,
                                    matcher.subject_is_ascii(),
                                    child_builder, output
                                );
                            }
                        }

                        if (simplifying) {
                            const R_len_t current_size =
                                static_cast<R_len_t>(output.size());
                            if (max_columns < current_size)
                                max_columns = current_size;
                        }

                        if (pattern_length >= vectorize_length-i)
                            break;
                        i += pattern_length;
                    }
                }
                }
                else {
                    worker_max_columns.assign(
                        static_cast<std::size_t>(
                            parallel_plan.workers
                        ),
                        0
                    );
                    AllBody body(
                        subjects, patterns, vectorize_length, options,
                        omit_no_match_value, simplifying,
                        stores, worker_max_columns
                    );
                    shared::run_parallel(
                        parallel_plan, tasks, body
                    );
                    if (simplifying) {
                        for (std::size_t i = 0;
                                i < worker_max_columns.size(); ++i) {
                            if (max_columns < worker_max_columns[i])
                                max_columns = worker_max_columns[i];
                        }
                    }
                }

                callback_protections.protect_with_index(
                    temporary, &temporary_index
                );
                if (!simplifying) {
                    result = entry_protections.reprotect_one(
                        Rf_allocVector(VECSXP, vectorize_length),
                        result_index
                    );
                    for (R_len_t i = 0; i < vectorize_length; ++i) {
                        temporary = callback_protections.reprotect_slot(
                            io::finalize(std::move(stores[
                                static_cast<std::size_t>(i)
                            ])),
                            temporary_index
                        );
                        SET_VECTOR_ELT(result, i, temporary);
                    }
                }
                else {
                    const R_xlen_t rows = vectorize_length;
                    const R_xlen_t columns = max_columns;
                    if (rows > 0 && columns > R_XLEN_T_MAX/rows) {
                        throw std::length_error(
                            "matrix length exceeds R's vector limit"
                        );
                    }

                    matrix_builder.reset(rows*columns);
                    for (R_xlen_t i = 0; i < rows; ++i) {
                        const io::OutputStore& current = stores[
                            static_cast<std::size_t>(i)
                        ];
                        const R_xlen_t current_size =
                            static_cast<R_xlen_t>(current.size());
                        R_xlen_t j = 0;
                        for (; j < current_size; ++j) {
                            matrix_builder.set_validated(
                                i+j*rows, current.view(j)
                            );
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
                        Rf_allocVector(INTSXP, 2), temporary_index
                    );
                    INTEGER(temporary)[0] = vectorize_length;
                    INTEGER(temporary)[1] = max_columns;
                    result = entry_protections.reprotect_one(
                        Rf_setAttrib(result, R_DimSymbol, temporary),
                        result_index
                    );
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_empty_pattern_warnings_r(empty_pattern_warnings);
    );
}

} } // namespace charr::altrep_backend
