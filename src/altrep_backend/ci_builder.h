// Copyright (c) 2026 charr authors
// SPDX-License-Identifier: MIT

#ifndef CHARR_CI_BUILDER_H
#define CHARR_CI_BUILDER_H

#include "ci_reader.h"
#include "ci_string8.h"

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>


namespace ci {


inline cetype_ext_t output_encoding(
    const char* data, size_t length, cetype_ext_t preferred
)
{
    if (preferred == cetype_ext_t::CE_NA || data == NULL)
        return cetype_ext_t::CE_NA;

    // Deviation from stringi: reject an overlong lazy record before either
    // scanning it or narrowing its size to R's character-string limit.
    if (length > static_cast<size_t>(R_LEN_T_MAX))
        throw std::length_error("character output exceeds R's string length limit");

    // Deviation from stringi: CE_ASCII_OR_UTF8 deliberately leaves the mark
    // unresolved until the payload is exposed. Explicit marks are trusted.
    if (preferred != cetype_ext_t::CE_ASCII_OR_UTF8)
        return preferred;
    return is_ascii(data, length)
        ? cetype_ext_t::CE_ASCII
        : cetype_ext_t::CE_UTF8;
}


inline charport::charvec::Store scalar_store(
    const char* data, size_t length, cetype_ext_t preferred
)
{
    // Deviation from stringi: C++11 permits data() to be null for empty
    // strings and vectors, while Store::scalar reserves null for NA.
    if (data == NULL && length == 0 && preferred != cetype_ext_t::CE_NA)
        data = "";
    return charport::charvec::Store::scalar(
        data, length, output_encoding(data, length, preferred)
    );
}


inline charport::charvec::Store scalar_store(const String8& value)
{
    if (value.isNA())
        return charport::charvec::Store::scalar(
            NULL, 0, cetype_ext_t::CE_NA
        );
    return scalar_store(
        value.data(), value.length(),
        value.isASCII()
            ? cetype_ext_t::CE_ASCII
            : cetype_ext_t::CE_UTF8
    );
}


inline charport::charvec::Store scalar_store(
    const std::string& value, cetype_ext_t preferred
)
{
    return scalar_store(value.data(), value.size(), preferred);
}


inline void builder_set(
    charport::charvec::Builder& builder, R_xlen_t i,
    const char* data, size_t length, cetype_ext_t preferred
)
{
    // Deviation from stringi: C++11 permits data() to be null for an empty
    // vector. Builder reserves a null pointer for NA, so canonicalize only
    // the empty, non-missing case at this common length-delimited boundary.
    if (data == NULL && length == 0 && preferred != cetype_ext_t::CE_NA)
        data = "";
    builder.set(
        i, data, length,
        output_encoding(data, length, preferred)
    );
}


inline void builder_set(
    charport::charvec::Builder& builder, R_xlen_t i,
    const std::string& value, cetype_ext_t preferred
)
{
    // Deviation from stringi: C++11 permits data() to be null for an empty
    // string, while Builder reserves a null pointer for NA.
    const char* data = value.empty() ? "" : value.data();
    builder_set(builder, i, data, value.size(), preferred);
}


inline void builder_set(
    charport::charvec::Builder& builder, R_xlen_t i,
    const String8& value
)
{
    if (value.isNA()) {
        builder.set_na(i);
        return;
    }
    builder_set(
        builder, i, value.data(), value.length(),
        value.isASCII()
            ? cetype_ext_t::CE_ASCII
            : cetype_ext_t::CE_UTF8
    );
}


inline const char* unicode_to_utf8(
    const UnicodeString& value, std::vector<char>& utf8_buffer,
    int32_t& utf8_length
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
        return "";
    }

    const int32_t capacity = UCNV_GET_MAX_BYTES_FOR_STRING(utf16_length, 3);
    utf8_buffer.resize(static_cast<size_t>(capacity));
    UErrorCode status = U_ZERO_ERROR;
    u_strToUTF8(
        utf8_buffer.data(), capacity, &utf8_length,
        value.getBuffer(), utf16_length, &status
    );
    if (U_FAILURE(status))
        throw StriException(status);

    return utf8_buffer.data();
}


inline charport::charvec::Store scalar_store(
    const UnicodeString& value, std::vector<char>& utf8_buffer
)
{
    if (value.isBogus())
        return charport::charvec::Store::scalar(
            NULL, 0, cetype_ext_t::CE_NA
        );

    int32_t utf8_length = 0;
    const char* utf8 = unicode_to_utf8(value, utf8_buffer, utf8_length);
    return scalar_store(
        utf8, static_cast<size_t>(utf8_length),
        cetype_ext_t::CE_ASCII_OR_UTF8
    );
}


inline void builder_set(
    charport::charvec::Builder& builder, R_xlen_t i,
    const UnicodeString& value, std::vector<char>& utf8_buffer
)
{
    if (value.isBogus()) {
        builder.set_na(i);
        return;
    }

    // StriContainerUTF16::toR used one reusable conversion buffer. Keep that
    // behavior while sending the length-delimited UTF-8 result to Builder.
    int32_t utf8_length = 0;
    const char* utf8 = unicode_to_utf8(value, utf8_buffer, utf8_length);
    builder_set(
        builder, i, utf8, utf8_length,
        cetype_ext_t::CE_ASCII_OR_UTF8
    );
}


inline void builder_append(
    charport::charvec::GrowableBuilder& builder,
    const char* data, size_t length, cetype_ext_t preferred
)
{
    // See builder_set(): a null empty C++11 buffer is still an empty string.
    if (data == NULL && length == 0 && preferred != cetype_ext_t::CE_NA)
        data = "";
    builder.append(
        data, length,
        output_encoding(data, length, preferred)
    );
}


inline void builder_append(
    charport::charvec::GrowableBuilder& builder,
    const std::string& value, cetype_ext_t preferred
)
{
    // Keep an empty record distinct from NA under C++11.
    const char* data = value.empty() ? "" : value.data();
    builder_append(builder, data, value.size(), preferred);
}


inline void builder_append(
    charport::charvec::GrowableBuilder& builder, const String8& value
)
{
    if (value.isNA()) {
        builder.append(NULL, 0, cetype_ext_t::CE_NA);
        return;
    }
    builder_append(
        builder, value.data(), value.length(),
        value.isASCII()
            ? cetype_ext_t::CE_ASCII
            : cetype_ext_t::CE_UTF8
    );
}


inline void builder_append(
    charport::charvec::GrowableBuilder& builder,
    const UnicodeString& value, std::vector<char>& utf8_buffer
)
{
    if (value.isBogus()) {
        builder.append(NULL, 0, cetype_ext_t::CE_NA);
        return;
    }

    int32_t utf8_length = 0;
    const char* utf8 = unicode_to_utf8(value, utf8_buffer, utf8_length);
    builder_append(
        builder, utf8, utf8_length, cetype_ext_t::CE_ASCII_OR_UTF8
    );
}


} // namespace ci

#endif
