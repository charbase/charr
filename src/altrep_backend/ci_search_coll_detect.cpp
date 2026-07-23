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
#include "ci_container_utf16.h"
#include "ci_container_usearch.h"
#include <unicode/uregex.h>


/**
 * Detect if a pattern occurs in a string [with collation]
 *
 * @param str character vector
 * @param pattern character vector
 * @param negate single bool
 * @param max_count single int
 * @param opts_collator passed to ci__ucol_open(),
 * if \code{NA}, then \code{ci_detect_fixed_byte} is called
 * @return logical vector
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *    corrected behavior on empty str/pattern
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-22)
 *    make StriException-friendly, use StriContainerUStringSearch
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          new fun: ci_detect_coll (opts_collator == NA not allowed)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.0-3 (Marek Gagolewski, 2016-02-03)
 *    FR #216: `negate` arg added
 *
 * @version 1.3.1 (Marek Gagolewski, 2019-02-08)
 *    #232: `max_count` arg added
 */
SEXP ci_detect_coll(SEXP str, SEXP pattern, SEXP negate,
                      SEXP max_count, SEXP opts_collator)
{
    bool negate_1 = ci__prepare_arg_logical_1_notNA(negate, "negate");
    int max_count_1 = ci__prepare_arg_integer_1_notNA(max_count, "max_count");
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));

    UCollator* collator = NULL;

    STRI__ERROR_HANDLER_BEGIN(2)
    // Deviation from stringi: catch R errors from collator option parsing so
    // queued warnings and any opened collator are released before R resumes.
    charport::unwind_protect([&]() -> SEXP {
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
    charport::unwind_protect([&]() -> SEXP {
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
    STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
        return Rf_allocVector(LGLSXP, vectorize_length);
    }));
    int* ret_tab = LOGICAL(ret);

    {
        StriContainerUTF16 str_cont(context, str, vectorize_length);
        StriContainerUStringSearch pattern_cont(
            context, pattern, vectorize_length, collator
        );  // collator is not owned by pattern_cont

        for (R_len_t i = pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            if (max_count_1 == 0) {
                ret_tab[i] = NA_LOGICAL;
                continue;
            }

            STRI__CONTINUE_ON_EMPTY_OR_NA_STR_PATTERN(str_cont, pattern_cont,
                    ret_tab[i] = NA_LOGICAL,
            {   ret_tab[i] = negate_1;
                if (max_count_1 > 0 && ret_tab[i]) --max_count_1;
            })

            UErrorCode status;
            UStringSearch *matcher = pattern_cont.getMatcher(i, str_cont.get(i));
            usearch_reset(matcher);


            status = U_ZERO_ERROR;
            ret_tab[i] = ((int)usearch_first(matcher, &status) != USEARCH_DONE);  // this is slow! :-(
            //ret_tab[i] = ((int)usearch_search(matcher, 0, NULL, NULL, &status));  // this is slow! :-(
            if (negate_1) ret_tab[i] = !ret_tab[i];
            if (max_count_1 > 0 && ret_tab[i]) --max_count_1;
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
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
