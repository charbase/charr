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
#include <exception>
#include <memory>
#include <vector>
#include "ci_container_charclass.h"


namespace {


void ci__rng_begin(bool& rng_active)
{
    // Deviation from stringi: the shared error boundary has already created
    // C++ state, so translate a GetRNGstate unwind before it can skip normal
    // stack destruction.
    charport::unwind_protect([]() -> SEXP {
        GetRNGstate();
        return R_NilValue;
    });
    rng_active = true;
}


void ci__rng_end(bool& rng_active)
{
    if (!rng_active)
        return;

    // Clear the guard before the R boundary: a failing PutRNGstate call must
    // not be attempted twice by the outer cleanup path.
    rng_active = false;
    charport::unwind_protect([]() -> SEXP {
        PutRNGstate();
        return R_NilValue;
    });
}


} // namespace


/** Generate random permutations of code points in each string
 *
 * @param str character vector
 * @return character vector
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-04)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.2.5 (Marek Gagolewski, 2019-07-23)
 *    #319: Fixed overflow in `ci_rand_shuffle()`.
 */
SEXP ci_rand_shuffle(SEXP str)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    bool rng_active = false;

    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP ret;
    {
        std::unique_ptr<charport::charvec::Builder> builder;
        std::exception_ptr pending_error;
        ci__rng_begin(rng_active);
        try {
            {
            ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
            R_len_t n = ci::checked_r_len(
                context.size(str), "character vectors"
            );
            builder.reset(new charport::charvec::Builder(n));
            StriContainerUTF8 str_cont(context, str, n);

            R_len_t bufsize = 0;
            for (R_len_t i=0; i<n; ++i) {
                if (str_cont.isNA(i)) continue;
                R_len_t ni = str_cont.get(i).length();
                if (ni > bufsize) bufsize = ni;
            }
            std::vector<UChar32> buf1(bufsize); // at most bufsize UChars32 (bufsize/4 min.)
            String8buf buf2(bufsize);

            for (R_len_t i=0; i<n; ++i) {

                if (str_cont.isNA(i)) {
                    builder->set_na(i);
                    continue;
                }

                // fill buf1
                UChar32 c = (UChar32)0;
                const char* s = str_cont.get(i).data();
                R_len_t sn = str_cont.get(i).length();
                R_len_t j = 0;
                R_len_t k = 0;
                while (c >= 0 && j < sn) {
                    U8_NEXT(s, j, sn, c);
                    buf1[k++] = (int)c;
                }

                if (c < 0) {
                    throw StriException(MSG__INVALID_UTF8);
//                  Rf_warning(...);
//                  SET_STRING_ELT(ret, i, NA_STRING);
//                  continue;
                }

                // do shuffle buf1 at pos 0..k-1: (Fisher-Yates shuffle)
                R_len_t cur_n = k;
                for (j=0; j<cur_n-1; ++j) {
                    // rand from i to cur_n-1
                    R_len_t r = (R_len_t)floor(unif_rand()*(double)(cur_n-j)+(double)j);
                    UChar32 tmp = buf1[r];
                    buf1[r] = buf1[j];
                    buf1[j] = tmp;
                }

                // create string:
                char* buf2data = buf2.data();
                c = (UChar32)0;
                j = 0;
                k = 0;
                UBool err = FALSE;
                while (!err && k < cur_n) {
                    c = buf1[k++];
                    U8_APPEND((uint8_t*)buf2data, j, bufsize, c, err);
                }

                if (err) throw StriException(MSG__INTERNAL_ERROR);

                ci::builder_set(
                    *builder, i, buf2data, j,
                    cetype_ext_t::CE_ASCII_OR_UTF8
                );
            }
            }
            STRI__PROTECT(ret = builder->to_sexp());
        }
        catch (...) {
            pending_error = std::current_exception();
        }

        // Deviation from stringi: release Reader and Builder storage before
        // PutRNGstate even on failure. Running PutRNGstate in the main try also
        // lets its own unwind reach the operation handler instead of escaping
        // from catch cleanup.
        builder.reset();
        ci__rng_end(rng_active);
        if (pending_error)
            std::rethrow_exception(pending_error);
    }

    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END({ /* RNG has already been released */ })
}


/** Generate random strings
 *
 * @param n single integer
 * @param length integer vector
 * @param pattern character vector
 * @return character vector
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-04)
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-05)
 *          Use StriContainerCharClass which now contains UnicodeSets;
 *          vectorized also over pattern
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci_rand_strings(SEXP n, SEXP length, SEXP pattern)
{
    int n_val = ci__prepare_arg_integer_1_notNA(n, "n");
    PROTECT(length    = ci__prepare_arg_integer(length, "length"));
    PROTECT(pattern   = ci__prepare_arg_string(pattern, "pattern"));

    if (n_val < 0) n_val = 0; /* that's not NA for sure now */

    R_len_t length_len = LENGTH(length);
    if (length_len <= 0) {
        UNPROTECT(2);
        Rf_error(MSG__ARG_EXPECTED_NOT_EMPTY, "length");
    }
    else if (length_len > n_val || n_val % length_len != 0)
        Rf_warning(MSG__WARN_RECYCLING_RULE2);

    R_len_t pattern_len = LENGTH(pattern);
    if (pattern_len <= 0) {
        UNPROTECT(2);
        Rf_error(MSG__ARG_EXPECTED_NOT_EMPTY, "pattern");
    }
    else if (pattern_len > n_val || n_val % pattern_len != 0)
        Rf_warning(MSG__WARN_RECYCLING_RULE2);

    bool rng_active = false;
    STRI__ERROR_HANDLER_BEGIN(2)
    SEXP ret;
    {
        std::unique_ptr<charport::charvec::Builder> builder;
        std::exception_ptr pending_error;
        ci__rng_begin(rng_active);
        try {
            {
            ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
            builder.reset(new charport::charvec::Builder(n_val));
            StriContainerCharClass pattern_cont(
                context, pattern, max(n_val, pattern_len)
            );
            StriContainerInteger length_cont(
                length, max(n_val, length_len)
            );

            // get max required bufsize
            size_t bufsize = 0;
            for (R_len_t i=0; i<length_len; ++i) {
                int length_cur = length_cont.getNAble(i);
                if (length_cur != NA_INTEGER &&
                        (size_t)length_cur > bufsize)
                    bufsize = length_cur;
            }
            bufsize *= 4;  // 1 UChar32 -> max. 4 UTF-8 bytes
            String8buf buf(bufsize);
            char* bufdata = buf.data();

            for (R_len_t i=0; i<n_val; ++i) {
                if (length_cont.isNA(i) || pattern_cont.isNA(i)) {
                    builder->set_na(i);
                    continue;
                }

                R_len_t length_cur = length_cont.get(i);
                if (length_cur < 0) length_cur = 0;

                const UnicodeSet* uset = &(pattern_cont.get(i));
                int32_t uset_size = uset->size();

                // generate string:
                size_t j = 0;
                UBool err = FALSE;
                bool embedded_nul = false;
                for (R_len_t k=0; k<length_cur; ++k) {
                    int32_t idx = (int32_t)floor(unif_rand()*(double)uset_size); /* 0..uset_size-1 */
                    UChar32 c = uset->charAt(idx);
                    if (c < 0) throw StriException(MSG__INTERNAL_ERROR);

                    if (c == 0)
                        embedded_nul = true;
                    U8_APPEND((uint8_t*)bufdata, j, bufsize, c, err);
                    if (err) throw StriException(MSG__INTERNAL_ERROR);
                }

                // Deviation from stringi: Builder accepts U+0000. Reject it
                // after this record's draws, then release RNG state normally;
                // stringi's R long-jump skips PutRNGstate in this pathology.
                if (embedded_nul)
                    throw StriException("embedded nul in string");

                ci::builder_set(
                    *builder, i, bufdata, j,
                    cetype_ext_t::CE_ASCII_OR_UTF8
                );
            }
            }
            STRI__PROTECT(ret = builder->to_sexp());
        }
        catch (...) {
            pending_error = std::current_exception();
        }

        // Deviation from stringi: release Reader and ICU owners before the
        // Builder's R boundary, then destroy its emptied or failed storage
        // before PutRNGstate on both success and failure.
        builder.reset();
        ci__rng_end(rng_active);
        if (pending_error)
            std::rethrow_exception(pending_error);
    }

    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END({ /* RNG has already been released */ })
}
