#ifndef CHARR_SHARED_ASCII_H
#define CHARR_SHARED_ASCII_H

#include "lint.h"

#include <charport/ascii.h>

#include <cstddef>

namespace charr {
namespace shared {

/*
 * The one ASCII scan in charr. The implementation is charport's, an R-free
 * header shared by both optimized backends. Do not add a local byte loop: a
 * per-byte early exit is about 25 times slower on ASCII text.
 *
 * R caches an ASCII bit on every CHARSXP, but only R 4.5.0 and later expose it
 * through Rf_charIsASCII(); older R scans the bytes with this.
 */
CHARR_NEUTRAL_HELPER inline bool is_ascii(
    const char* data, std::size_t length
) noexcept {
    return charport::is_ascii(data, length);
}

// For callers that hold an R length. A nonpositive length is ASCII.
CHARR_NEUTRAL_HELPER inline bool is_ascii(
    const char* data, int length
) noexcept {
    return length <= 0 ||
        charport::is_ascii(data, static_cast<std::size_t>(length));
}

} // namespace shared
} // namespace charr

#endif
