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
#include "ci_reader.h"
#include "../shared/native_to_utf8.h"
#include "altrep_backend/io/utf8_output.h"

#include <algorithm>
#include <cstdint>

namespace charr { namespace altrep_backend {


namespace reverse {

struct ReverseInput {
    const char* ptr;
    int len;
    bool ascii;
};

bool has_utf8_bom(const char* ptr, int len) noexcept
{
    return len >= 3 &&
        static_cast<uint8_t>(ptr[0]) == UTF8_BOM_BYTE1 &&
        static_cast<uint8_t>(ptr[1]) == UTF8_BOM_BYTE2 &&
        static_cast<uint8_t>(ptr[2]) == UTF8_BOM_BYTE3;
}

ReverseInput normalize_input(
    const charport::StrView& value,
    charr::shared::NativeToUtf8& converter
)
{
    if (value.enc == cetype_ext_t::CE_ASCII)
        return ReverseInput{value.ptr, value.len, true};
    if (value.enc == cetype_ext_t::CE_BYTES)
        throw StriException(MSG__BYTESENC);

    const char* ptr = value.ptr;
    int len = value.len;
    bool ambiguous = value.enc == cetype_ext_t::CE_ASCII_OR_UTF8;
    if (value.enc == cetype_ext_t::CE_LATIN1 ||
            value.enc == cetype_ext_t::CE_NATIVE) {
        const shared::ByteView converted =
            value.enc == cetype_ext_t::CE_LATIN1
            ? converter.latin1(ptr, len)
            : converter.native(ptr, len);
        ptr = converted.ptr;
        len = converted.len;
        // Conversion establishes valid UTF-8, but not whether the payload is
        // ASCII. Resolve that mark before reserving native charvec storage.
        ambiguous = true;
    }
    else if (value.enc != cetype_ext_t::CE_UTF8 && !ambiguous) {
        throw StriException("unknown charport string encoding");
    }

    if (has_utf8_bom(ptr, len)) {
        ptr += 3;
        len -= 3;
        // Removing the BOM may leave an otherwise ASCII payload.
        ambiguous = true;
    }
    return ReverseInput{
        ptr, len, ambiguous && ci::is_ascii(ptr, static_cast<std::size_t>(len))
    };
}

void reverse_utf8(const ReverseInput& value, char* output)
{
    int32_t source_index = value.len;
    int32_t output_index = 0;
    while (source_index > 0) {
        const uint8_t last_byte = static_cast<uint8_t>(
            value.ptr[source_index - 1]
        );
        if (last_byte < 0x80) {
            output[output_index++] = static_cast<char>(last_byte);
            --source_index;
            continue;
        }

        const int32_t code_point_end = source_index;
        UChar32 code_point;
        U8_PREV(value.ptr, 0, source_index, code_point);
        if (code_point < 0)
            throw StriException(MSG__INVALID_UTF8);

        // U8_PREV has validated this sequence. Copying its original bytes is
        // cheaper than reconstructing the same UTF-8 code point.
        const int32_t code_point_width = code_point_end - source_index;
        switch (code_point_width) {
        case 4:
            output[output_index + 3] = value.ptr[source_index + 3];
            [[fallthrough]];
        case 3:
            output[output_index + 2] = value.ptr[source_index + 2];
            [[fallthrough]];
        case 2:
            output[output_index + 1] = value.ptr[source_index + 1];
            [[fallthrough]];
        default:
            output[output_index] = value.ptr[source_index];
        }
        output_index += code_point_width;
    }
}

} // namespace reverse

using namespace reverse;


/**
 * Reverse Each String
 * @param str character vector
 * @return character vector with every string reversed
 *
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          use io::Utf16Input
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException-friendly + io::Utf8Input (bug fix, do reversing manually)
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-01)
 *          detect incorrect utf8 byte stream
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci_reverse(SEXP str)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));    // prepare string argument

    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP ret;
    {
        ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
        std::shared_ptr<ci::ReaderBorrow> borrow = context.acquire(str);
        const charport::StrViews& values = borrow->views();
        const R_xlen_t str_len = values.size();
        charr::altrep_backend::io::OutputBuilder builder(str_len);
        charr::shared::NativeToUtf8 converter;

        for (R_xlen_t i = 0; i < str_len; ++i) {
            const charport::StrView source = values[i];
            if (source.is_na()) {
                builder.set_na(i);
                continue;
            }

            const ReverseInput value = normalize_input(source, converter);
            char* output = builder.reserve(
                i, static_cast<std::size_t>(value.len),
                value.ascii
                    ? cetype_ext_t::CE_ASCII
                    : cetype_ext_t::CE_UTF8
            );
            if (value.ascii) {
                std::reverse_copy(
                    value.ptr, value.ptr + value.len, output
                );
            }
            else {
                reverse_utf8(value, output);
            }
        }

        borrow.reset();
        STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
            return builder.to_sexp();
        }));
    }

    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}

} } // namespace charr::altrep_backend
