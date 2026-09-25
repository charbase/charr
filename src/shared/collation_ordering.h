#ifndef CHARR_SHARED_COLLATION_ORDERING_H
#define CHARR_SHARED_COLLATION_ORDERING_H

#include "lint.h"
#include "index_hash_table.h"
#include "string_view.h"

#include <unicode/ucol.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace charr {
namespace shared {

/*
 * An abbreviated collation key, as PostgreSQL uses them: the first 8 bytes of
 * the element's ICU sort key from one ucol_nextSortKeyPart() call, packed
 * big-endian and zero padded. ICU key bytes are never zero, so the padding
 * keeps the order, and prefix order implies collation order. `complete`
 * records that the whole key fit in fewer than 8 bytes; two complete equal
 * prefixes are therefore collation-equal, and a complete prefix is never
 * collation-equal to an incomplete one. Equal prefixes that are not both
 * complete are resolved with ucol_strcollUTF8().
 *
 * The cost of one prefix is bounded by the first few characters, whatever
 * the string's length. A caller keeps one entry per element in a contiguous
 * vector of its Frame; the helpers below reorder that vector in place.
 */
struct CollationPrefix {
    std::uint64_t prefix;
    std::int32_t index;
    bool complete;
};


// Compute the entries of [begin, end), each at its own element index. A
// missing element gets an incomplete zero prefix that no caller compares, and
// so does every element when the collator's case level is on, where ICU's
// keys do not follow its collation order.
// Writes nothing outside the range, so disjoint ranges may be computed on
// different threads, each with its own collator.
CHARR_NEUTRAL_HELPER UErrorCode compute_collation_prefixes(
    const StringView* values,
    std::size_t begin,
    std::size_t end,
    const UCollator* collator,
    CollationPrefix* keys
) noexcept;


// Mark strings already encountered in the requested traversal direction.
// `keys` holds one computed entry per element and is reordered; `table` is
// scratch. Native allocation failures propagate as C++ exceptions. ICU
// failures are returned so each backend can preserve its established error
// message.
CHARR_CXX_HELPER UErrorCode mark_collation_duplicates(
    const StringView* values,
    std::size_t size,
    bool from_last,
    const UCollator* collator,
    std::vector<CollationPrefix>& keys,
    IndexHashTable& table,
    int* output
);


// Partition the source indices and stable-sort the nonmissing indices. `keys`
// holds one computed entry per element; on success it holds the nonmissing
// entries in sorted order. All vectors belong to the caller's Frame and are
// filled in place.
CHARR_CXX_HELPER UErrorCode build_collation_order(
    const StringView* values,
    std::size_t size,
    bool decreasing,
    const UCollator* collator,
    std::vector<CollationPrefix>& keys,
    std::vector<int>& order,
    std::vector<int>& missing
);


// Assign increasing minimum ranks through the sorted entries that
// build_collation_order() left in `keys`.
CHARR_NEUTRAL_HELPER UErrorCode assign_min_collation_ranks(
    const StringView* values,
    const UCollator* collator,
    const CollationPrefix* keys,
    std::size_t size,
    int* output
) noexcept;

} // namespace shared
} // namespace charr

#endif
