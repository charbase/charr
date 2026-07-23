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
#include "ci_builder.h"
#include "ci_container_utf8.h"
#include "ci_container_integer.h"
#include "ci_string8buf.h"
#include <cstring>
#include <vector>


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

    STRI__ERROR_HANDLER_BEGIN(3)
    SEXP ret;
    {
        ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
        R_len_t str_length = ci::checked_r_len(
            context.size(str), "character vectors"
        );
        R_len_t width_length = ci::checked_r_len(
            context.size(width), "integer vectors"
        );
//   R_len_t side_length    = LENGTH(side);
        R_len_t pad_length = ci::checked_r_len(
            context.size(pad), "character vectors"
        );
        R_len_t vectorize_length = 0;
        vectorize_length = ci__recycling_rule(
            STRI__DEFERRED_WARNINGS, 3, str_length, width_length,
            /*side_length, */ pad_length
        );

        charport::charvec::Builder builder(vectorize_length);
        StriContainerInteger  width_cont(width, vectorize_length);
        {
            StriContainerUTF8 str_cont(context, str, vectorize_length);
//   StriContainerUTF8      side_cont(side, vectorize_length);
            StriContainerUTF8 pad_cont(context, pad, vectorize_length);

            String8buf buf(0); // TODO: prealloc
            for (R_len_t i=0; i<vectorize_length; ++i) {
                if (str_cont.isNA(i) || pad_cont.isNA(i)
                        || /*side_cont.isNA(i) ||*/ width_cont.isNA(i)) {
                    builder.set_na(i);
                    continue;
                }

                // get the current string
                R_len_t str_cur_n = str_cont.get(i).length();
                const char* str_cur_s = str_cont.get(i).data();
                R_len_t str_cur_width;

                // get the width/length of padding code point(s)
                R_len_t pad_cur_n = pad_cont.get(i).length();
                const char* pad_cur_s = pad_cont.get(i).data();
                R_len_t pad_cur_width;
                if (use_length_val) {
                    pad_cur_width = 1;
                    str_cur_width = str_cont.get(i).countCodePoints();
                    R_len_t k = 0;
                    UChar32 pad_cur = 0;
                    U8_NEXT(pad_cur_s, k, pad_cur_n, pad_cur);
                    if (pad_cur <= 0 || k < pad_cur_n)
                        throw StriException(MSG__NOT_EQ_N_CODEPOINTS, "pad", 1);
                }
                else {
                    pad_cur_width = ci__width_string(pad_cur_s, pad_cur_n);
                    str_cur_width = ci__width_string(str_cur_s, str_cur_n);
                    if (pad_cur_width != 1)
                        throw StriException(MSG__NOT_EQ_N_WIDTH, "pad", 1);
                }

                // get the minimal width
                R_len_t width_cur = width_cont.get(i);

                if (str_cur_width >= width_cur)  {
                    // no padding at all
                    ci::builder_set(builder, i, str_cont.get(i));
                    continue;
                }

                R_len_t padnum = width_cur-str_cur_width;
                buf.resize(str_cur_n+padnum*pad_cur_n, false);

                char* buftmp = buf.data();
                R_len_t k = 0;
                switch(_side) {

                case 0: // left
                    for (k=0; k<padnum; ++k) {
                        memcpy(buftmp, pad_cur_s, pad_cur_n);
                        buftmp += pad_cur_n;
                    }
                    memcpy(buftmp, str_cur_s, str_cur_n);
                    buftmp += str_cur_n;
                    break;

                case 1: // right
                    memcpy(buftmp, str_cur_s, str_cur_n);
                    buftmp += str_cur_n;
                    for (k=0; k<padnum; ++k) {
                        memcpy(buftmp, pad_cur_s, pad_cur_n);
                        buftmp += pad_cur_n;
                    }
                    break;

                case 2: // both
                    for (k=0; k<padnum/2; ++k) {
                        memcpy(buftmp, pad_cur_s, pad_cur_n);
                        buftmp += pad_cur_n;
                    }
                    memcpy(buftmp, str_cur_s, str_cur_n);
                    buftmp += str_cur_n;
                    for (; k<padnum; ++k) {
                        memcpy(buftmp, pad_cur_s, pad_cur_n);
                        buftmp += pad_cur_n;
                    }
                    break;
                }

                ci::builder_set(
                    builder, i, buf.data(), (int)(buftmp-buf.data()),
                    cetype_ext_t::CE_ASCII_OR_UTF8
                );
            }
        }

        STRI__PROTECT(ret = builder.to_sexp());
    }

    STRI__DEFERRED_WARNINGS.emit();
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
