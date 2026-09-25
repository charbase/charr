// Copyright (c) 2026 charr authors
// SPDX-License-Identifier: MIT

#ifndef CHARR_CI_BUILDER_H
#define CHARR_CI_BUILDER_H

#include "ci_exception.h"
#include "ci_macros.h"
#include "io/reader_utils.h"

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace charr { namespace altrep_backend {


namespace ci {


// Resolves to CETYPE_EXT_ASCII or CETYPE_EXT_UTF8 without scanning:
// u_strToUTF8 emits one byte per UTF-16 code unit only when every code point
// is below 0x80.
// Anything higher costs strictly more UTF-8 bytes than UTF-16 code units
// (2 or 3 bytes for one unit, 4 bytes for a surrogate pair), so equal lengths
// is precisely the ASCII case.
CHARR_NEUTRAL_HELPER inline cetype_ext_t utf8_mark_from_lengths(
    int32_t utf16_length, int32_t utf8_length
) noexcept
{
    return utf8_length == utf16_length
        ? CETYPE_EXT_ASCII
        : CETYPE_EXT_UTF8;
}


CHARR_CXX_HELPER inline const char* unicode_to_utf8(
    const UnicodeString& value, std::vector<char>& utf8_buffer,
    int32_t& utf8_length, cetype_ext_t& utf8_mark
)
{
    const int32_t utf16_length = value.length();
    const int32_t max_length = std::numeric_limits<int32_t>::max();
    if (utf16_length > max_length/3-10)
        throw std::length_error("UTF-8 output exceeds ICU's length limit");

    utf8_length = 0;
    if (utf16_length == 0) {
        // Deviation from stringi: Builder treats a null pointer as NA, while
        // vector::data() may be null for an empty buffer under C++11.
        utf8_buffer.clear();
        utf8_mark = CETYPE_EXT_ASCII;
        return "";
    }

    const size_t capacity = static_cast<size_t>(
        UCNV_GET_MAX_BYTES_FOR_STRING(utf16_length, 3)
    );
    // Grow-only, matching String8buf::resize. vector::resize down and then up
    // again value-initializes the re-exposed range, so shrinking between
    // elements would memset the buffer on every subsequent long string.
    if (utf8_buffer.size() < capacity)
        utf8_buffer.resize(capacity);
    UErrorCode status = U_ZERO_ERROR;
    u_strToUTF8(
        utf8_buffer.data(), static_cast<int32_t>(capacity), &utf8_length,
        value.getBuffer(), utf16_length, &status
    );
    if (U_FAILURE(status))
        throw StriException(status);

    utf8_mark = utf8_mark_from_lengths(utf16_length, utf8_length);
    return utf8_buffer.data();
}


} // namespace ci


} } // namespace charr::altrep_backend

#endif
