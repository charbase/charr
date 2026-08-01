#ifndef CHARR_SHARED_REPEAT_H
#define CHARR_SHARED_REPEAT_H

#include "lint.h"

#include <cstddef>

namespace charr {
namespace shared {

// Compute the byte length of a repeated string without overflowing the
// caller's output limit. Nonpositive counts and empty inputs have length zero.
CHARR_NEUTRAL_HELPER bool checked_repeat_size(
    std::size_t source_length,
    int times,
    std::size_t limit,
    std::size_t& output_length
) noexcept;

// Fill an already-sized output region by repeatedly doubling the initialized
// prefix. The source and destination regions must not overlap.
CHARR_NEUTRAL_HELPER void repeat_bytes(
    char* destination,
    const char* source,
    std::size_t source_length,
    std::size_t output_length
) noexcept;

} // namespace shared
} // namespace charr

#endif
