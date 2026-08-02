
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
#include "io/reader_utils.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/unwind.h"

#include <clocale>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace charr { namespace altrep_backend {


namespace length {

CHARR_NEUTRAL_HELPER bool ci__length_utf8_fast(
    const char* str, int n, int& length
) noexcept
{
    std::int32_t i = 0;
    length = 0;
    while (i < n) {
        UChar32 code_point;
        U8_NEXT(str, i, n, code_point);
        if (code_point < 0)
            return false;
        ++length;
    }
    return true;
}




CHARR_NEUTRAL_HELPER inline int ci__ascii_char_width(
    unsigned char value
) noexcept
{
    return value >= 0x20 && value != 0x7f ? 1 : 0;
}


CHARR_NEUTRAL_HELPER int ci__ascii_string_width(
    const char* data, int length
) noexcept
{
    int width = 0;
    for (int i = 0; i < length; ++i)
        width += ci__ascii_char_width(
            static_cast<unsigned char>(data[i])
        );
    return width;
}

} // namespace length

using namespace length;


/**
 * Count the number of characters/code points in a string
 *
 * Note that ICU permits only strings of length < 2^31.
 *
 * @param s character vector
 * @return integer vector
 *
 * @version 0.1-?? (Marcin Bujarski)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          Multiple input encoding support
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException-friendly
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-27)
 *          using StriUcnv;
 *          warn on invalid UTF-8 sequences
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-05-22)
 *    use ci__length_string for UTF-8
 */
CHARR_ENTRYPOINT SEXP ci_length(SEXP str) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );

    try {
        charport::Reader reader;
        charport::StrViews views;
        shared::NativeToUtf8 native_to_utf8;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                reader.reset(str);
                const R_len_t str_n = io::checked_r_len(
                    reader.size(), "character vectors"
                );

                result = entry_protections.reprotect_one(
                    Rf_allocVector(INTSXP, str_n), result_index
                );
                int* retint = INTEGER(result);

                if (str_n > 0) {
                    views.resize(str_n);
                    reader.views(
                        0, str_n,
                        views.ptrs(), views.lengths(), views.encodings()
                    );
                    const char* const* ptrs = views.ptrs();
                    const int* lengths = views.lengths();
                    const cetype_ext_t* encodings = views.encodings();

                    for (R_len_t k = 0; k < str_n; ++k) {
                        const cetype_ext_t encoding = encodings[k];
                        if (encoding == CETYPE_EXT_NA) {
                            retint[k] = NA_INTEGER;
                            continue;
                        }

                        const char* curs_s = ptrs[k];
                        const int curs_n = lengths[k];
                        if (encoding == CETYPE_EXT_ASCII ||
                                encoding == CETYPE_EXT_LATIN1) {
                            retint[k] = curs_n;
                        }
                        else if (encoding == CETYPE_EXT_BYTES) {
                            throw StriException(MSG__BYTESENC);
                        }
                        else if (encoding == CETYPE_EXT_UTF8 ||
                                encoding ==
                                    CETYPE_EXT_ASCII_OR_UTF8 ||
                                (encoding == CETYPE_EXT_NATIVE &&
                                 native_to_utf8.native_is_utf8())) {
                            if (!ci__length_utf8_fast(
                                    curs_s, curs_n, retint[k])) {
                                throw StriException(MSG__INVALID_UTF8);
                            }
                        }
                        else if (encoding == CETYPE_EXT_NATIVE) {
                            const shared::ByteView converted =
                                native_to_utf8.native(curs_s, curs_n);
                            if (!ci__length_utf8_fast(
                                    converted.ptr,
                                    converted.len,
                                    retint[k])) {
                                throw StriException(MSG__INVALID_UTF8);
                            }
                        }
                        else {
                            throw StriException(
                                "unknown charport string encoding"
                            );
                        }
                    }
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}


#ifdef DEBUG
//#define DEBUG_STRI_WIDTH
#endif


/** Get width of a single character
 *
 * inspired by http://www.cl.cam.ac.uk/~mgk25/ucs/wcwidth.c
 * and https://github.com/nodejs/node/blob/master/src/node_i18n.cc
 * but with extras
 *
 * @version ?? init
 *
 * @version 1.2.1 (Marek Gagolewski, 2018-04-20)
 *    add Variation Selectors support (width 0)
 *
 * @version 1.6.1 (Marek Gagolewski, 2021-05-04)
 *    emoji support etc.
 *
 * @version 1.6.2 (Marek Gagolewski, 2021-05-13)
 *    bugfixes
 *
 * @param c code point
 * @return 0, 1, or 2
 */
CHARR_NEUTRAL_HELPER int ci__width_char(UChar32 c) noexcept
{
    if (c >= 0 && c <= 0x7f)
        return ci__ascii_char_width(static_cast<unsigned char>(c));

    const std::uint32_t gc_mask = U_GET_GC_MASK(c);

    /* Characters with the \code{UCHAR_EAST_ASIAN_WIDTH} enumerable property
       equal to \code{U_EA_FULLWIDTH} or \code{U_EA_WIDE} are of width 2. */
    int width = (int)u_getIntPropertyValue(c, UCHAR_EAST_ASIAN_WIDTH);

#ifdef DEBUG_STRI_WIDTH
//     Rscript -e 'ci_width("\u005E\u0060\u2081\u03C9\u0425\u00DF")'
    Rprintf(
        "c=%08x MN=%d ME=%d CF=%d CC=%d SO=%d SK=%d EMOJI_MOD=%d EMOJI_PRES=%d h1=%d h2=%d w=%d\n", (int)c,
        0!=(gc_mask & U_GC_MN_MASK),
        0!=(gc_mask & U_GC_ME_MASK),
        0!=(gc_mask & U_GC_CF_MASK),
        0!=(gc_mask & U_GC_CC_MASK),
        0!=(gc_mask & U_GC_SO_MASK),
        0!=(gc_mask & U_GC_SK_MASK),
        0!=u_hasBinaryProperty(c, UCHAR_EMOJI_MODIFIER),
        0!=u_hasBinaryProperty(c, UCHAR_EMOJI_PRESENTATION),
        u_getIntPropertyValue(c, UCHAR_HANGUL_SYLLABLE_TYPE) == U_HST_VOWEL_JAMO,
        u_getIntPropertyValue(c, UCHAR_HANGUL_SYLLABLE_TYPE) == U_HST_TRAILING_JAMO,
        (int)width
    );
//      U_EA_NEUTRAL,   0/*[N]*/
//      U_EA_AMBIGUOUS, 1/*[A]*/
//      U_EA_HALFWIDTH, 2/*[H]*/
//      U_EA_FULLWIDTH, 3/*[F]*/
//      U_EA_NARROW,    4/*[Na]*/
//      U_EA_WIDE,      5/*[W]*/
#endif

    if (c == (UChar32)0x00AD) return 1; /* SOFT HYPHEN  */
    if (c == (UChar32)0x200B) return 0; /* ZERO WIDTH SPACE */

    /* GC: Me, Mn, Cf, Cc -> width = 0 */
    if (gc_mask &
            (U_GC_MN_MASK | U_GC_ME_MASK | U_GC_CF_MASK | U_GC_CC_MASK))
        return 0;

    /* Hangul Jamo medial vowels and final consonants have width 0 */
    int hangul = (int)u_getIntPropertyValue(c, UCHAR_HANGUL_SYLLABLE_TYPE);
    if (hangul == U_HST_VOWEL_JAMO || hangul == U_HST_TRAILING_JAMO)
        return 0;

    /* Variation Selectors */
    if (c >= (UChar32)0xFE00 && c <= (UChar32)0xFE0F)
        return 0;

#if U_ICU_VERSION_MAJOR_NUM>=57
    // UCHAR_EMOJI_* is ICU >= 57
    if (
        u_hasBinaryProperty(c, UCHAR_EMOJI_MODIFIER)
    ) {
        return 0;
    }
#endif

    if (width == U_EA_FULLWIDTH || width == U_EA_WIDE)
        return 2;

    /* v1.6.1 had U_EA_AMBIGUOUS set to 2, this was not a good idea
    if (width == U_EA_AMBIGUOUS) return 2;
    // 'a' is narrow
    // 'a with ogonek' is neutral
    // 'Eszett' is ambiguous
    // 'grave accent' is narrow
    */

    /* v1.6.1 GC=So -> width = 2 */
    if (gc_mask & U_GC_SO_MASK)
        return 2;

    /*
    v1.6.1 had GC=Sk of width = 0
    but there are exceptions:
    \u005E \N{CIRCUMFLEX ACCENT}   ^  is Sk
    \u0060 \N{GRAVE ACCENT}        `  is Sk
    generally, it's not a good idea
    */


#if U_ICU_VERSION_MAJOR_NUM>=57
    // UCHAR_EMOJI_* is ICU >= 57
    if (width == U_EA_NEUTRAL && u_hasBinaryProperty(c, UCHAR_EMOJI_PRESENTATION))
        return 2;
#endif

    /*  any other characters have width 1 */
    return 1;
}




/** Get width of a single character (context-dependent)
 *
 * inspired by http://www.cl.cam.ac.uk/~mgk25/ucs/wcwidth.c
 * and https://github.com/nodejs/node/blob/master/src/node_i18n.cc
 * but with extras
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-06-14)
 *    stand-alone fun
 *
 * @param c code point
 * @param p previous code point
 * @return int
 */
CHARR_NEUTRAL_HELPER int ci__width_char_with_context(
    UChar32 c, UChar32 p, bool& reset
) noexcept
{
    if (reset) {
        p = 0;
        reset = false;
    }

#if U_ICU_VERSION_MAJOR_NUM>=57
    // UCHAR_EMOJI_* is ICU >= 57
    if (
        /*j > 0 &&*/ p == 0x200D /* ZERO WIDTH JOINER */ && (
            u_hasBinaryProperty(c, UCHAR_EMOJI_MODIFIER) ||
            u_hasBinaryProperty(c, UCHAR_EMOJI_PRESENTATION) ||
            c == 0x2640 /* FEMALE */ ||
            c == 0x2642 /* MALE */ ||
            c == 0x26A7 /* TRANSGENDER */ ||
            c == 0x2695 /* HEALTH */ ||
            c == 0x2696 /* JUDGE */ ||
            c == 0x1F5E8 /* SPEECH */ ||
            c == 0x1F32B /* CLOUDS */ ||
            c == 0x2708 /* PLANE */ ||
            c == 0x2764 /* HEART */ ||
            c == 0x2744 /* SNOWFLAKE */ ||
            c == 0x2620 /* SKULL AND CROSSBONES */
        )
    ) {
        // emoji sequence - ignore (display might not support it)
        return 0;
    }
    else if (
        /*j > 0 &&*/ (p >= 0x1F1E6 && p <= 0x1F1FF)
        && (c >= 0x1F1E6 && c <= 0x1F1FF)
    ) {
        // E2.0 flag (p counted as of width=2 already)
        reset = true;  // allow the next flag to be recognised
        return 0;
    }
    else {
        return ci__width_char(c);
    }
#else // U_ICU_VERSION_MAJOR_NUM < 57 - no emoji support
    return ci__width_char(c);
#endif
}


/** Get the length (number of Unicode code points) of a single UTF-8 string
 *  or get the position where a substring of <= max_length ends
 *
 * @param str_cur_s string
 * @param str_cur_n number of bytes in str_cur_s
 * @param max_length
 * @return length of the whole string (if max_length==NA_INTEGER) or index
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-05-22)
 *    extracted from ci_length
 */
CHARR_CXX_HELPER int ci__length_string(
    const char* str_cur_s, int str_cur_n, int max_length
)
{
    // is string is in ASCII, then length == str_cur_n, but with
    // merely str_cur_s ptr we are unable to tell that here

    UChar32 c = 0;
    R_len_t j = 0;
    R_len_t cur_length = 0;
    while (j < str_cur_n) {
        R_len_t prevj = j;
        U8_NEXT(str_cur_s, j, str_cur_n, c); // faster that U8_FWD_1 & gives bad UChar32s
        if (c < 0)
            throw StriException(MSG__INVALID_UTF8);
        cur_length++;
        if (max_length != NA_INTEGER && cur_length > max_length)
            return prevj;
    }

    if (max_length == NA_INTEGER)
        return cur_length;
    else
        return str_cur_n;  // the whole string has length <= max_length
}


/** Get the width of a single UTF-8 string or get the position where
 *  a substring of <= max_width ends
 *
 * @param str_cur_s string
 * @param str_cur_n number of bytes in str_cur_s
 * @param max_width
 * @return width of the whole string (if max_width==NA_INTEGER)
 * or index
 *
 * @version 1.6.1 (Marek Gagolewski)
 *    most in https://unicode.org/Public/emoji/13.1/emoji-test.txt of width=2
 *
 * @version 1.6.2 (Marek Gagolewski, 2021-05-13)
 *    bugfixes
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-05-22)
 *    max_width
 */
CHARR_CXX_HELPER int ci__width_string(
    const char* str_cur_s, int str_cur_n, int max_width
)
{
    int cur_width = 0;

    UChar32 p;      // previous
    UChar32 c = 0;  // current
    R_len_t j = 0;
    bool reset = true;
    while (j < str_cur_n) {
        R_len_t prevj = j;
        p = c;

        const unsigned char byte =
            static_cast<unsigned char>(str_cur_s[j]);
        if (byte < 0x80) {
            c = static_cast<UChar32>(byte);
            ++j;
            if (reset)
                reset = false;
            cur_width += ci__ascii_char_width(byte);
            if (max_width != NA_INTEGER && cur_width > max_width)
                return prevj;
            continue;
        }

        U8_NEXT(str_cur_s, j, str_cur_n, c);
        if (c < 0)
            throw StriException(MSG__INVALID_UTF8);

        cur_width += ci__width_char_with_context(c, p, reset);

        // test if max_width exceeded (here; there may be zero-width chars)
        if (max_width != NA_INTEGER && cur_width > max_width)
            return prevj;
    }

    if (max_width == NA_INTEGER)
        return cur_width;
    else
        return str_cur_n;  // the whole string has width <= max_width
}


/**
  * Determine the width of strings
  *
  * @param str character vector
  * @return integer vector
  *
  * @version 0.5-1 (Marek Gagolewski, 2015-04-22)
  */
CHARR_ENTRYPOINT SEXP ci_width(SEXP str) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );

    try {
        charport::Reader reader;
        charport::StrViews views;
        shared::NativeToUtf8 native_to_utf8;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                reader.reset(str);
                const R_len_t str_n = io::checked_r_len(
                    reader.size(), "character vectors"
                );

                result = entry_protections.reprotect_one(
                    Rf_allocVector(INTSXP, str_n), result_index
                );
                int* retint = INTEGER(result);

                if (str_n > 0) {
                    views.resize(str_n);
                    reader.views(
                        0, str_n,
                        views.ptrs(), views.lengths(), views.encodings()
                    );
                    const char* const* ptrs = views.ptrs();
                    const int* lengths = views.lengths();
                    const cetype_ext_t* encodings = views.encodings();

                    for (R_len_t i = 0; i < str_n; ++i) {
                        const cetype_ext_t encoding = encodings[i];
                        if (encoding == CETYPE_EXT_NA) {
                            retint[i] = NA_INTEGER;
                            continue;
                        }

                        const char* data = ptrs[i];
                        const int length = lengths[i];
                        if (encoding == CETYPE_EXT_ASCII) {
                            retint[i] = ci__ascii_string_width(data, length);
                            continue;
                        }
                        if (encoding == CETYPE_EXT_BYTES)
                            throw StriException(MSG__BYTESENC);

                        if (encoding == CETYPE_EXT_UTF8 ||
                                encoding ==
                                    CETYPE_EXT_ASCII_OR_UTF8) {
                            retint[i] = ci__width_string(data, length);
                            continue;
                        }

                        if (encoding != CETYPE_EXT_LATIN1 &&
                                encoding != CETYPE_EXT_NATIVE) {
                            throw StriException(
                                "unknown charport string encoding"
                            );
                        }
                        const shared::ByteView converted =
                            encoding == CETYPE_EXT_LATIN1
                                ? native_to_utf8.latin1(data, length)
                                : native_to_utf8.native(data, length);
                        retint[i] = ci__width_string(
                            converted.ptr, converted.len
                        );
                    }
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}

} } // namespace charr::altrep_backend
