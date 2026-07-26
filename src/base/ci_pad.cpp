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
#include "ci_utf8.h"
#include "ci_container_integer.h"
#include "ci_string8buf.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>


namespace charr { namespace base {

namespace {

// ASCII width is exact without an ICU property lookup, including C0 and DEL.
// Non-ASCII code points retain stringi's context-sensitive width rules.
int ci__pad_width_string(const char* data, int length)
{
    int width = 0;
    UChar32 previous = 0;
    UChar32 current = 0;
    R_len_t i = 0;
    bool reset = true;

    while (i < length) {
        const unsigned char byte = static_cast<unsigned char>(data[i]);
        previous = current;
        if (byte < 0x80) {
            current = byte;
            ++i;
            if (reset)
                reset = false;
            if (byte >= 0x20 && byte != 0x7f)
                ++width;
        }
        else {
            U8_NEXT(data, i, length, current);
            if (current < 0)
                throw StriException(MSG__INVALID_UTF8);
            width += ci__width_char_with_context(current, previous, reset);
        }
    }

    return width;
}


char* ci__pad_repeat(
    char* output, const char* pattern, std::size_t pattern_length,
    R_len_t count
)
{
    if (count <= 0 || pattern_length == 0)
        return output;

    if (pattern_length == 1) {
        std::memset(output, static_cast<unsigned char>(pattern[0]), count);
        return output+count;
    }

    // Seed one copy, then double the initialized prefix instead of copying the
    // same multi-byte pad once per output code point.
    const std::size_t total = pattern_length*static_cast<std::size_t>(count);
    std::memcpy(output, pattern, pattern_length);
    std::size_t copied = pattern_length;
    while (copied < total) {
        const std::size_t chunk = std::min(copied, total-copied);
        std::memcpy(output+copied, output, chunk);
        copied += chunk;
    }
    return output+total;
}

} // namespace


/**
 * Pad a string
 *
 * vectorized over str, length and pad
 * if str or pad or length is NA the result will be NA
 *
 * @param str character vector
 * @param min_length integer vector
 * @param side [internal int]
 * @param pad character vector
 * @param use_length single logical value
 * @return character vector
 *
 * @version 0.1-?? (Bartlomiej Tartanus)
 *
 * @version 0.2-2 (Marek Gagolewski, 2014-04-20)
 *          use ci_error_handler, pad should be a single code point, not byte
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-04-22)
 *    `use_length` arg added,
 *    second argument renamed `width`
*/
SEXP ci_pad(SEXP str, SEXP width, SEXP side, SEXP pad, SEXP use_length)
{
    // this is an internal arg, check manually, error() allowed here
    if (!Rf_isInteger(side) || LENGTH(side) != 1)
        Rf_error(MSG__INCORRECT_INTERNAL_ARG);
    int _side = INTEGER(side)[0];
    if (_side < 0 || _side > 2)
        Rf_error(MSG__INCORRECT_INTERNAL_ARG);

    bool use_length_val = ci__prepare_arg_logical_1_notNA(use_length, "use_length");
    PROTECT(str         = ci__prepare_arg_string(str, "str"));
    PROTECT(width       = ci__prepare_arg_integer(width, "width"));
    PROTECT(pad         = ci__prepare_arg_string(pad, "pad"));

//   side       = ci__prepare_arg_string(side, "side");
//   const char* side_opts[] = {"left", "right", "both", NULL};

    R_len_t str_length     = LENGTH(str);
    R_len_t width_length  = LENGTH(width);
//   R_len_t side_length    = LENGTH(side);
    R_len_t pad_length     = LENGTH(pad);

    R_len_t vectorize_length = ci__recycling_rule(true, 3,
                               str_length, width_length, /*side_length, */ pad_length);

    STRI__ERROR_HANDLER_BEGIN(3)
    Utf8Input str_cont(str, vectorize_length);
    StriContainerInteger  width_cont(width, vectorize_length);
//   Utf8Input side_cont(side, vectorize_length);
    Utf8Input pad_cont(pad, vectorize_length);

    SEXP ret;
    STRI__PROTECT(ret = Rf_allocVector(STRSXP, vectorize_length));

    const bool scalar_pad = pad_length == 1;
    bool scalar_pad_validated = false;
    R_len_t scalar_pad_n = 0;
    const char* scalar_pad_s = NULL;
    bool scalar_pad_ascii = false;

    String8buf buf(0); // TODO: prealloc
    for (R_len_t i=0; i<vectorize_length; ++i) {
        if (str_cont.isNA(i) || pad_cont.isNA(i)
                || /*side_cont.isNA(i) ||*/ width_cont.isNA(i)) {
            SET_STRING_ELT(ret, i, NA_STRING);
            continue;
        }

        // get the current string
        const Utf8Record& str_cur = str_cont.get(i);
        R_len_t str_cur_n = str_cur.length();
        const char* str_cur_s = str_cur.data();
        R_len_t str_cur_width;

        // get the width/length of padding code point(s)
        const Utf8Record& pad_cur = pad_cont.get(i);
        R_len_t pad_cur_n = pad_cur.length();
        const char* pad_cur_s = pad_cur.data();
        if (scalar_pad && scalar_pad_validated) {
            pad_cur_n = scalar_pad_n;
            pad_cur_s = scalar_pad_s;
        }
        else {
            if (use_length_val) {
                R_len_t k = 0;
                UChar32 pad_codepoint = 0;
                U8_NEXT(pad_cur_s, k, pad_cur_n, pad_codepoint);
                if (pad_codepoint <= 0 || k < pad_cur_n)
                    throw StriException(MSG__NOT_EQ_N_CODEPOINTS, "pad", 1);
            }
            else if (ci__pad_width_string(pad_cur_s, pad_cur_n) != 1) {
                throw StriException(MSG__NOT_EQ_N_WIDTH, "pad", 1);
            }

            if (scalar_pad) {
                scalar_pad_n = pad_cur_n;
                scalar_pad_s = pad_cur_s;
                scalar_pad_ascii = pad_cur.isASCII();
                scalar_pad_validated = true;
            }
        }

        if (use_length_val)
            str_cur_width = str_cur.countCodePoints();
        else
            str_cur_width = ci__pad_width_string(str_cur_s, str_cur_n);

        // get the minimal width
        R_len_t width_cur = width_cont.get(i);

        if (str_cur_width >= width_cur)  {
            // no padding at all
            SET_STRING_ELT(
                ret, i, Rf_mkCharLenCE(
                    str_cur.data(), str_cur.length(),
                    str_cur.isASCII() ? CE_NATIVE : CE_UTF8
                )
            );
            continue;
        }

        R_len_t padnum = width_cur-str_cur_width;
        const std::size_t pad_bytes = static_cast<std::size_t>(pad_cur_n);
        if (pad_bytes > 0 &&
                static_cast<std::size_t>(padnum) >
                (static_cast<std::size_t>(R_LEN_T_MAX)-
                    static_cast<std::size_t>(str_cur_n))/pad_bytes)
            throw std::length_error("padded string exceeds R's string length limit");
        const R_len_t output_length = str_cur_n+padnum*pad_cur_n;
        buf.resize(output_length, false);

        char* buftmp = buf.data();
        R_len_t left_count = 0;
        switch(_side) {

        case 0: // left
            left_count = padnum;
            break;

        case 1: // right
            break;

        case 2: // both
            left_count = padnum/2;
            break;
        }

        buftmp = ci__pad_repeat(
            buftmp, pad_cur_s, pad_bytes, left_count
        );
        if (str_cur_n > 0) {
            std::memcpy(buftmp, str_cur_s, str_cur_n);
            buftmp += str_cur_n;
        }
        buftmp = ci__pad_repeat(
            buftmp, pad_cur_s, pad_bytes, padnum-left_count
        );

        const bool output_ascii = str_cur.isASCII() &&
            (scalar_pad ? scalar_pad_ascii : pad_cur.isASCII());
        SET_STRING_ELT(
            ret, i,
            Rf_mkCharLenCE(
                buf.data(), static_cast<int>(buftmp-buf.data()),
                output_ascii ? CE_NATIVE : CE_UTF8
            )
        );
    }

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


// // Second version by BT: uses StriContainerUTF16 & ICU's padLeading
//{
//   str    = ci__prepare_arg_string(str, "str"); // prepare string argument
//   length = ci__prepare_arg_integer(length, "length");
//   pad    = ci__prepare_arg_string(pad, "pad");
//
//   R_len_t vectorize_length = ci__recycling_rule(true, 3, LENGTH(str), LENGTH(length), LENGTH(pad));
//
//   SEXP ret;
//   PROTECT(ret = allocVector(STRSXP, vectorize_length));
//
//   STRI__ERROR_HANDLER_BEGIN
//   StriContainerUTF16 str_cont(str, vectorize_length, false);
//   StriContainerUTF16 pad_cont(pad, vectorize_length);
//   StriContainerInteger length_cont(length, vectorize_length);
//
//   for (R_len_t i = 0; i < vectorize_length; i++)
//   {
//      if (pad_cont.isNA(i) || str_cont.isNA(i) || length_cont.isNA(i)) {
//         SET_STRING_ELT(ret, i, NA_STRING);
//         continue;
//      }
//
//      if (pad_cont.get(i).length() > 0) {
//         UChar cur_pad = (pad_cont.get(i))[0]; // This is Uchar - 16 bit.....
//         str_cont.getWritable(i).padLeading(length_cont.get(i), cur_pad);
//      }
//
//      SET_STRING_ELT(ret, i, str_cont.toR(i));
//   }
//
//   UNPROTECT(1);
//   return ret;
//   STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
//}

} } // namespace charr::base
