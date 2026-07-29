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
#include "collation/pattern_set.h"
#include "ci_utf16_cursor.h"

namespace charr { namespace altrep_backend {


/**
 * Count pattern occurcess in a string [with collation]
 *
 * @param str character vector
 * @param pattern character vector
 * @param opts_collator passed to ci__ucol_open()
 * @return integer vector
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          corrected behavior on empty str/pattern
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-23)
 *          make StriException-friendly,
 *          use collation::PatternSet
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          new fun: ci_count_coll (opts_collator == NA not allowed)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci_count_coll(SEXP str, SEXP pattern, SEXP opts_collator)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));

    UCollator* collator = NULL;

    STRI__ERROR_HANDLER_BEGIN(2)
    // Deviation from stringi: catch R errors from collator option parsing so
    // queued warnings and any opened collator are released before R resumes.
    ci::unwind_protect([&]() -> SEXP {
        collator = ci__ucol_open(
            STRI__DEFERRED_WARNINGS, opts_collator
        );
        return R_NilValue;
    });
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    R_len_t str_n = ci::checked_r_len(
        context.size(str), "character vectors"
    );
    R_len_t pattern_n = ci::checked_r_len(
        context.size(pattern), "character vectors"
    );
    R_len_t vectorize_length = 0;
    // Deviation from stringi: queue recycling warnings while the collator is
    // live and emit them after the collator closes.
    ci::unwind_protect([&]() -> SEXP {
        vectorize_length = ci__recycling_rule(
            false, 2, str_n, pattern_n
        );
        if (vectorize_length > 0 &&
                (vectorize_length%str_n != 0 ||
                 vectorize_length%pattern_n != 0))
            STRI__DEFERRED_WARNINGS.push(MSG__WARN_RECYCLING_RULE);
        return R_NilValue;
    });

    SEXP ret;
    STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
        return Rf_allocVector(INTSXP, vectorize_length);
    }));
    int* ret_tab = INTEGER(ret);

    {
        ci::Utf16Cursor str_cont(context, str, vectorize_length);
        collation::PatternSet pattern_cont(
            context, pattern, vectorize_length, collator
        );  // collator is not owned by pattern_cont

        for (R_len_t i = pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            const UnicodeString& source = str_cont.get(i);
            if (source.isBogus() || pattern_cont.isNA(i) ||
                    pattern_cont.get(i).length() <= 0) {
                ret_tab[i] = NA_INTEGER;
                continue;
            }
            if (source.length() <= 0) {
                ret_tab[i] = 0;
                continue;
            }

            // getMatcher() has just installed this record's text, which
            // restarts iteration on its own. A usearch_reset() here would only
            // re-derive collator state that has not changed since the matcher
            // was opened.
            UStringSearch *matcher = pattern_cont.getMatcher(i, source);
            UErrorCode status = U_ZERO_ERROR;
            R_len_t found = 0;
            while (!U_FAILURE(status) && ((int)usearch_next(matcher, &status) != USEARCH_DONE))
                ++found;
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
            ret_tab[i] = found;
        }
    }

    if (collator) {
        ucol_close(collator);
        collator=NULL;
    }
    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(
        if (collator) ucol_close(collator);
    )
    }

} } // namespace charr::altrep_backend
