#ifndef CHARR_SHARED_UTF8_H
#define CHARR_SHARED_UTF8_H

#include "lint.h"
#include "string_view.h"

#include <unicode/utf8.h>

#include <climits>
#include <cstdint>

namespace charr {
namespace shared {

class NativeToUtf8;
class SliceArena;


// ICU reports UTF-8 byte offsets, while R's string locations count Unicode
// code points. This cursor converts a nondecreasing sequence of byte offsets
// in one pass through the string. A target below the previous one restarts
// the walk from the beginning, so an unordered sequence is still converted
// correctly, only without the single-pass bound. A target past the end of
// the string is treated as the end.
class Utf8PositionCursor {
public:
    CHARR_NEUTRAL_HELPER explicit Utf8PositionCursor(
        const StringView& value
    ) noexcept
        : data_(reinterpret_cast<const std::uint8_t*>(value.ptr)),
          length_(value.len), byte_(0), position_(0),
          ascii_(value.enc == StringEncoding::ascii)
    {
    }

    CHARR_NEUTRAL_HELPER int at_byte(int target) noexcept
    {
        if (target > length_)
            target = length_;
        if (target < 0)
            target = 0;
        if (ascii_)
            return target;
        if (target < byte_) {
            byte_ = 0;
            position_ = 0;
        }
        while (byte_ < target) {
            U8_FWD_1(data_, byte_, length_);
            ++position_;
        }
        return position_;
    }

private:
    const std::uint8_t* data_;
    int length_;
    int byte_;
    int position_;
    bool ascii_;
};


// A 0-based code-point index as a 1-based R position. The only index with no
// 1-based counterpart is the end of a string of INT_MAX code points, where an
// empty match can start; R cannot represent the position after it, so that
// one value saturates rather than overflowing.
CHARR_NEUTRAL_HELPER inline int one_based_position(int index) noexcept
{
    return index < INT_MAX ? index+1 : INT_MAX;
}

CHARR_CXX_HELPER StringView normalize_utf8_slow(
    const StringView& source,
    NativeToUtf8& converter,
    SliceArena& storage,
    bool strip_bom
);

CHARR_CXX_HELPER StringView normalize_utf8_transient_slow(
    const StringView& source,
    NativeToUtf8& converter
);

// Converted data remains valid until converter is used again.
CHARR_CXX_HELPER inline StringView normalize_utf8_transient(
    const StringView& source,
    NativeToUtf8& converter
)
{
    if (source.is_na())
        return source;

    if (source.ptr != nullptr && source.len >= 0) {
        if (source.enc == StringEncoding::ascii)
            return source;

        if (source.enc == StringEncoding::utf8 ||
                source.enc == StringEncoding::ascii_or_utf8) {
            const bool has_bom = source.len >= 3 &&
                static_cast<unsigned char>(source.ptr[0]) == 0xefU &&
                static_cast<unsigned char>(source.ptr[1]) == 0xbbU &&
                static_cast<unsigned char>(source.ptr[2]) == 0xbfU;
            if (!has_bom)
                return source;
            return StringView{
                source.ptr+3, source.len-3, StringEncoding::utf8
            };
        }
    }

    return normalize_utf8_transient_slow(source, converter);
}


// Normalize one borrowed string view to UTF-8. The common ASCII and UTF-8
// cases remain an inline borrow; conversion and BOM handling use the slow
// path. The returned view otherwise points into Frame-owned SliceArena
// storage. Missing values stay missing; bytes-marked input is rejected.
CHARR_CXX_HELPER inline StringView normalize_utf8(
    const StringView& source,
    NativeToUtf8& converter,
    SliceArena& storage
)
{
    if (source.is_na())
        return source;

    if (source.ptr != nullptr && source.len >= 0) {
        if (source.enc == StringEncoding::ascii)
            return source;

        if (source.enc == StringEncoding::utf8 ||
                source.enc == StringEncoding::ascii_or_utf8) {
            const bool has_bom = source.len >= 3 &&
                static_cast<unsigned char>(source.ptr[0]) == 0xefU &&
                static_cast<unsigned char>(source.ptr[1]) == 0xbbU &&
                static_cast<unsigned char>(source.ptr[2]) == 0xbfU;
            if (!has_bom)
                return source;
        }
    }

    return normalize_utf8_slow(source, converter, storage, true);
}

// Collation search treats a leading U+FEFF as part of the string. Preserve
// its UTF-8 bytes while applying the same native and Latin-1 conversion as
// normalize_utf8().
CHARR_CXX_HELPER inline StringView normalize_utf8_preserve_bom(
    const StringView& source,
    NativeToUtf8& converter,
    SliceArena& storage
)
{
    if (source.is_na())
        return source;

    if (source.ptr != nullptr && source.len >= 0 &&
            (source.enc == StringEncoding::ascii ||
             source.enc == StringEncoding::utf8 ||
             source.enc == StringEncoding::ascii_or_utf8)) {
        return source;
    }

    return normalize_utf8_slow(source, converter, storage, false);
}

// Convert a code-point distance to a byte index in a normalized UTF-8 view.
// Distances beyond either end clamp to the corresponding string boundary.
CHARR_NEUTRAL_HELPER int utf8_index_forward(
    const StringView& value, int code_points
) noexcept;

CHARR_NEUTRAL_HELPER int utf8_index_backward(
    const StringView& value, int code_points
) noexcept;

} // namespace shared
} // namespace charr

#endif
