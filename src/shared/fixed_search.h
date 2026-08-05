#ifndef CHARR_SHARED_FIXED_SEARCH_H
#define CHARR_SHARED_FIXED_SEARCH_H

#include "lint.h"
#include "replacement.h"
#include "string_view.h"

#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace charr {
namespace shared {

class ByteSearchMatcher;


struct FixedSearchOptions {
    bool case_insensitive;
    bool overlap;
};


using FixedRange = ByteRange;


enum class FixedSplitResult : unsigned char {
    ok,
    limit_too_large
};


// Keep the scalar delimiter path inline: fixed splitting commonly handles
// short fields, so an out-of-line generic matcher call is measurable here.
CHARR_CXX_HELPER CHARR_ALWAYS_INLINE FixedSplitResult split_exact_byte(
    const StringView& subject,
    unsigned char delimiter,
    int n,
    bool omit_empty,
    bool tokens_only,
    std::vector<FixedRange>& fields
)
{
    fields.clear();
    if (n >= std::numeric_limits<int>::max()-1)
        return FixedSplitResult::limit_too_large;
    if (n == 0)
        return FixedSplitResult::ok;
    if (subject.is_na() || subject.len < 0 ||
            (subject.ptr == nullptr && subject.len != 0)) {
        throw std::invalid_argument("fixed matcher requires a subject");
    }
    if (subject.len == 0)
        return FixedSplitResult::ok;

    const int field_limit = n < 0
        ? std::numeric_limits<int>::max()
        : n;
    int search_limit = field_limit;
    if (tokens_only && search_limit < std::numeric_limits<int>::max())
        ++search_limit;

    fields.push_back(FixedRange{0, 0});
    int field_count = 1;
    int search_from = 0;
    while (field_count < search_limit) {
        const void* match = std::memchr(
            subject.ptr+search_from, delimiter,
            static_cast<std::size_t>(subject.len-search_from)
        );
        if (match == nullptr)
            break;

        const int start = static_cast<int>(
            static_cast<const char*>(match)-subject.ptr
        );
        const int end = start+1;
        search_from = end;
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


// Exact byte search used by direct fixed-string fast paths. The general
// matcher below handles case folding, overlap, and matcher reuse.
CHARR_NEUTRAL_HELPER inline int find_first_exact_bytes(
    const char* subject, int subject_length,
    const char* pattern, int pattern_length
) noexcept
{
    if (subject == nullptr || pattern == nullptr ||
            subject_length < pattern_length || pattern_length <= 0) {
        return -1;
    }

    if (pattern_length == 1) {
        const void* found = std::memchr(
            subject, static_cast<unsigned char>(pattern[0]),
            static_cast<std::size_t>(subject_length)
        );
        return found == nullptr
            ? -1
            : static_cast<int>(static_cast<const char*>(found)-subject);
    }

    const char* current = subject;
    const char* const last = subject+subject_length-pattern_length;
    while (current <= last) {
        const std::size_t available = static_cast<std::size_t>(
            last-current+1
        );
        current = static_cast<const char*>(std::memchr(
            current, static_cast<unsigned char>(pattern[0]), available
        ));
        if (current == nullptr)
            return -1;
        if (std::memcmp(
                current, pattern,
                static_cast<std::size_t>(pattern_length)
            ) == 0) {
            return static_cast<int>(current-subject);
        }
        ++current;
    }
    return -1;
}


CHARR_NEUTRAL_HELPER inline int count_exact_bytes(
    const char* subject, int subject_length,
    const char* pattern, int pattern_length
) noexcept
{
    if (subject == nullptr || pattern == nullptr ||
            subject_length <= 0 || pattern_length <= 0) {
        return 0;
    }

    int count = 0;
    int offset = 0;
    while (offset <= subject_length-pattern_length) {
        const int relative = find_first_exact_bytes(
            subject+offset, subject_length-offset,
            pattern, pattern_length
        );
        if (relative < 0)
            break;
        ++count;
        offset += relative+pattern_length;
    }
    return count;
}


// Exact fixed comparisons at a byte boundary in normalized UTF-8 input.
CHARR_NEUTRAL_HELPER bool fixed_starts_with(
    const StringView& subject, int byte_index,
    const StringView& pattern, bool case_insensitive
) noexcept;

CHARR_NEUTRAL_HELPER bool fixed_ends_with(
    const StringView& subject, int byte_index,
    const StringView& pattern, bool case_insensitive
) noexcept;


// Reusable pure-C++ state for fixed byte search. Pattern bytes are borrowed
// and must outlive every count() call that uses them.
class CHARR_OWNER_TYPE FixedMatcher {
public:
    CHARR_CXX_HELPER FixedMatcher() noexcept;
    CHARR_CXX_HELPER ~FixedMatcher() noexcept;

    FixedMatcher(const FixedMatcher&) = delete;
    FixedMatcher& operator=(const FixedMatcher&) = delete;
    FixedMatcher(FixedMatcher&&) = delete;
    FixedMatcher& operator=(FixedMatcher&&) = delete;

    CHARR_CXX_HELPER int count(
        const StringView& subject,
        const StringView& pattern,
        FixedSearchOptions options
    );

    CHARR_CXX_HELPER bool contains(
        const StringView& subject,
        const StringView& pattern,
        FixedSearchOptions options
    );

    CHARR_CXX_HELPER bool find_first(
        const StringView& subject,
        const StringView& pattern,
        FixedSearchOptions options,
        FixedRange& output
    );

    CHARR_CXX_HELPER void find_all(
        const StringView& subject,
        const StringView& pattern,
        FixedSearchOptions options,
        std::vector<FixedRange>& output
    );

    // Split ranges are the fields between fixed matches. Empty fields removed
    // by omit_empty do not count toward a finite limit. tokens_only searches
    // one field ahead, then discards the remainder.
    CHARR_CXX_HELPER FixedSplitResult split(
        const StringView& subject,
        const StringView& pattern,
        FixedSearchOptions options,
        int n,
        bool omit_empty,
        bool tokens_only,
        std::vector<FixedRange>& fields
    );

private:
    std::unique_ptr<ByteSearchMatcher> matcher_;
    const char* pattern_;
    int pattern_length_;
    FixedSearchOptions options_;

    CHARR_CXX_HELPER void prepare(
        const StringView& pattern,
        FixedSearchOptions options
    );

    CHARR_CXX_HELPER bool reset_subject(
        const StringView& subject,
        const StringView& pattern,
        FixedSearchOptions options
    );
};


struct FixedExtractRow {
    // Index into FixedExtractPlan::matches for case-insensitive search, or
    // into the caller's pattern vector when matches_are_patterns is true.
    std::size_t begin;
    int count;
    bool forced_na;
};


struct FixedExtractRepeatKey {
    StringView pattern;
    int count;

    CHARR_NEUTRAL_HELPER bool operator==(
        const FixedExtractRepeatKey& other
    ) const noexcept
    {
        return count == other.count &&
            pattern.len == other.pattern.len &&
            pattern.enc == other.pattern.enc &&
            (pattern.len <= 0 || std::memcmp(
                pattern.ptr, other.pattern.ptr,
                static_cast<std::size_t>(pattern.len)
            ) == 0);
    }
};


struct FixedExtractRepeatHash {
    CHARR_NEUTRAL_HELPER std::size_t operator()(
        const FixedExtractRepeatKey& value
    ) const noexcept
    {
        std::size_t hash = static_cast<std::size_t>(2166136261U);
        for (int i = 0; i < value.pattern.len; ++i) {
            hash ^= static_cast<unsigned char>(value.pattern.ptr[i]);
            hash *= static_cast<std::size_t>(16777619U);
        }
        hash ^= static_cast<std::size_t>(value.pattern.enc);
        hash *= static_cast<std::size_t>(16777619U);
        hash ^= static_cast<std::size_t>(value.count);
        return hash;
    }
};


struct CHARR_OWNER_TYPE FixedExtractPlan {
    CHARR_CXX_HELPER FixedExtractPlan() noexcept;

    std::vector<FixedExtractRow> rows;
    std::vector<StringView> matches;
    int max_columns;
    bool matches_are_patterns;
};


CHARR_CXX_HELPER void plan_fixed_extract(
    const std::vector<StringView>& subjects,
    const std::vector<StringView>& patterns,
    int output_length,
    FixedSearchOptions options,
    bool omit_no_match,
    FixedMatcher& matcher,
    std::vector<FixedRange>& scratch,
    FixedExtractPlan& plan
);


CHARR_CXX_HELPER void plan_fixed_extract(
    const std::vector<StringView>& subjects,
    const std::vector<StringView>& patterns,
    int begin,
    int end,
    FixedSearchOptions options,
    bool omit_no_match,
    FixedMatcher& matcher,
    std::vector<FixedRange>& scratch,
    FixedExtractPlan& plan
);


CHARR_CXX_HELPER CHARR_ALWAYS_INLINE FixedSplitResult split_fixed_fields(
    FixedMatcher& matcher,
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
    return matcher.split(
        subject, pattern, options, n,
        omit_empty, tokens_only, fields
    );
}

} // namespace shared
} // namespace charr

#endif
