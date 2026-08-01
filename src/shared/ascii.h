#ifndef CHARR_SHARED_ASCII_H
#define CHARR_SHARED_ASCII_H

#include "lint.h"

#include <cstddef>
#include <cstdint>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace charr {
namespace shared {

/*
 * R caches an ASCII bit on every CHARSXP, but only R 4.5.0 and later expose it
 * through Rf_charIsASCII(). Older R needs the answer computed instead, so scan
 * the bytes. Every non-ASCII byte has its high bit set and every ASCII byte has
 * it clear, so one OR over the whole string decides it.
 *
 * The AVX2 block matches charport's check_ascii(). It compiles only when the
 * build already targets AVX2, which an ordinary CRAN build does not, so the
 * portable loop below is what usually ships. That loop accumulates rather than
 * returning early, which keeps it branch-free and lets the compiler vectorize
 * it on its own.
 */
CHARR_NEUTRAL_HELPER inline bool is_ascii(
    const char* data, std::size_t length
) noexcept {
    const std::uint8_t* bytes =
        reinterpret_cast<const std::uint8_t*>(data);
    std::size_t index = 0;

#if defined(__AVX2__)
    if (length >= 32) {
        __m256i wide = _mm256_setzero_si256();
        for (; index + 32 <= length; index += 32) {
            wide = _mm256_or_si256(
                wide,
                _mm256_lddqu_si256(
                    reinterpret_cast<const __m256i*>(bytes + index)
                )
            );
        }
        if (_mm256_movemask_epi8(wide) != 0)
            return false;
    }
    if (length >= index + 16) {
        const __m128i narrow = _mm_lddqu_si128(
            reinterpret_cast<const __m128i*>(bytes + index)
        );
        if (_mm_movemask_epi8(narrow) != 0)
            return false;
        index += 16;
    }
#endif

    std::uint8_t accumulator = 0;
    for (; index < length; ++index)
        accumulator = static_cast<std::uint8_t>(accumulator | bytes[index]);
    return (accumulator & 0x80U) == 0;
}

} // namespace shared
} // namespace charr

#endif
