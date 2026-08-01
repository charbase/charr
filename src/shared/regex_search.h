#ifndef CHARR_SHARED_REGEX_SEARCH_H
#define CHARR_SHARED_REGEX_SEARCH_H

#include "lint.h"
#include "string_view.h"

#include <unicode/regex.h>
#include <unicode/unistr.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace charr {
namespace shared {

struct RegexOptions {
    std::uint32_t flags;
    std::int32_t stack_limit;
    std::int32_t time_limit;
};


// A non-owning UTF-16 record. Pattern identities remain stable after the
// pattern collection has been sized and filled.
struct RegexInput {
    const void* identity;
    const icu::UnicodeString* value;
    int length;
    bool missing;
};


struct RegexRange {
    int start;
    int end;
};


// Convert a normalized UTF-8 view to ICU's UTF-16 representation. Malformed
// UTF-8 uses the same U+FFFD substitution as the regex input path.
CHARR_CXX_HELPER void set_regex_utf16(
    icu::UnicodeString& output,
    const StringView& source
);


// Convert UTF-8 byte ranges to R's 1-based code-point locations. A negative
// range is an unmatched capture and is left unchanged.
CHARR_NEUTRAL_HELPER void regex_range_to_positions(
    const StringView& subject,
    RegexRange& range,
    bool return_length
) noexcept;

CHARR_NEUTRAL_HELPER void regex_ranges_to_positions(
    const StringView& subject,
    std::vector<RegexRange>& ranges,
    bool return_length
) noexcept;


enum class RegexSplitResult : unsigned char {
    ok,
    limit_too_large
};


enum class RegexReplaceMode : unsigned char {
    first,
    all
};


enum class RegexReplaceResult : unsigned char {
    value,
    missing
};


// Owns the UTF-16 patterns for one operation. Input views must already be
// normalized to UTF-8; malformed UTF-8 is replaced in the same way as the
// legacy stringi input container.
class CHARR_OWNER_TYPE RegexPatterns {
public:
    CHARR_CXX_HELPER RegexPatterns() noexcept;

    RegexPatterns(const RegexPatterns&) = delete;
    RegexPatterns& operator=(const RegexPatterns&) = delete;
    RegexPatterns(RegexPatterns&&) = delete;
    RegexPatterns& operator=(RegexPatterns&&) = delete;

    CHARR_CXX_HELPER void resize(std::size_t size);
    CHARR_CXX_HELPER void set(
        std::size_t index, const StringView& source
    );

    CHARR_NEUTRAL_HELPER std::size_t size() const noexcept;
    CHARR_CXX_HELPER RegexInput get(std::size_t index) const;
    CHARR_NEUTRAL_HELPER int empty_count() const noexcept;
    CHARR_CXX_HELPER void context(
        std::size_t index, std::string& output
    ) const;

private:
    std::vector<icu::UnicodeString> values_;
    std::vector<unsigned char> missing_;
};


// Owns one compiled ICU regex and a reusable UTF-16 subject buffer. Pattern
// storage is borrowed from an earlier Frame owner and must outlive this
// matcher. Subject identities let recycled records reuse their conversion.
class CHARR_OWNER_TYPE RegexMatcher {
public:
    CHARR_CXX_HELPER explicit RegexMatcher(
        RegexOptions options
    ) noexcept;
    CHARR_CXX_HELPER ~RegexMatcher() noexcept;

    RegexMatcher(const RegexMatcher&) = delete;
    RegexMatcher& operator=(const RegexMatcher&) = delete;
    RegexMatcher(RegexMatcher&&) = delete;
    RegexMatcher& operator=(RegexMatcher&&) = delete;

    CHARR_CXX_HELPER void reset_options(RegexOptions options) noexcept;

    CHARR_CXX_HELPER bool bind(
        const RegexInput& pattern,
        UErrorCode& status,
        bool& pattern_compile_error
    ) noexcept;

    CHARR_CXX_HELPER bool contains(
        const StringView& subject,
        const void* subject_identity,
        UErrorCode& status
    );

    CHARR_CXX_HELPER int count(
        const StringView& subject,
        const void* subject_identity,
        UErrorCode& status
    );

    CHARR_CXX_HELPER bool find_first(
        const StringView& subject,
        const void* subject_identity,
        RegexRange& match,
        UErrorCode& status
    );

    CHARR_CXX_HELPER void find_all(
        const StringView& subject,
        const void* subject_identity,
        std::vector<RegexRange>& matches,
        UErrorCode& status
    );

    CHARR_CXX_HELPER bool find_first_with_captures(
        const StringView& subject,
        const void* subject_identity,
        RegexRange& match,
        std::vector<RegexRange>& captures,
        UErrorCode& status
    );

    CHARR_CXX_HELPER void find_all_with_captures(
        const StringView& subject,
        const void* subject_identity,
        std::vector<RegexRange>& matches,
        std::vector<std::vector<RegexRange> >& captures,
        UErrorCode& status
    );

    // Apply the currently bound pattern to a normalized UTF-8 view. The
    // identity must remain stable while the view is valid and lets recycled
    // records reuse their UTF-16 conversion.
    CHARR_CXX_HELPER RegexReplaceResult replace(
        const StringView& subject,
        const void* subject_identity,
        const icu::UnicodeString* replacement,
        RegexReplaceMode mode,
        icu::UnicodeString& output,
        UErrorCode& status
    );

    // Apply the currently bound pattern to an already-normalized UTF-16
    // subject. A null replacement means that a matching element becomes
    // missing; a nonmatching element still returns the original subject.
    CHARR_CXX_HELPER RegexReplaceResult replace(
        const icu::UnicodeString& subject,
        const icu::UnicodeString* replacement,
        RegexReplaceMode mode,
        icu::UnicodeString& output,
        UErrorCode& status
    );

    CHARR_NEUTRAL_HELPER int group_count() const noexcept;
    CHARR_CXX_HELPER void capture_names(
        std::vector<std::string>& names,
        UErrorCode& status
    ) const;

    // Split ranges are UTF-8 byte slices from the normalized subject. Empty
    // fields removed by omit_empty do not count toward a finite limit.
    CHARR_CXX_HELPER RegexSplitResult split(
        const StringView& subject,
        const void* subject_identity,
        int n,
        bool omit_empty,
        bool tokens_only,
        std::vector<RegexRange>& fields,
        UErrorCode& status
    );

    CHARR_CXX_HELPER void split_default(
        const StringView& subject,
        const void* subject_identity,
        std::vector<RegexRange>& fields,
        UErrorCode& status
    );

    CHARR_NEUTRAL_HELPER bool subject_is_ascii() const noexcept;

private:
    icu::RegexMatcher* matcher_;
    const void* pattern_identity_;
    RegexOptions options_;
    icu::UnicodeString subject_;
    const void* subject_identity_;
    std::vector<int> byte_offsets_;
    bool subject_ascii_;
    bool byte_offsets_valid_;

    CHARR_CXX_HELPER void close() noexcept;
    CHARR_NEUTRAL_HELPER int byte_offset(
        int utf16_offset, UErrorCode& status
    ) const noexcept;
};

} // namespace shared
} // namespace charr

#endif
