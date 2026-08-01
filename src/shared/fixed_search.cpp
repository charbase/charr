// Derived from stringi 19e9586ba39b3320df49355e32bd18d74ed6098f.
// Copyright (c) 2013-2025, Marek Gagolewski. See inst/COPYRIGHTS.

#include "fixed_search.h"

#include "byte_search_matcher.h"

#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>

#include <unicode/uchar.h>
#include <unicode/utf8.h>

namespace charr {
namespace shared {

CHARR_NEUTRAL_HELPER bool fixed_starts_with(
    const StringView& subject, int byte_index,
    const StringView& pattern, bool case_insensitive
) noexcept
{
    if (byte_index < 0 || byte_index > subject.len || pattern.len <= 0 ||
            subject.ptr == nullptr || pattern.ptr == nullptr) {
        return false;
    }

    if (!case_insensitive) {
        return pattern.len <= subject.len-byte_index &&
            std::memcmp(
                subject.ptr+byte_index, pattern.ptr,
                static_cast<std::size_t>(pattern.len)
            ) == 0;
    }

    int subject_index = byte_index;
    int pattern_index = 0;
    UChar32 subject_code_point;
    UChar32 pattern_code_point;
    while (pattern_index < pattern.len) {
        if (subject_index >= subject.len)
            return false;
        U8_NEXT(
            subject.ptr, subject_index, subject.len, subject_code_point
        );
        U8_NEXT(
            pattern.ptr, pattern_index, pattern.len, pattern_code_point
        );
        if (u_toupper(subject_code_point) != u_toupper(pattern_code_point))
            return false;
    }
    return true;
}


CHARR_NEUTRAL_HELPER bool fixed_ends_with(
    const StringView& subject, int byte_index,
    const StringView& pattern, bool case_insensitive
) noexcept
{
    if (byte_index < 0 || byte_index > subject.len || pattern.len <= 0 ||
            subject.ptr == nullptr || pattern.ptr == nullptr) {
        return false;
    }

    if (!case_insensitive) {
        return pattern.len <= byte_index &&
            std::memcmp(
                subject.ptr+byte_index-pattern.len, pattern.ptr,
                static_cast<std::size_t>(pattern.len)
            ) == 0;
    }

    int subject_index = byte_index;
    int pattern_index = pattern.len;
    UChar32 subject_code_point;
    UChar32 pattern_code_point;
    while (pattern_index > 0) {
        if (subject_index <= 0)
            return false;
        U8_PREV(
            subject.ptr, 0, subject_index, subject_code_point
        );
        U8_PREV(
            pattern.ptr, 0, pattern_index, pattern_code_point
        );
        if (u_toupper(subject_code_point) != u_toupper(pattern_code_point))
            return false;
    }
    return true;
}

FixedMatcher::FixedMatcher() noexcept
    : matcher_(), pattern_(nullptr), pattern_length_(0),
      options_{false, false}
{
}


FixedMatcher::~FixedMatcher() noexcept = default;


FixedExtractPlan::FixedExtractPlan() noexcept
    : rows(), matches(), max_columns(0), matches_are_patterns(false)
{
}


void FixedMatcher::prepare(
    const StringView& pattern,
    FixedSearchOptions options
)
{
    if (pattern.is_na() || pattern.ptr == nullptr || pattern.len <= 0)
        throw std::invalid_argument("fixed matcher requires a pattern");

    if (matcher_ && pattern.ptr == pattern_ &&
            pattern.len == pattern_length_ &&
            options.case_insensitive == options_.case_insensitive &&
            options.overlap == options_.overlap) {
        return;
    }

    matcher_.reset(new ByteSearchMatcher(
        pattern.ptr, pattern.len, options.overlap,
        options.case_insensitive
    ));
    pattern_ = pattern.ptr;
    pattern_length_ = pattern.len;
    options_ = options;
}


int FixedMatcher::count(
    const StringView& subject,
    const StringView& pattern,
    FixedSearchOptions options
)
{
    if (!reset_subject(subject, pattern, options))
        return 0;

    int found = 0;
    while (matcher_->find_next() != ByteSearchMatcher::not_found)
        ++found;
    return found;
}


bool FixedMatcher::contains(
    const StringView& subject,
    const StringView& pattern,
    FixedSearchOptions options
)
{
    if (!reset_subject(subject, pattern, options))
        return false;
    return matcher_->find_first() != ByteSearchMatcher::not_found;
}


bool FixedMatcher::find_first(
    const StringView& subject,
    const StringView& pattern,
    FixedSearchOptions options,
    FixedRange& output
)
{
    if (!reset_subject(subject, pattern, options))
        return false;

    const int start = matcher_->find_first();
    if (start == ByteSearchMatcher::not_found)
        return false;

    output.start = start;
    output.end = start+matcher_->matched_length();
    return true;
}


void FixedMatcher::find_all(
    const StringView& subject,
    const StringView& pattern,
    FixedSearchOptions options,
    std::vector<FixedRange>& output
)
{
    output.clear();
    if (!reset_subject(subject, pattern, options))
        return;

    int start = matcher_->find_first();
    while (start != ByteSearchMatcher::not_found) {
        output.push_back(FixedRange{
            start, start+matcher_->matched_length()
        });
        start = matcher_->find_next();
    }
}


FixedSplitResult FixedMatcher::split(
    const StringView& subject,
    const StringView& pattern,
    FixedSearchOptions options,
    int n,
    bool omit_empty,
    bool tokens_only,
    std::vector<FixedRange>& fields
)
{
    if (!options.case_insensitive && pattern.ptr != nullptr &&
            pattern.len == 1) {
        return split_exact_byte(
            subject, static_cast<unsigned char>(pattern.ptr[0]),
            n, omit_empty, tokens_only, fields
        );
    }

    fields.clear();
    if (n >= std::numeric_limits<int>::max()-1)
        return FixedSplitResult::limit_too_large;
    if (n == 0)
        return FixedSplitResult::ok;
    if (!reset_subject(subject, pattern, options)) {
        return FixedSplitResult::ok;
    }

    const int field_limit = n < 0
        ? std::numeric_limits<int>::max()
        : n;
    int search_limit = field_limit;
    if (tokens_only && search_limit < std::numeric_limits<int>::max())
        ++search_limit;

    fields.push_back(FixedRange{0, 0});
    int field_count = 1;
    while (field_count < search_limit) {
        const int start = matcher_->find_next();
        if (start == ByteSearchMatcher::not_found)
            break;
        const int end = start+matcher_->matched_length();
        FixedRange& current = fields.back();
        if (omit_empty && current.start == start) {
            current.start = end;
        }
        else {
            current.end = start;
            fields.push_back(FixedRange{end, end});
            ++field_count;
        }
    }

    fields.back().end = subject.len;
    if (omit_empty && fields.back().start == fields.back().end)
        fields.pop_back();

    if (tokens_only && field_limit < std::numeric_limits<int>::max()) {
        while (fields.size() > static_cast<std::size_t>(field_limit))
            fields.pop_back();
    }
    return FixedSplitResult::ok;
}


bool FixedMatcher::reset_subject(
    const StringView& subject,
    const StringView& pattern,
    FixedSearchOptions options
)
{
    if (subject.is_na() || subject.len < 0 ||
            (subject.ptr == nullptr && subject.len != 0)) {
        throw std::invalid_argument("fixed matcher requires a subject");
    }
    if (subject.len == 0)
        return false;

    prepare(pattern, options);
    matcher_->reset(subject.ptr, subject.len);
    return true;
}


void plan_fixed_extract(
    const std::vector<StringView>& subjects,
    const std::vector<StringView>& patterns,
    int output_length,
    FixedSearchOptions options,
    bool omit_no_match,
    FixedMatcher& matcher,
    std::vector<FixedRange>& scratch,
    FixedExtractPlan& plan
)
{
    plan.rows.clear();
    plan.matches.clear();
    plan.max_columns = 0;
    plan.matches_are_patterns = !options.case_insensitive;

    if (output_length <= 0)
        return;
    if (subjects.empty() || patterns.empty()) {
        throw std::invalid_argument(
            "fixed extraction requires non-empty inputs"
        );
    }

    plan.rows.resize(static_cast<std::size_t>(output_length));
    for (int i = 0; i < output_length; ++i) {
        const std::size_t pattern_index =
            static_cast<std::size_t>(i) % patterns.size();
        const StringView& subject = subjects[
            static_cast<std::size_t>(i) % subjects.size()
        ];
        const StringView& pattern = patterns[pattern_index];
        FixedExtractRow& row = plan.rows[static_cast<std::size_t>(i)];
        row.begin = plan.matches_are_patterns
            ? pattern_index
            : plan.matches.size();
        row.count = 0;
        row.forced_na = false;

        if (subject.is_na() || pattern.is_na() || pattern.len <= 0) {
            row.forced_na = true;
        }
        else if (subject.len <= 0) {
            row.forced_na = !omit_no_match;
        }
        else if (plan.matches_are_patterns) {
            row.count = options.overlap
                ? matcher.count(subject, pattern, options)
                : count_exact_bytes(
                    subject.ptr, subject.len,
                    pattern.ptr, pattern.len
                );
            row.forced_na = row.count == 0 && !omit_no_match;
        }
        else {
            matcher.find_all(subject, pattern, options, scratch);
            const StringEncoding encoding =
                subject.enc == StringEncoding::ascii
                    ? StringEncoding::ascii
                    : StringEncoding::ascii_or_utf8;
            for (std::size_t j = 0; j < scratch.size(); ++j) {
                const FixedRange& range = scratch[j];
                plan.matches.push_back(StringView{
                    subject.ptr+range.start,
                    range.end-range.start,
                    encoding
                });
            }
            const std::size_t count = plan.matches.size()-row.begin;
            if (count > static_cast<std::size_t>(
                    std::numeric_limits<int>::max())) {
                throw std::length_error(
                    "fixed extraction result is too large"
                );
            }
            row.count = static_cast<int>(count);
            row.forced_na = row.count == 0 && !omit_no_match;
        }

        const int width = row.forced_na ? 1 : row.count;
        if (width > plan.max_columns)
            plan.max_columns = width;
    }
}

} // namespace shared
} // namespace charr
