// Derived from stringi.
// Copyright (c) 2013-2025, Marek Gagolewski. See inst/COPYRIGHTS.

#include "utf8.h"

#include "native_to_utf8.h"
#include "slice_arena.h"

#include <cstddef>
#include <cstring>
#include <stdexcept>

#include <unicode/utf8.h>

namespace charr {
namespace shared {

namespace utf8 {

const char empty = '\0';

CHARR_NEUTRAL_HELPER bool has_bom(
    const char* data, int length
) noexcept {
    return length >= 3 &&
        static_cast<unsigned char>(data[0]) == 0xef &&
        static_cast<unsigned char>(data[1]) == 0xbb &&
        static_cast<unsigned char>(data[2]) == 0xbf;
}

} // namespace utf8

CHARR_CXX_HELPER StringView normalize_utf8_transient_slow(
    const StringView& source,
    NativeToUtf8& converter
)
{
    if (source.is_na())
        return source;
    if (source.ptr == nullptr || source.len < 0)
        throw std::runtime_error("invalid string view");
    if (source.enc == StringEncoding::bytes) {
        throw std::runtime_error(
            "bytes encoding is not supported by this function"
        );
    }

    const char* data = source.ptr;
    int length = source.len;
    StringEncoding encoding = StringEncoding::utf8;

    switch (source.enc) {
    case StringEncoding::ascii:
        return source;
    case StringEncoding::utf8:
    case StringEncoding::ascii_or_utf8:
        if (utf8::has_bom(data, length)) {
            data += 3;
            length -= 3;
        }
        else if (source.enc == StringEncoding::ascii_or_utf8) {
            encoding = StringEncoding::ascii_or_utf8;
        }
        break;
    case StringEncoding::latin1: {
        const ByteView converted = converter.latin1(data, length);
        if (converted.len < 0 ||
                (converted.ptr == nullptr && converted.len > 0)) {
            throw std::runtime_error(
                "encoding conversion returned invalid bytes"
            );
        }
        data = converted.ptr;
        length = converted.len;
        break;
    }
    case StringEncoding::native: {
        if (converter.native_is_utf8()) {
            if (utf8::has_bom(data, length)) {
                data += 3;
                length -= 3;
            }
            break;
        }

        const ByteView converted = converter.native(data, length);
        if (converted.len < 0 ||
                (converted.ptr == nullptr && converted.len > 0)) {
            throw std::runtime_error(
                "encoding conversion returned invalid bytes"
            );
        }
        data = converted.ptr;
        length = converted.len;
        break;
    }
    case StringEncoding::missing:
        throw std::runtime_error(
            "non-missing string view has NA encoding"
        );
    case StringEncoding::bytes:
        break;
    case StringEncoding::unknown:
        throw std::runtime_error("unknown string encoding");
    }

    if (length == 0)
        data = &utf8::empty;
    return StringView{data, length, encoding};
}

CHARR_CXX_HELPER StringView normalize_utf8_slow(
    const StringView& source,
    NativeToUtf8& converter,
    SliceArena& storage,
    bool strip_bom_policy
)
{
    if (source.is_na()) {
        return StringView{
            nullptr, missing_string_length, StringEncoding::missing
        };
    }
    if (source.ptr == nullptr || source.len < 0)
        throw std::runtime_error("invalid string view");
    if (source.enc == StringEncoding::bytes) {
        throw std::runtime_error(
            "bytes encoding is not supported by this function"
        );
    }

    const char* data = source.ptr;
    int length = source.len;
    StringEncoding encoding = StringEncoding::utf8;
    bool strip_bom = false;
    bool converted = false;

    switch (source.enc) {
    case StringEncoding::ascii:
        encoding = StringEncoding::ascii;
        break;
    case StringEncoding::utf8:
        strip_bom = strip_bom_policy && utf8::has_bom(data, length);
        break;
    case StringEncoding::ascii_or_utf8:
        strip_bom = strip_bom_policy && utf8::has_bom(data, length);
        encoding = strip_bom
            ? StringEncoding::utf8
            : StringEncoding::ascii_or_utf8;
        break;
    case StringEncoding::latin1:
    case StringEncoding::native: {
        const bool native = source.enc == StringEncoding::native;
        // A native mark names the active encoding. When that encoding is
        // UTF-8, borrow the bytes just as CE_UTF8 does; conversion is not an
        // extra validation pass.
        if (native && converter.native_is_utf8()) {
            strip_bom = strip_bom_policy && utf8::has_bom(data, length);
            break;
        }

        const ByteView converted_view = native
            ? converter.native(data, length)
            : converter.latin1(data, length);
        if (converted_view.len < 0 ||
                (converted_view.ptr == nullptr && converted_view.len > 0)) {
            throw std::runtime_error(
                "encoding conversion returned invalid bytes"
            );
        }
        data = converted_view.ptr == nullptr
            ? &utf8::empty
            : converted_view.ptr;
        length = converted_view.len;
        converted = true;
        break;
    }
    case StringEncoding::missing:
        throw std::runtime_error(
            "non-missing string view has NA encoding"
        );
    case StringEncoding::bytes:
        break;
    case StringEncoding::unknown:
        throw std::runtime_error("unknown string encoding");
    }

    if (strip_bom) {
        data += 3;
        length -= 3;
    }
    if (converted && length > 0) {
        char* stable = storage.allocate(static_cast<std::size_t>(length));
        std::memcpy(stable, data, static_cast<std::size_t>(length));
        data = stable;
    }
    if (length == 0)
        data = &utf8::empty;

    return StringView{data, length, encoding};
}


CHARR_NEUTRAL_HELPER int utf8_index_forward(
    const StringView& value, int code_points
) noexcept
{
    if (code_points <= 0 || value.len <= 0 || value.ptr == nullptr)
        return 0;
    if (value.enc == StringEncoding::ascii)
        return code_points < value.len ? code_points : value.len;

    int byte_index = 0;
    int current = 0;
    while (current < code_points && byte_index < value.len) {
        U8_FWD_1(
            reinterpret_cast<const uint8_t*>(value.ptr),
            byte_index, value.len
        );
        ++current;
    }
    return byte_index;
}


CHARR_NEUTRAL_HELPER int utf8_index_backward(
    const StringView& value, int code_points
) noexcept
{
    if (value.len <= 0 || value.ptr == nullptr)
        return 0;
    if (code_points <= 0)
        return value.len;
    if (value.enc == StringEncoding::ascii)
        return code_points < value.len ? value.len-code_points : 0;

    int byte_index = value.len;
    int current = 0;
    while (current < code_points && byte_index > 0) {
        U8_BACK_1(
            reinterpret_cast<const uint8_t*>(value.ptr), 0, byte_index
        );
        ++current;
    }
    return byte_index;
}

} // namespace shared
} // namespace charr
