#ifndef CHARR_BASE_UTF8_VIEWS_H
#define CHARR_BASE_UTF8_VIEWS_H

#include "../ci_stringi.h"

#include <unicode/utf8.h>
#include <unicode/uchar.h>

namespace charr {
namespace base_backend {
namespace io {

enum class Utf8RecordState : unsigned char {
    ascii,
    utf8,
    missing
};

struct ByteView {
    const char* ptr;
    R_len_t len;

    const char* data() const noexcept
    {
        return ptr;
    }

    R_len_t length() const noexcept
    {
        return len;
    }
};

struct Utf8Record {
    const char* ptr;
    R_len_t len;
    Utf8RecordState state;

    bool is_na() const noexcept
    {
        return state == Utf8RecordState::missing;
    }

    bool isNA() const noexcept
    {
        return is_na();
    }

    bool isASCII() const noexcept
    {
        return state == Utf8RecordState::ascii;
    }

    bool isUTF8() const noexcept
    {
        return state == Utf8RecordState::utf8;
    }

    const char* data() const
    {
#ifndef NDEBUG
        if (is_na())
            throw StriException("missing UTF-8 record has no payload");
#endif
        return ptr;
    }

    R_len_t length() const
    {
#ifndef NDEBUG
        if (is_na())
            throw StriException("missing UTF-8 record has no length");
#endif
        return len;
    }

    R_len_t countCodePoints() const
    {
        return isASCII() ? len : ci__length_string(data(), len);
    }

    bool endsWith(
        R_len_t byteindex, const char* pattern, R_len_t pattern_length,
        bool case_insensitive
    ) const
    {
        if (!case_insensitive) {
            if (byteindex-pattern_length < 0)
                return false;
            for (R_len_t k = 0; k < pattern_length; ++k) {
                if (ptr[byteindex-k-1] != pattern[pattern_length-k-1])
                    return false;
            }
            return true;
        }

        R_len_t pattern_index = pattern_length;
        UChar32 value_cp;
        UChar32 pattern_cp;
        while (pattern_index > 0) {
            if (byteindex <= 0)
                return false;
            U8_PREV(ptr, 0, byteindex, value_cp);
            U8_PREV(pattern, 0, pattern_index, pattern_cp);
            if (u_toupper(value_cp) != u_toupper(pattern_cp))
                return false;
        }
        return true;
    }

    bool startsWith(
        R_len_t byteindex, const char* pattern, R_len_t pattern_length,
        bool case_insensitive
    ) const
    {
        if (!case_insensitive) {
            if (byteindex+pattern_length > len)
                return false;
            for (R_len_t k = 0; k < pattern_length; ++k) {
                if (ptr[byteindex+k] != pattern[k])
                    return false;
            }
            return true;
        }

        R_len_t pattern_index = 0;
        UChar32 value_cp;
        UChar32 pattern_cp;
        while (pattern_index < pattern_length) {
            if (byteindex >= len)
                return false;
            U8_NEXT(ptr, byteindex, len, value_cp);
            U8_NEXT(pattern, pattern_index, pattern_length, pattern_cp);
            if (u_toupper(value_cp) != u_toupper(pattern_cp))
                return false;
        }
        return true;
    }
};

} // namespace io
} // namespace base_backend
} // namespace charr

#endif
