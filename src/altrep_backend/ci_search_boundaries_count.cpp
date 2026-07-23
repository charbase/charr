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
#include "ci_container_utf8_indexable.h"
#include "ci_container_integer.h"
#include "ci_brkiter.h"


/** Count the number of BreakIterator boundaries
 *
 * @param str character vector
 * @param opts_brkiter identifier
 * @return character vector
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-10-30)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-02)
 *          use StriRuleBasedBreakIterator
 */
SEXP ci_count_boundaries(SEXP str, SEXP opts_brkiter)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));

    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP ret;
    {
    // Deviation from stringi: keep the option's ICU storage inside the
    // unwind-safe scope so it is released before warning replay.
    StriBrkIterOptions opts_brkiter2(
        opts_brkiter, "line_break", STRI__DEFERRED_WARNINGS
    );
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    R_len_t str_length = ci::checked_r_len(
        context.size(str), "character vectors"
    );

    STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
        return Rf_allocVector(INTSXP, str_length);
    }));
    int* ret_tab = INTEGER(ret);

    {
        StriContainerUTF8_indexable str_cont(
            context, str, str_length
        );
        StriRuleBasedBreakIterator brkiter(opts_brkiter2);

        for (R_len_t i = 0; i < str_length; ++i)
        {
            if (str_cont.isNA(i)) {
                ret_tab[i] = NA_INTEGER;
                continue;
            }

            brkiter.setupMatcher(
                str_cont.get(i).data(), str_cont.get(i).length(),
                STRI__DEFERRED_WARNINGS
            );
            brkiter.first();

            R_len_t cur_count = 0;
            while (brkiter.next())
                ++cur_count;

            ret_tab[i] = cur_count;
        }
    }

    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END({ /* no action */  })
}
