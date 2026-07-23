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
#include "ci_string8buf.h"
#include <deque>
#include <vector>

/**
 * fill a vector with CHARSXPs == individual code points in s of length n
 */
void ci__split_codepoints(vector<UChar32>& out, const char* s, int n) {
    UChar32 c = 0;
    R_len_t j = 0; // current pos
    while (j < n) {
        U8_NEXT(s, j, n, c);
        out.push_back(c);

        if (c < 0)
            throw StriException(MSG__INVALID_UTF8);
    }
}


/**
 *  Translate code points
 *
 *
 *  @param str character vector
 *  @param pattern character vector
 *  @param replacement character vector
 *  @return character vector
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-04-06)
 *
 * @version 1.3.2 (Marek Gagolewski, 2019-02-20)
 *     BUGFIX: overlapping maps (#343)
 */
SEXP ci_trans_char(SEXP str, SEXP pattern, SEXP replacement) {
    PROTECT(str          = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern      = ci__prepare_arg_string_1(pattern, "pattern"));
    PROTECT(replacement  = ci__prepare_arg_string_1(replacement, "replacement"));

    STRI__ERROR_HANDLER_BEGIN(3)
    SEXP ret;
    {
        ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
        R_len_t vectorize_length = ci::checked_r_len(
            context.size(str), "character vectors"
        );
        charport::charvec::Builder builder(vectorize_length);
        {
            StriContainerUTF8 replacement_cont(context, replacement, 1);
            StriContainerUTF8 pattern_cont(context, pattern, 1);

            if (replacement_cont.isNA(0) || pattern_cont.isNA(0)) {
                for (R_len_t i=0; i<vectorize_length; ++i)
                    builder.set_na(i);
            }
            else {
                const String8* s_pat = &pattern_cont.get(0);
                const String8* s_rep = &replacement_cont.get(0);

                std::vector<UChar32> d_pat;
                ci__split_codepoints(d_pat, s_pat->data(), s_pat->length());

                std::vector<UChar32> d_rep;
                ci__split_codepoints(d_rep, s_rep->data(), s_rep->length());

                R_len_t m = std::min(d_rep.size(), d_pat.size());
                if (d_pat.size() != d_rep.size()) {
                    context.warn(MSG__WARN_RECYCLING_RULE2);
                }


                StriContainerUTF8 str_cont(context, str, vectorize_length);
                if (m == 0) { // nothing to do
                    for (R_len_t i = str_cont.vectorize_init();
                            i != str_cont.vectorize_end();
                            i = str_cont.vectorize_next(i))
                        ci::builder_set(builder, i, str_cont.getNAble(i));
                }
                else {
                    std::vector<char> buf;
                    for (R_len_t i = str_cont.vectorize_init();
                            i != str_cont.vectorize_end();
                            i = str_cont.vectorize_next(i))
                    {
                        if (str_cont.isNA(i)) {
                            builder.set_na(i);
                            continue;
                        }
                        buf.clear();

                        const char* s = str_cont.get(i).data();
                        R_len_t n = str_cont.get(i).length();

                        UChar32 c = 0;
                        R_len_t j = 0; // current pos
                        while (j < n) {
                            U8_NEXT(s, j, n, c);
                            if (c < 0) throw StriException(MSG__INVALID_UTF8);

                            // considering only the first m elements in d_pat and d_rep, from last
                            for (R_len_t k=m-1; k>=0; --k) {
                                if (d_pat[k] == c) {
                                    c = d_rep[k];
                                    break;
                                }
                            }

                            // U8_APPEND_UNSAFE(buf, /end/, c)
                            uint32_t __uc=(c);
                            if(__uc<=0x7f) {
                                buf.push_back((uint8_t)__uc);
                            } else {
                                if(__uc<=0x7ff) {
                                    buf.push_back((uint8_t)((__uc>>6)|0xc0));
                                } else {
                                    if(__uc<=0xffff) {
                                        buf.push_back((uint8_t)((__uc>>12)|0xe0));
                                    } else {
                                        buf.push_back((uint8_t)((__uc>>18)|0xf0));
                                        buf.push_back((uint8_t)(((__uc>>12)&0x3f)|0x80));
                                    }
                                    buf.push_back((uint8_t)(((__uc>>6)&0x3f)|0x80));
                                }
                                buf.push_back((uint8_t)((__uc&0x3f)|0x80));
                            }
                        }

                        ci::builder_set(
                            builder, i, buf.empty() ? "" : buf.data(),
                            buf.size(), cetype_ext_t::CE_ASCII_OR_UTF8
                        );
                    }
                }
            }
        }

        STRI__PROTECT(ret = builder.to_sexp());
    }

    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}
