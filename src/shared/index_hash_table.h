#ifndef CHARR_SHARED_INDEX_HASH_TABLE_H
#define CHARR_SHARED_INDEX_HASH_TABLE_H

#include "lint.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace charr {
namespace shared {

/*
 * An insert-only open-addressing table of element indices. Slots are a flat
 * int32 array, the capacity is a power of two at least twice the number of
 * insertions, and collisions probe linearly. The table stores no keys: the
 * caller keeps them in its own array, hashes them, and decides equality while
 * it walks the probe sequence:
 *
 *     for (std::size_t slot = table.first_slot(hash); ;
 *             slot = table.next_slot(slot)) {
 *         const std::int32_t held = table.at(slot);
 *         if (held < 0) { table.store(slot, index); break; }  // new key
 *         if (equal(held, index)) break;                      // seen key
 *     }
 *
 * With at most half the slots used an empty slot always ends the walk.
 */
class CHARR_OWNER_TYPE IndexHashTable {
public:
    CHARR_CXX_HELPER IndexHashTable() noexcept : slots_(), mask_(0)
    {
    }

    IndexHashTable(const IndexHashTable&) = delete;
    IndexHashTable& operator=(const IndexHashTable&) = delete;
    IndexHashTable(IndexHashTable&&) = delete;
    IndexHashTable& operator=(IndexHashTable&&) = delete;

    // Empty the table and size it for up to `count` insertions.
    CHARR_CXX_HELPER void reset(std::size_t count)
    {
        std::size_t capacity = 1;
        while (capacity < 2*count) {
            if (capacity > slots_.max_size()/2)
                throw std::length_error("index hash table is too large");
            capacity *= 2;
        }
        slots_.assign(capacity, empty_slot);
        mask_ = capacity-1;
    }

    CHARR_NEUTRAL_HELPER std::size_t first_slot(
        std::uint64_t hash
    ) const noexcept
    {
        return static_cast<std::size_t>(hash) & mask_;
    }

    CHARR_NEUTRAL_HELPER std::size_t next_slot(
        std::size_t slot
    ) const noexcept
    {
        return (slot+1) & mask_;
    }

    // The index held in `slot`, or a negative value when it is empty.
    CHARR_NEUTRAL_HELPER std::int32_t at(std::size_t slot) const noexcept
    {
        return slots_[slot];
    }

    CHARR_NEUTRAL_HELPER void store(
        std::size_t slot, std::int32_t index
    ) noexcept
    {
        slots_[slot] = index;
    }

private:
    static constexpr std::int32_t empty_slot = -1;

    std::vector<std::int32_t> slots_;
    std::size_t mask_;
};


// A 64-bit finalizer (splitmix64) that spreads every input bit over the low
// bits the table masks with, so keys differing only in high bytes do not
// collide.
CHARR_NEUTRAL_HELPER inline std::uint64_t mix_hash64(
    std::uint64_t value
) noexcept
{
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return value;
}

} // namespace shared
} // namespace charr

#endif
