#ifndef CHARR_SHARED_COLLATION_SEARCH_H
#define CHARR_SHARED_COLLATION_SEARCH_H

#include "lint.h"
#include "string_view.h"

#include <unicode/ucol.h>
#include <unicode/ustring.h>
#include <unicode/unistr.h>
#include <unicode/usearch.h>

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace charr {
namespace shared {

// A non-owning UTF-16 record. The identity distinguishes stable pattern
// records without comparing or copying their payloads.
struct CollationInput {
    const void* identity;
    const UChar* data;
    int length;
    bool missing;
};


struct CollationRange {
    int start;
    int end;
};


struct CollationUtf8Slice {
    const char* data;
    int length;
    bool ascii;
};


enum class CollationSplitResult : unsigned char {
    ok,
    limit_too_large
};


// Owns stable UTF-16 records prepared from normalized UTF-8 views. The owner
// lives in an entry point's Frame; set() only mutates that existing owner.
class CHARR_OWNER_TYPE CollationInputs {
public:
    CHARR_CXX_HELPER CollationInputs() noexcept;

    CollationInputs(const CollationInputs&) = delete;
    CollationInputs& operator=(const CollationInputs&) = delete;
    CollationInputs(CollationInputs&&) = delete;
    CollationInputs& operator=(CollationInputs&&) = delete;

    CHARR_CXX_HELPER void resize(std::size_t size);
    CHARR_CXX_HELPER void set(
        std::size_t index, const StringView& source
    );
    CHARR_CXX_HELPER void set_missing(std::size_t index);
    CHARR_CXX_HELPER void swap_value(
        std::size_t index, icu::UnicodeString& value
    );

    CHARR_NEUTRAL_HELPER std::size_t size() const noexcept;
    CHARR_CXX_HELPER CollationInput get(std::size_t index) const;

private:
    std::vector<icu::UnicodeString> values_;
    std::vector<unsigned char> missing_;
};


// Alternates between two UTF-16 buffers while walking stable, normalized
// UTF-8 records. ICU continues to borrow the previous buffer until the
// matcher is rebound, so get() fills the inactive buffer and leaves the
// borrowed one untouched. Record identity lets recycled records reuse the
// active conversion. The source records must not change between get() calls.
class CHARR_OWNER_TYPE CollationCursor {
public:
    CHARR_CXX_HELPER CollationCursor() noexcept;

    CollationCursor(const CollationCursor&) = delete;
    CollationCursor& operator=(const CollationCursor&) = delete;
    CollationCursor(CollationCursor&&) = delete;
    CollationCursor& operator=(CollationCursor&&) = delete;

    CHARR_CXX_HELPER CollationInput get(
        const void* identity, const StringView& source
    );

private:
    icu::UnicodeString values_[2];
    unsigned char active_;
    const void* identity_;
    bool missing_;

    CHARR_NEUTRAL_HELPER CollationInput current() const noexcept;
};


// Converts nondecreasing UTF-16 offsets to R's code-point positions in one
// pass through a collation subject. The cursor owns no storage.
class CollationPositionCursor {
public:
    CHARR_NEUTRAL_HELPER explicit CollationPositionCursor(
        const CollationInput& subject
    ) noexcept;

    CHARR_NEUTRAL_HELPER int at_utf16(int target) noexcept;
    CHARR_NEUTRAL_HELPER CollationRange to_r_range(
        const CollationRange& range, bool return_length
    ) noexcept;

private:
    const UChar* data_;
    int length_;
    int utf16_;
    int position_;
};


// Owns one ICU string-search handle. The collator and input records are
// borrowed from earlier Frame owners and must outlive this matcher.
class CHARR_OWNER_TYPE CollationMatcher {
public:
    CHARR_CXX_HELPER CollationMatcher() noexcept;
    CHARR_CXX_HELPER ~CollationMatcher() noexcept;

    CollationMatcher(const CollationMatcher&) = delete;
    CollationMatcher& operator=(const CollationMatcher&) = delete;
    CollationMatcher(CollationMatcher&&) = delete;
    CollationMatcher& operator=(CollationMatcher&&) = delete;

    CHARR_CXX_HELPER int count(
        UCollator* collator,
        const CollationInput& subject,
        const CollationInput& pattern,
        UErrorCode& status
    ) noexcept;
    CHARR_CXX_HELPER bool contains(
        UCollator* collator,
        const CollationInput& subject,
        const CollationInput& pattern,
        UErrorCode& status
    ) noexcept;
    CHARR_CXX_HELPER bool find_first(
        UCollator* collator,
        const CollationInput& subject,
        const CollationInput& pattern,
        CollationRange& match,
        UErrorCode& status
    ) noexcept;
    CHARR_CXX_HELPER void find_all(
        UCollator* collator,
        const CollationInput& subject,
        const CollationInput& pattern,
        std::vector<CollationRange>& matches,
        UErrorCode& status
    );
    // Split ranges are the fields between collation matches. A finite n stops
    // the ICU search as soon as that many fields have been found; matches
    // discarded by omit_empty do not count toward the limit.
    CHARR_CXX_HELPER CollationSplitResult split(
        UCollator* collator,
        const CollationInput& subject,
        const CollationInput& pattern,
        int n,
        bool omit_empty,
        bool tokens_only,
        std::vector<CollationRange>& fields,
        UErrorCode& status
    );
    CHARR_CXX_HELPER bool starts_with(
        UCollator* collator,
        const CollationInput& subject,
        const CollationInput& pattern,
        int from,
        UErrorCode& status
    ) noexcept;
    CHARR_CXX_HELPER bool ends_with(
        UCollator* collator,
        const CollationInput& subject,
        const CollationInput& pattern,
        int to,
        UErrorCode& status
    ) noexcept;

private:
    UStringSearch* matcher_;
    UCollator* collator_;
    const void* pattern_identity_;

    CHARR_CXX_HELPER void close() noexcept;
    CHARR_CXX_HELPER bool prepare(
        UCollator* collator,
        const CollationInput& subject,
        const CollationInput& pattern,
        UErrorCode& status
    ) noexcept;
};


CHARR_NEUTRAL_HELPER int utf16_start_offset(
    const CollationInput& subject, int position
) noexcept;


CHARR_NEUTRAL_HELPER int utf16_end_offset(
    const CollationInput& subject, int position
) noexcept;


CHARR_CXX_HELPER inline CollationUtf8Slice collation_utf8_slice(
    const CollationInput& subject,
    const CollationRange& range,
    std::vector<char>& buffer,
    UErrorCode& status
)
{
    status = U_ZERO_ERROR;
    if (subject.missing || subject.length < 0 ||
            (subject.data == nullptr && subject.length != 0) ||
            range.start < 0 || range.end < range.start ||
            range.end > subject.length) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return CollationUtf8Slice{"", 0, true};
    }

    const int utf16_length = range.end-range.start;
    if (utf16_length == 0)
        return CollationUtf8Slice{"", 0, true};

    const int maximum = std::numeric_limits<int>::max();
    if (utf16_length > maximum/3-10)
        throw std::length_error("UTF-8 output exceeds ICU's length limit");

    const std::size_t capacity = static_cast<std::size_t>(utf16_length)*3;
    if (buffer.size() < capacity)
        buffer.resize(capacity);

    int utf8_length = 0;
    u_strToUTF8(
        buffer.data(), static_cast<int>(capacity), &utf8_length,
        subject.data+range.start, utf16_length, &status
    );
    if (U_FAILURE(status))
        return CollationUtf8Slice{"", 0, true};

    return CollationUtf8Slice{
        buffer.data(), utf8_length, utf8_length == utf16_length
    };
}


// Splice literal replacement text into caller-owned storage. Search ranges
// are UTF-16 offsets returned by CollationMatcher and may be zero-width.
CHARR_CXX_HELPER void write_collation_replacement(
    const CollationInput& subject,
    const CollationInput& replacement,
    const std::vector<CollationRange>& ranges,
    icu::UnicodeString& output
);

} // namespace shared
} // namespace charr

#endif
