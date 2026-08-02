// Derived from stringi.
// Copyright (c) 2013-2025, Marek Gagolewski. See inst/COPYRIGHTS.

#include "collation_ordering.h"

#include <algorithm>
#include <set>
#include <utility>

namespace charr {
namespace shared {

namespace collation_ordering {

struct IndexLess {
    const StringView* values;
    UCollator* collator;
    bool decreasing;

    CHARR_CXX_HELPER bool operator()(
        std::size_t first, std::size_t second
    ) const
    {
        UErrorCode status = U_ZERO_ERROR;
        const UCollationResult result = ucol_strcollUTF8(
            collator,
            values[first].ptr, values[first].len,
            values[second].ptr, values[second].len,
            &status
        );
        if (U_FAILURE(status))
            throw status;
        return decreasing ? result > 0 : result < 0;
    }
};

} // namespace collation_ordering

UErrorCode mark_collation_duplicates(
    const StringView* values,
    std::size_t size,
    bool from_last,
    UCollator* collator,
    int* output
)
{
    try {
        typedef collation_ordering::IndexLess IndexLess;
        std::set<std::size_t, IndexLess> unique(
            IndexLess{values, collator, true}
        );
        bool missing_seen = false;

        for (std::size_t step = 0; step < size; ++step) {
            const std::size_t index = from_last ? size-1-step : step;

            if (values[index].is_na()) {
                output[index] = missing_seen ? 1 : 0;
                missing_seen = true;
                continue;
            }

            const std::pair<
                std::set<std::size_t, IndexLess>::iterator,
                bool
            > inserted = unique.insert(index);
            output[index] = inserted.second ? 0 : 1;
        }
    }
    catch (UErrorCode status) {
        return status;
    }

    return U_ZERO_ERROR;
}


UErrorCode build_collation_order(
    const StringView* values,
    std::size_t size,
    bool decreasing,
    UCollator* collator,
    std::vector<int>& order,
    std::vector<int>& missing
)
{
    try {
        order.resize(size);
        missing.clear();

        std::size_t order_size = 0;
        for (std::size_t i = 0; i < size; ++i) {
            if (values[i].is_na())
                missing.push_back(static_cast<int>(i));
            else
                order[order_size++] = static_cast<int>(i);
        }
        order.resize(order_size);

        typedef collation_ordering::IndexLess IndexLess;
        std::stable_sort(
            order.begin(), order.end(),
            IndexLess{values, collator, decreasing}
        );
    }
    catch (UErrorCode status) {
        return status;
    }

    return U_ZERO_ERROR;
}


UErrorCode assign_min_collation_ranks(
    const StringView* values,
    UCollator* collator,
    const int* order,
    std::size_t order_size,
    int* output
) noexcept
{
    int current_rank = 1;
    for (std::size_t i = 0; i < order_size; ++i) {
        if (i > 0) {
            UErrorCode status = U_ZERO_ERROR;
            const UCollationResult result = ucol_strcollUTF8(
                collator,
                values[static_cast<std::size_t>(order[i-1])].ptr,
                values[static_cast<std::size_t>(order[i-1])].len,
                values[static_cast<std::size_t>(order[i])].ptr,
                values[static_cast<std::size_t>(order[i])].len,
                &status
            );
            if (U_FAILURE(status))
                return status;
            if (result != UCOL_EQUAL)
                current_rank = static_cast<int>(i)+1;
        }

        output[order[i]] = current_rank;
    }

    return U_ZERO_ERROR;
}

} // namespace shared
} // namespace charr
