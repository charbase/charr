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
#include "ci_container_utf16.h"
#include "ci_container_usearch.h"
#include "ci_container_bytesearch.h"
#include "ci_container_integer.h"
#include "ci_container_logical.h"
#include <deque>
#include <utility>
#include <unicode/brkiter.h>
#include <unicode/rbbi.h>
using namespace std;


/**
 * Split a single string into text lines
 *
 * @param str character vector
 *
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-04)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci_split_lines1(SEXP str)
{
    PROTECT(str = ci__prepare_arg_string_1(str, "str"));
    R_len_t vectorize_length = LENGTH(str);

    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP ans = R_NilValue;
    bool source_is_na = false;
    {
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    // Deviation from stringi: retain the exact-size result in a Store so it
    // remains lazy, then release that owner before warnings.
    charport::charvec::Store output(0, 0);
    {
        StriContainerUTF8 str_cont(context, str, vectorize_length);

        if (str_cont.isNA(0)) {
            source_is_na = true;
        }
        else {
            const char* str_cur_s = str_cont.get(0).data();
            R_len_t str_cur_n = str_cont.get(0).length();

            UChar32 c;
            R_len_t jlast;
            deque< pair<R_len_t, R_len_t> > occurrences;
            occurrences.push_back(pair<R_len_t, R_len_t>(0, 0));
            for (R_len_t j=0; j < str_cur_n; /* null */) {
                jlast = j;
                U8_NEXT(str_cur_s, j, str_cur_n, c);

                switch (c) {
                case ASCII_CR: /* CR */
                    /* check if next is LF */
                    // Deviation from stringi: the borrowed record has no
                    // sentinel byte after its length.
                    if (j < str_cur_n && str_cur_s[j] == ASCII_LF) { // look ahead one byte
                        j++; // just one byte
                    }
                    break;

                case ASCII_LF: /* LF */
                    break;

                case UCHAR_NEL: /* NEL */
                    break;

                case ASCII_VT: /* VT */
                    break;

                case ASCII_FF: /* FF */
                    break;

                case UCHAR_LS: /* LS */
                    break;

                case UCHAR_PS: /* PS */
                    break;

                default:
                    /* not a newline character */
                    occurrences.back().second = j;
                    continue;
                }

                occurrences.back().second = jlast;
                if (j < str_cur_n)
                    occurrences.push_back(pair<R_len_t, R_len_t>(j, j));
            }

            if (occurrences.size() == 1) {
                const pair<R_len_t, R_len_t>& curoccur =
                    occurrences.front();
                const char* value = str_cur_s+curoccur.first;
                size_t value_length = static_cast<size_t>(
                    curoccur.second-curoccur.first
                );
                output = ci::scalar_store(
                    value, value_length,
                    cetype_ext_t::CE_ASCII_OR_UTF8
                );
            }
            else if (!occurrences.empty()) {
                charport::charvec::Builder builder(
                    static_cast<R_xlen_t>(occurrences.size())
                );
                deque< pair<R_len_t, R_len_t> >::iterator iter =
                    occurrences.begin();
                for (R_len_t i=0; iter != occurrences.end(); ++iter, ++i) {
                    pair<R_len_t, R_len_t> curoccur = *iter;
                    ci::builder_set(
                        builder, i, str_cur_s+curoccur.first,
                        curoccur.second-curoccur.first,
                        cetype_ext_t::CE_ASCII_OR_UTF8
                    );
                }
                output = builder.release_store();
            }
        }
    }

    if (!source_is_na) {
        STRI__PROTECT(ans = charport::charvec::wrap(
            std::move(output)
        ));
    }
    }
    STRI__DEFERRED_WARNINGS.emit();

    STRI__UNPROTECT_ALL
    return source_is_na ? str : ans;

    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/**
 * Split a string into text lines
 *
 * @param str character vector
 * @param omit_empty logical vector
 *
 * @return list of character vectors
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-04)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-10-30)
 *                removed `n_max` arg, as it doesn't make sense
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci_split_lines(SEXP str, SEXP omit_empty)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
//   n_max = ci__prepare_arg_integer(n_max, "n_max");
    PROTECT(omit_empty = ci__prepare_arg_logical(omit_empty, "omit_empty"));

    STRI__ERROR_HANDLER_BEGIN(2)
    SEXP ret;
    {
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    R_len_t str_n = ci::checked_r_len(
        context.size(str), "character vectors"
    );
    R_len_t omit_empty_n = 0;
    R_len_t vectorize_length = 0;
    charport::unwind_protect([&]() -> SEXP {
        omit_empty_n = LENGTH(omit_empty);
        vectorize_length = ci__recycling_rule(
            false, 2, str_n, omit_empty_n
        );
        return R_NilValue;
    });
    // Deviation from stringi: queue the controllable recycling warning until
    // Reader and lazy-output owners have been released.
    if (vectorize_length > 0 &&
            (vectorize_length % str_n != 0 ||
             vectorize_length % omit_empty_n != 0))
        context.warn(MSG__WARN_RECYCLING_RULE);

    // Deviation from stringi: preinitialize lazy empty vectors because the
    // vectorization order is not sequential, then replace each visited slot
    // with a scalar or exact-size Store once its line count is known.
    vector<charport::charvec::Store> stores;
    stores.reserve(static_cast<size_t>(vectorize_length));
    for (R_len_t i=0; i<vectorize_length; ++i)
        stores.emplace_back(0, 0);
//   StriContainerInteger   n_max_cont(n_max, vectorize_length);
    StriContainerLogical   omit_empty_cont(omit_empty, vectorize_length);
    {
        StriContainerUTF8 str_cont(context, str, vectorize_length);
        charport::charvec::Builder output(0);

        for (R_len_t i = str_cont.vectorize_init();
                i != str_cont.vectorize_end();
                i = str_cont.vectorize_next(i))
        {
            if (str_cont.isNA(i)) {
                stores[i] = charport::charvec::Store::scalar(
                    nullptr, 0, cetype_ext_t::CE_NA
                );
                continue;
            }

            const char* str_cur_s = str_cont.get(i).data();
            R_len_t str_cur_n = str_cont.get(i).length();
            // int  n_max_cur        = n_max_cont.get(i);
            int  omit_empty_cur   = omit_empty_cont.get(i);

            // if (n_max_cur < 0)
            //    n_max_cur = INT_MAX;
            // else if (n_max_cur == 0) {
            //    SET_VECTOR_ELT(ret, i, Rf_allocVector(STRSXP, 0));
            //    continue;
            // }

            //#define STRI_INDEX_NEWLINE_CR   0
            //#define STRI_INDEX_NEWLINE_LF   1
            //#define STRI_INDEX_NEWLINE_CRLF 2
            //#define STRI_INDEX_NEWLINE_NEL  3
            //#define STRI_INDEX_NEWLINE_VT   4
            //#define STRI_INDEX_NEWLINE_FF   5
            //#define STRI_INDEX_NEWLINE_LS   6
            //#define STRI_INDEX_NEWLINE_PS   7
            //#define STRI_INDEX_NEWLINE_LAST 8

            // int counts[STRI_INDEX_NEWLINE_LAST];
            // for (R_len_t j=0; j<STRI_INDEX_NEWLINE_LAST; ++j)
            //    counts[j] = 0;

            UChar32 c;
            R_len_t jlast, k=1;
            deque< pair<R_len_t, R_len_t> > occurrences;
            occurrences.push_back(pair<R_len_t, R_len_t>(0, 0));
            for (R_len_t j=0; j < str_cur_n /*&& k < n_max_cur*/; /* null */) {
                jlast = j;
                U8_NEXT(str_cur_s, j, str_cur_n, c);

                switch (c) {
                case ASCII_CR: /* CR */
                    // counts[STRI_INDEX_NEWLINE_CR]++;
                    /* check if next is LF */
                    // Deviation from stringi: the borrowed record has no
                    // sentinel byte after its length.
                    if (j < str_cur_n && str_cur_s[j] == ASCII_LF) { // look ahead one byte
                        // counts[STRI_INDEX_NEWLINE_LF]++;
                        // counts[STRI_INDEX_NEWLINE_CRLF]++;
                        j++; // just one byte
                    }
                    break;

                case ASCII_LF: /* LF */
                    // counts[STRI_INDEX_NEWLINE_LF]++;
                    break;

                case UCHAR_NEL: /* NEL */
                    // counts[STRI_INDEX_NEWLINE_NEL]++;
                    break;

                case ASCII_VT: /* VT */
                    // counts[STRI_INDEX_NEWLINE_VT]++;
                    break;

                case ASCII_FF: /* FF */
                    // counts[STRI_INDEX_NEWLINE_FF]++;
                    break;

                case UCHAR_LS: /* LS */
                    // counts[STRI_INDEX_NEWLINE_LS]++;
                    break;

                case UCHAR_PS: /* PS */
                    // counts[STRI_INDEX_NEWLINE_PS]++;
                    break;

                default:
                    /* not a newline character */
                    occurrences.back().second = j;
                    continue;
                }

                // if here, then at newline
                if (omit_empty_cur && occurrences.back().second == occurrences.back().first)
                    occurrences.back().first = occurrences.back().second = j; // don't start any new field
                else {
                    occurrences.back().second = jlast;
                    occurrences.push_back(pair<R_len_t, R_len_t>(j, j));
                    ++k; // another field
                }
            }

            // if (k == n_max_cur)
            //    occurrences.back().second = str_cur_n;
            if (omit_empty_cur && occurrences.back().first == occurrences.back().second)
                occurrences.pop_back();

            if (occurrences.size() == 1) {
                const pair<R_len_t, R_len_t>& curoccur =
                    occurrences.front();
                const char* value = str_cur_s+curoccur.first;
                size_t value_length = static_cast<size_t>(
                    curoccur.second-curoccur.first
                );
                stores[i] = ci::scalar_store(
                    value, value_length,
                    cetype_ext_t::CE_ASCII_OR_UTF8
                );
            }
            else if (!occurrences.empty()) {
                output.reset(static_cast<R_xlen_t>(occurrences.size()));
                deque< pair<R_len_t, R_len_t> >::iterator iter =
                    occurrences.begin();
                for (R_len_t j=0; iter != occurrences.end(); ++iter, ++j) {
                    pair<R_len_t, R_len_t> curoccur = *iter;
                    ci::builder_set(
                        output, j, str_cur_s+curoccur.first,
                        curoccur.second-curoccur.first,
                        cetype_ext_t::CE_ASCII_OR_UTF8
                    );
                }
                stores[i] = output.release_store();
            }
        }
    }

    STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
        return Rf_allocVector(VECSXP, vectorize_length);
    }));
    for (R_len_t i=0; i<vectorize_length; ++i) {
        SEXP ans;
        STRI__PROTECT(ans = charport::charvec::wrap(
            std::move(stores[i])
        ));
        SET_VECTOR_ELT(ret, i, ans);
        STRI__UNPROTECT(1);
    }
    }
    STRI__DEFERRED_WARNINGS.emit();

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}
