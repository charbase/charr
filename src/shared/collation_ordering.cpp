// Derived from stringi.
// Copyright (c) 2013-2025, Marek Gagolewski. See inst/COPYRIGHTS.

#include "collation_ordering.h"

#include <unicode/uiter.h>

#include <algorithm>
#include <cstring>

namespace charr {
namespace shared {

namespace collation_ordering {

const int32_t prefix_bytes = 8;


// Three-way collation comparison of two computed entries. Different prefixes
// decide alone; equal complete prefixes are equal keys. Otherwise identical
// bytes are equal without asking ICU, and ucol_strcollUTF8 decides the rest.
CHARR_NEUTRAL_HELPER int compare_entries(
    const StringView* values,
    const UCollator* collator,
    const CollationPrefix& first,
    const CollationPrefix& second,
    UErrorCode& status
) noexcept
{
    if (first.prefix != second.prefix)
        return first.prefix < second.prefix ? -1 : 1;
    if (first.complete && second.complete)
        return 0;

    const StringView& a = values[static_cast<std::size_t>(first.index)];
    const StringView& b = values[static_cast<std::size_t>(second.index)];
    if (a.len == b.len &&
            (a.len == 0 || std::memcmp(a.ptr, b.ptr,
                static_cast<std::size_t>(a.len)) == 0)) {
        return 0;
    }

    const UCollationResult result = ucol_strcollUTF8(
        collator, a.ptr, a.len, b.ptr, b.len, &status
    );
    if (result == UCOL_EQUAL)
        return 0;
    return result == UCOL_LESS ? -1 : 1;
}


// Collation order in the requested direction, then source index ascending,
// which is the order std::stable_sort gave by collation alone in either
// direction. The index makes it a total order. std::stable_sort is still the
// sort used: a merge sort makes fewer comparisons than introsort, and a
// comparison whose prefixes tie costs a full collation.
struct EntryLess {
    const StringView* values;
    const UCollator* collator;
    bool decreasing;

    CHARR_CXX_HELPER bool operator()(
        const CollationPrefix& first, const CollationPrefix& second
    ) const
    {
        UErrorCode status = U_ZERO_ERROR;
        const int result = compare_entries(
            values, collator, first, second, status
        );
        if (U_FAILURE(status))
            throw status;
        if (result != 0)
            return decreasing ? result > 0 : result < 0;
        return first.index < second.index;
    }
};


// Mismatching entries an incomplete element may pass while probing the
// deduplication table before it leaves its string to the sorting pass.
const std::size_t probe_limit = 8;


// Hash input for an incomplete element besides its prefix: the length and
// the middle and last 8 bytes. A fixed cost whatever the string's length.
CHARR_NEUTRAL_HELPER std::uint64_t sample_bytes(
    const StringView& value
) noexcept
{
    const std::size_t size = static_cast<std::size_t>(value.len);
    std::uint64_t middle = 0;
    std::uint64_t tail = 0;
    if (size >= sizeof(tail)) {
        std::memcpy(&middle, value.ptr + (size-sizeof(tail))/2, sizeof(tail));
        std::memcpy(&tail, value.ptr + (size-sizeof(tail)), sizeof(tail));
    }
    else if (size > 0) {
        std::memcpy(&tail, value.ptr, size);
    }
    return mix_hash64(mix_hash64(tail ^ size) ^ middle);
}


// The first deduplication pass's equality: equal complete prefixes, or
// identical bytes. Either way the two elements collate equal.
CHARR_NEUTRAL_HELPER bool same_entry(
    const StringView* values,
    const CollationPrefix& first,
    const CollationPrefix& second
) noexcept
{
    if (first.prefix != second.prefix || first.complete != second.complete)
        return false;
    if (first.complete)
        return true;

    const StringView& a = values[static_cast<std::size_t>(first.index)];
    const StringView& b = values[static_cast<std::size_t>(second.index)];
    return a.len == b.len && (a.len == 0 ||
        std::memcmp(a.ptr, b.ptr, static_cast<std::size_t>(a.len)) == 0);
}

} // namespace collation_ordering

using namespace collation_ordering;


UErrorCode compute_collation_prefixes(
    const StringView* values,
    std::size_t begin,
    std::size_t end,
    const UCollator* collator,
    CollationPrefix* keys
) noexcept
{
    // With the case level on, ICU's sort keys can order two strings the
    // opposite way from ucol_strcollUTF8: with uppercase first,
    // ".../index.html" and ".../Index.html" (sharing a 35-character prefix)
    // collate +1 while their keys compare -1, in ICU 74.1 and 78.2 alike. No
    // prefix is then trusted. Every entry stays an incomplete zero prefix, so
    // every comparison collates and deduplication falls to its byte-identical
    // and sorting passes.
    UErrorCode attribute_status = U_ZERO_ERROR;
    const UColAttributeValue case_level = ucol_getAttribute(
        collator, UCOL_CASE_LEVEL, &attribute_status
    );
    if (U_FAILURE(attribute_status))
        return attribute_status;
    const bool use_prefix = case_level != UCOL_ON;

    for (std::size_t i = begin; i < end; ++i) {
        CollationPrefix& key = keys[i];
        key.prefix = 0;
        key.index = static_cast<std::int32_t>(i);
        key.complete = false;
        if (!use_prefix || values[i].is_na())
            continue;

        // One call with a zeroed state reads only as much of the string as
        // the first 8 key bytes need. Ill-formed UTF-8 is read as U+FFFD, as
        // ucol_strcollUTF8 reads it.
        UCharIterator iterator;
        uiter_setUTF8(
            &iterator, values[i].ptr != nullptr ? values[i].ptr : "",
            values[i].len
        );
        uint32_t state[2] = {0, 0};
        uint8_t bytes[prefix_bytes] = {0, 0, 0, 0, 0, 0, 0, 0};
        UErrorCode status = U_ZERO_ERROR;
        const int32_t written = ucol_nextSortKeyPart(
            collator, &iterator, state, bytes, prefix_bytes, &status
        );
        if (U_FAILURE(status))
            return status;

        std::uint64_t prefix = 0;
        for (int32_t j = 0; j < prefix_bytes; ++j)
            prefix = (prefix << 8) | bytes[j];
        key.prefix = prefix;
        key.complete = written < prefix_bytes;
    }

    return U_ZERO_ERROR;
}


/*
 * Deduplication runs in two passes; marks are written by source index.
 *
 * The first pass walks the elements in traversal order through a hash table.
 * A complete prefix can equal only another complete prefix, and equal
 * complete prefixes are equal keys, so complete elements are deduplicated
 * there exactly, with no ICU call. An incomplete element is marked only when
 * an earlier element has the same bytes, which always collate equal; this
 * takes the bulk of a vector with heavy duplication out of the second pass.
 * Its hash samples a few bytes rather than reading the whole string, so an
 * incomplete element stops probing after a few mismatches and is simply left
 * to the second pass.
 *
 * The second pass sorts the incomplete elements still unmarked by collation
 * and then source index; each run of equal entries keeps its first (or, from
 * the last, its final) source index. Sorting has no bad case when many
 * strings share a long prefix, which hashing the prefix would. The element a
 * run keeps is the first of its class in traversal order, which nothing
 * before it can have marked.
 */
UErrorCode mark_collation_duplicates(
    const StringView* values,
    std::size_t size,
    bool from_last,
    const UCollator* collator,
    std::vector<CollationPrefix>& keys,
    IndexHashTable& table,
    int* output
)
{
    try {
        std::size_t present_size = 0;
        for (std::size_t i = 0; i < size; ++i) {
            if (!values[i].is_na())
                ++present_size;
        }
        table.reset(present_size);

        bool missing_seen = false;
        for (std::size_t step = 0; step < size; ++step) {
            const std::size_t index = from_last ? size-1-step : step;

            if (values[index].is_na()) {
                output[index] = missing_seen ? 1 : 0;
                missing_seen = true;
                continue;
            }

            const CollationPrefix& key = keys[index];
            const std::uint64_t hash = key.complete
                ? mix_hash64(key.prefix)
                : mix_hash64(key.prefix ^ sample_bytes(values[index]));
            std::size_t mismatches = 0;
            output[index] = 0;
            for (std::size_t slot = table.first_slot(hash); ;
                    slot = table.next_slot(slot)) {
                const std::int32_t held = table.at(slot);
                if (held < 0) {
                    table.store(slot, static_cast<std::int32_t>(index));
                    break;
                }
                if (same_entry(values, keys[static_cast<std::size_t>(held)],
                        key)) {
                    output[index] = 1;
                    break;
                }
                if (!key.complete && ++mismatches == probe_limit)
                    break;
            }
        }

        std::size_t incomplete_size = 0;
        for (std::size_t i = 0; i < size; ++i) {
            if (!values[i].is_na() && !keys[i].complete && output[i] == 0)
                keys[incomplete_size++] = keys[i];
        }
        keys.resize(incomplete_size);
        std::stable_sort(
            keys.begin(), keys.end(), EntryLess{values, collator, false}
        );

        std::size_t run_begin = 0;
        for (std::size_t i = 1; i <= incomplete_size; ++i) {
            if (i < incomplete_size) {
                UErrorCode status = U_ZERO_ERROR;
                const int result = compare_entries(
                    values, collator, keys[i-1], keys[i], status
                );
                if (U_FAILURE(status))
                    return status;
                if (result == 0)
                    continue;
            }

            const std::size_t kept = from_last ? i-1 : run_begin;
            for (std::size_t j = run_begin; j < i; ++j) {
                output[static_cast<std::size_t>(keys[j].index)] =
                    j == kept ? 0 : 1;
            }
            run_begin = i;
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
    const UCollator* collator,
    std::vector<CollationPrefix>& keys,
    std::vector<int>& order,
    std::vector<int>& missing
)
{
    try {
        missing.clear();

        std::size_t order_size = 0;
        for (std::size_t i = 0; i < size; ++i) {
            if (values[i].is_na())
                missing.push_back(static_cast<int>(i));
            else
                keys[order_size++] = keys[i];
        }
        keys.resize(order_size);

        std::stable_sort(
            keys.begin(), keys.end(),
            EntryLess{values, collator, decreasing}
        );

        order.resize(order_size);
        for (std::size_t i = 0; i < order_size; ++i)
            order[i] = static_cast<int>(keys[i].index);
    }
    catch (UErrorCode status) {
        return status;
    }

    return U_ZERO_ERROR;
}


UErrorCode assign_min_collation_ranks(
    const StringView* values,
    const UCollator* collator,
    const CollationPrefix* keys,
    std::size_t size,
    int* output
) noexcept
{
    int current_rank = 1;
    for (std::size_t i = 0; i < size; ++i) {
        if (i > 0) {
            UErrorCode status = U_ZERO_ERROR;
            const int result = compare_entries(
                values, collator, keys[i-1], keys[i], status
            );
            if (U_FAILURE(status))
                return status;
            if (result != 0)
                current_rank = static_cast<int>(i)+1;
        }

        output[keys[i].index] = current_rank;
    }

    return U_ZERO_ERROR;
}

} // namespace shared
} // namespace charr
