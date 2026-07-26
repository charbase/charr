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
#include "ci_container_utf16.h"
#include "ci_utf8.h"
#include "ci_container_usearch.h"
#include <unicode/uregex.h>
#include <vector>


/**
 * Detect if a pattern occurs in a string [with collation]
 *
 * @param str character vector
 * @param pattern character vector
 * @param omit_na single logical value
 * @param opts_collator passed to ci__ucol_open(),
 * if {NA}, then {ci_detect_fixed_byte} is called
 *
 * @return character vector
 *
 * @version 0.3-1 (Bartek Tartanus, 2014-07-25)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-10-17)
 *                using std::vector<int> to avoid mem-leaks
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-06)
 *    Added missing ucol_close
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-04)
 *    #122: omit_na arg added
 *
 * @version 1.0-3 (Marek Gagolewski, 2016-02-03)
 *    #216: `negate` arg added
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-17)
 *    assure LENGTH(pattern) <= LENGTH(str)
 */
SEXP ci_subset_coll(SEXP str, SEXP pattern, SEXP omit_na, SEXP negate, SEXP opts_collator)
{
    bool negate_1 = ci__prepare_arg_logical_1_notNA(negate, "negate");
    bool omit_na1 = ci__prepare_arg_logical_1_notNA(omit_na, "omit_na");
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));

    UCollator* collator = NULL;
    STRI__ERROR_HANDLER_BEGIN(2)
    R_len_t str_n = 0;
    R_len_t pattern_n = 0;
    R_len_t vectorize_length = 0;
    charport::unwind_protect([&]() -> SEXP {
        str_n = LENGTH(str);
        pattern_n = LENGTH(pattern);
        // Deviation from stringi: keep controlled recycling conditions on the
        // C++ path so R signalling happens after operation cleanup.
        if (str_n > 0 && str_n < pattern_n)
            throw StriException(MSG__WARN_RECYCLING_RULE2);
        vectorize_length = ci__recycling_rule(
            STRI__DEFERRED_WARNINGS, 2, str_n, pattern_n
        );
        return R_NilValue;
    });

    if (vectorize_length == 0) {
        charport::charvec::Store output(0, 0);
        SEXP ret;
        STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
        STRI__UNPROTECT_ALL
        return ret;
    }

    // Deviation from stringi: catch R errors from collator option parsing so
    // queued warnings and any opened collator are released before R resumes.
    charport::unwind_protect([&]() -> SEXP {
        collator = ci__ucol_open(
            STRI__DEFERRED_WARNINGS, opts_collator
        );
        return R_NilValue;
    });

    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    charport::charvec::Store output(0, 0);
    int result_counter = 0;
    {
        StriContainerUTF16 str_cont(
            context, str, vectorize_length
        );
        StriContainerUStringSearch pattern_cont(
            context, pattern, vectorize_length, collator
        );  // collator is not owned by pattern_cont

        // BT: this cannot be done with deque, because pattern is reused so i
        // does not go like 0,1,2...n but 0,pat_len,2*pat_len,1,...
        // MG: agreed
        std::vector<int> which(vectorize_length);

        for (R_len_t i = pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            STRI__CONTINUE_ON_EMPTY_OR_NA_STR_PATTERN(
                str_cont, pattern_cont,
            {   if (omit_na1)
                    which[i] = FALSE;
                else {
                    which[i] = NA_LOGICAL;
                    result_counter++;
                }
            },
            {   which[i] = negate_1;
                if (which[i]) result_counter++;
            })

            UStringSearch *matcher = pattern_cont.getMatcher(
                i, str_cont.get(i)
            );
            usearch_reset(matcher);
            UErrorCode status = U_ZERO_ERROR;
            which[i] = (
                (int)usearch_first(matcher, &status) != USEARCH_DONE
            );  // this is F*G slow! :-(
            if (negate_1) which[i] = !which[i];
            if (which[i]) result_counter++;
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
        }

        output = ci__subset_by_logical(str_cont, which, result_counter);
    }

    SEXP ret;
    STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
    if (collator) {
        ucol_close(collator);
        collator = NULL;
    }
    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(
    if (collator) {
    ucol_close(collator);
        collator = NULL;
    }
    )
}


/**
 * Substitutes vector elements if a pattern occurs in a string
 *
 * @param str character vector
 * @param pattern character vector
 * @param value character vector
 * @param opts_collator list
 * @return character vector
 *
 * @version 1.0-3 (Marek Gagolewski, 2016-02-03)
 *   #124
 *
 * @version 1.0-3 (Marek Gagolewski, 2016-02-03)
 *   #216: `negate` arg added
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-17)
 *    assure LENGTH(pattern) and LENGTH(value) <= LENGTH(str)
 */
SEXP ci_subset_coll_replacement(SEXP str, SEXP pattern, SEXP negate, SEXP opts_collator, SEXP value)
{
    bool negate_1 = ci__prepare_arg_logical_1_notNA(negate, "negate");
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));
    PROTECT(value = ci__prepare_arg_string(value, "value"));

    UCollator* collator = NULL;
    STRI__ERROR_HANDLER_BEGIN(3)
    R_len_t value_length = 0;
    R_len_t pattern_length = 0;
    R_len_t vectorize_length = 0;
    charport::unwind_protect([&]() -> SEXP {
        value_length = LENGTH(value);
        pattern_length = LENGTH(pattern);
        vectorize_length = LENGTH(str);

        // we are subsetting `str`, therefore recycling is slightly different
        // Deviation from stringi: queue or throw controlled conditions so R
        // handlers run only after operation cleanup.
        if (value_length == 0)
            throw StriException(MSG__REPLACEMENT_ZERO);
        if (pattern_length == 0)
            throw StriException(MSG__WARN_EMPTY_VECTOR);
        if (vectorize_length > 0 && vectorize_length < pattern_length)
            throw StriException(MSG__WARN_RECYCLING_RULE2);
        if (vectorize_length > 0 &&
                (vectorize_length % pattern_length) != 0)
            STRI__DEFERRED_WARNINGS.push(MSG__WARN_RECYCLING_RULE);
        return R_NilValue;
    });

    if (vectorize_length == 0) {
        charport::charvec::Builder output(0);
        SEXP ret;
        STRI__PROTECT(ret = output.to_sexp());
        STRI__UNPROTECT_ALL
        return ret;
    }

    // Deviation from stringi: catch R errors from collator option parsing so
    // queued warnings and any opened collator are released before R resumes.
    charport::unwind_protect([&]() -> SEXP {
        collator = ci__ucol_open(
            STRI__DEFERRED_WARNINGS, opts_collator
        );
        return R_NilValue;
    });

    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    SEXP ret;
    {
        charport::charvec::Builder output(vectorize_length);
        std::vector<int> detected(vectorize_length, 0);
        {
            Utf8Input value_cont(
                context, value, value_length
            );
            StriContainerUTF16 str_cont(
                context, str, vectorize_length
            );
            StriContainerUStringSearch pattern_cont(
                context, pattern, vectorize_length, collator
            );  // collator is not owned by pattern_cont

            for (R_len_t i = pattern_cont.vectorize_init();
                    i != pattern_cont.vectorize_end();
                    i = pattern_cont.vectorize_next(i))
            {
                if (pattern_cont.isNA(i)) {
                    // behave like `[<-`
                    detected[i] = false;
                    continue;
                }
                STRI__CONTINUE_ON_EMPTY_OR_NA_STR_PATTERN(
                    str_cont, pattern_cont,
                    {detected[i] = NA_INTEGER;},
                    {detected[i] = negate_1;}
                );

                UStringSearch *matcher = pattern_cont.getMatcher(
                    i, str_cont.get(i)
                );
                usearch_reset(matcher);
                UErrorCode status = U_ZERO_ERROR;
                detected[i] = (
                    ((int)usearch_first(matcher, &status) != USEARCH_DONE &&
                        !negate_1) ||
                    (usearch_first(matcher, &status) == USEARCH_DONE &&
                        negate_1)
                );
                STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
            }

            vector<char> utf8_buffer;
            R_len_t k = 0;  // we must traverse `str_cont` in order now
            for (R_len_t i = 0; i<vectorize_length; ++i) {
                if (detected[i] == NA_INTEGER)
                    output.set_na(i);
                else if (detected[i] == 0)
                    ci::builder_set(
                        output, i, str_cont.get(i), utf8_buffer
                    );
                else
                    ci::builder_set(
                        output, i, value_cont.get((k++)%value_length)
                    );
            }
            if ((k % value_length) != 0)
                context.warn(MSG_REPLACEMENT_MULTIPLE);
        }

        STRI__PROTECT(ret = output.to_sexp());
    }

    if (collator) {
        ucol_close(collator);
        collator = NULL;
    }
    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(
    if (collator) {
    ucol_close(collator);
        collator = NULL;
    }
    )
}
