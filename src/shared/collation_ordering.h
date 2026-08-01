#ifndef CHARR_SHARED_COLLATION_ORDERING_H
#define CHARR_SHARED_COLLATION_ORDERING_H

#include "lint.h"
#include "string_view.h"

#include <unicode/ucol.h>

#include <cstddef>
#include <vector>

namespace charr {
namespace shared {

// Mark strings already encountered in the requested traversal direction.
// Native allocation failures propagate as C++ exceptions. ICU failures are
// returned so each backend can preserve its established error message.
CHARR_CXX_HELPER UErrorCode mark_collation_duplicates(
    const StringView* values,
    std::size_t size,
    bool from_last,
    UCollator* collator,
    int* output
);


// Partition the source indices and stable-sort the nonmissing indices.
// Both vectors belong to the caller's Frame and are filled in place.
CHARR_CXX_HELPER UErrorCode build_collation_order(
    const StringView* values,
    std::size_t size,
    bool decreasing,
    UCollator* collator,
    std::vector<int>& order,
    std::vector<int>& missing
);


// Assign increasing minimum ranks through an already sorted nonmissing order.
CHARR_NEUTRAL_HELPER UErrorCode assign_min_collation_ranks(
    const StringView* values,
    UCollator* collator,
    const int* order,
    std::size_t order_size,
    int* output
) noexcept;

} // namespace shared
} // namespace charr

#endif
