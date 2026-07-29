// Copied from stringi 19e9586ba39b3320df49355e32bd18d74ed6098f; stri_* renamed to ci_*. See inst/COPYRIGHTS.
/* This file is part of the 'stringi' project.
 * Copyright (c) 2013-2025, Marek Gagolewski <https://www.gagolewski.com/>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "ci_stringi.h"
#include "io/utf16_input.h"
#include "../shared/native_to_utf8.h"

#include <cstdint>
#include <exception>
#include <string>


namespace charr { namespace base_backend {

namespace escape {

struct EscapeInput {
    const char* ptr;
    int length;
};


bool has_utf8_bom(const char* ptr, int length) noexcept
{
    return length >= 3 &&
        static_cast<uint8_t>(ptr[0]) == UTF8_BOM_BYTE1 &&
        static_cast<uint8_t>(ptr[1]) == UTF8_BOM_BYTE2 &&
        static_cast<uint8_t>(ptr[2]) == UTF8_BOM_BYTE3;
}


EscapeInput normalize_escape_input(SEXP value, shared::NativeToUtf8& converter)
{
    const char* ptr = CHAR(value);
    int length = LENGTH(value);

    if (IS_ASCII(value))
        return EscapeInput{ptr, length};
    if (IS_BYTES(value))
        throw StriException(MSG__BYTESENC);

    bool strip_bom = IS_UTF8(value);
    if (!IS_UTF8(value)) {
        const bool native = !IS_LATIN1(value);
        const shared::ByteView converted = native
            ? converter.native(ptr, length)
            : converter.latin1(ptr, length);
        strip_bom = native && has_utf8_bom(ptr, length);
        ptr = converted.ptr;
        length = converted.len;
    }

    if (strip_bom && has_utf8_bom(ptr, length)) {
        ptr += 3;
        length -= 3;
    }
    return EscapeInput{ptr, length};
}


char short_escape(UChar32 code_point) noexcept
{
    switch (code_point) {
    case 0x07: return 'a';
    case 0x08: return 'b';
    case 0x09: return 't';
    case 0x0a: return 'n';
    case 0x0b: return 'v';
    case 0x0c: return 'f';
    case 0x0d: return 'r';
    case 0x22: return '"';
    case 0x27: return '\'';
    case 0x5c: return '\\';
    default:   return '\0';
    }
}


std::size_t escaped_width(UChar32 code_point) noexcept
{
    if (short_escape(code_point) != '\0')
        return 2;
    if (code_point >= 32 && code_point <= 126)
        return 1;
    return code_point <= 0xffff ? 6 : 10;
}


std::size_t escaped_size(const EscapeInput& input)
{
    std::size_t output_size = 0;
    int32_t cursor = 0;
    while (cursor < input.length) {
        UChar32 code_point;
        const uint8_t lead = static_cast<uint8_t>(input.ptr[cursor]);
        if (lead <= 0x7f) {
            code_point = lead;
            ++cursor;
        }
        else {
            U8_NEXT(input.ptr, cursor, input.length, code_point);
            if (code_point < 0)
                throw StriException(MSG__INVALID_UTF8);
        }

        const std::size_t width = escaped_width(code_point);
        if (output_size >
                static_cast<std::size_t>(R_LEN_T_MAX) - width) {
            throw StriException("escaped string exceeds R's string length limit");
        }
        output_size += width;
    }
    return output_size;
}


char* write_hex(char* output, UChar32 value, int digits) noexcept
{
    static const char hex[] = "0123456789abcdef";
    for (int shift = (digits-1)*4; shift >= 0; shift -= 4)
        *output++ = hex[(static_cast<uint32_t>(value) >> shift) & 0x0fU];
    return output;
}


void write_escape(const EscapeInput& input, char* output) noexcept
{
    int32_t cursor = 0;
    while (cursor < input.length) {
        UChar32 code_point;
        const uint8_t lead = static_cast<uint8_t>(input.ptr[cursor]);
        if (lead <= 0x7f) {
            code_point = lead;
            ++cursor;
        }
        else {
            U8_NEXT_UNSAFE(input.ptr, cursor, code_point);
        }

        const char escaped = short_escape(code_point);
        if (escaped != '\0') {
            *output++ = '\\';
            *output++ = escaped;
        }
        else if (code_point >= 32 && code_point <= 126) {
            *output++ = static_cast<char>(code_point);
        }
        else if (code_point <= 0xffff) {
            *output++ = '\\';
            *output++ = 'u';
            output = write_hex(output, code_point, 4);
        }
        else {
            *output++ = '\\';
            *output++ = 'U';
            output = write_hex(output, code_point, 8);
        }
    }
}

} // namespace escape

using namespace escape;

/**
 *  Escape Unicode code points
 *
 *  @param str character vector
 *  @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-17)
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-01)
 *          fail on incorrect utf8 byte seqs;
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.1.6 (Steve Grubb, 2017-07-20)
 *          if ((char)c >= 32 || (char)c <= 126) should be &&
*/
SEXP ci_escape_unicode(SEXP str)
{
    PROTECT(str = ci__prepare_arg_string(str, "str")); // prepare string argument

    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP ret;
    const R_len_t str_length = LENGTH(str);
    const SEXP* values = str_length > 0 ? STRING_PTR_RO(str) : nullptr;
    STRI__PROTECT(ret = Rf_allocVector(STRSXP, str_length));

    try {
        shared::NativeToUtf8 converter;
        std::string output;
        for (R_len_t i = 0; i < str_length; ++i) {
            if (values[i] == NA_STRING) {
                SET_STRING_ELT(ret, i, NA_STRING);
                continue;
            }

            const EscapeInput input = normalize_escape_input(
                values[i], converter
            );
            const std::size_t output_size = escaped_size(input);
            if (output.size() < output_size)
                output.resize(output_size);
            if (output_size > 0)
                write_escape(input, &output[0]);
            SET_STRING_ELT(
                ret, i,
                Rf_mkCharLenCE(
                    output_size > 0 ? output.data() : "",
                    static_cast<int>(output_size), CE_UTF8
                )
            );
        }
    }
    catch (const StriException&) {
        throw;
    }
    catch (const std::exception& error) {
        throw StriException("%s", error.what());
    }

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


} } // namespace charr::base_backend
