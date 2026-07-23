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
#include "ci_container_utf8.h"
#include "ci_container_regex.h"


/**
 * Detect if a pattern occurs in a string
 *
 * @param str R character vector
 * @param pattern R character vector containing regular expressions
 * @param omit_na single logical value
 * @param opts_regex list
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
 * @version 0.4-1 (Marek Gagolewski, 2014-12-04)
 *    #122: omit_na arg added
 *
 * @version 1.0-2 (Marek Gagolewski, 2016-01-29)
 *    #214: allow a regex pattern like `.*`  to match an empty string
 *
 * @version 1.0-3 (Marek Gagolewski, 2016-02-03)
 *    #216: `negate` arg added
 *
 * @version 1.4.7 (Marek Gagolewski, 2020-08-24)
 *    Use StriContainerRegexPattern::getRegexOptions
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-17)
 *    assure LENGTH(pattern) <= LENGTH(str)
 */
SEXP ci_subset_regex(SEXP str, SEXP pattern, SEXP omit_na, SEXP negate, SEXP opts_regex)
{
    bool negate_1 = ci__prepare_arg_logical_1_notNA(negate, "negate");
    bool omit_na1 = ci__prepare_arg_logical_1_notNA(omit_na, "omit_na");
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));

    STRI__ERROR_HANDLER_BEGIN(2)
    SEXP ret;
    {
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    R_len_t str_n = ci::checked_r_len(
        context.size(str), "character vectors"
    );
    R_len_t pattern_n = ci::checked_r_len(
        context.size(pattern), "character vectors"
    );
    R_len_t vectorize_length = 0;
    // Deviation from stringi: defer this controlled length error until the
    // outer C++ boundary has released operation state.
    charport::unwind_protect([&]() -> SEXP {
        if (str_n > 0 && str_n < pattern_n)
            throw StriException(MSG__WARN_RECYCLING_RULE2);
        vectorize_length = ci__recycling_rule(
            false, 2, str_n, pattern_n
        );
        return R_NilValue;
    });
    // Deviation from stringi: queue the recycling warning until regex/Reader
    // owners have been released.
    if (vectorize_length > 0 &&
            (vectorize_length % str_n != 0 ||
             vectorize_length % pattern_n != 0))
        context.warn(MSG__WARN_RECYCLING_RULE);

    if (vectorize_length == 0) {
        charport::charvec::Store output(0, 0);
        STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
        STRI__UNPROTECT_ALL
        return ret;
    }

    StriRegexMatcherOptions pattern_opts;
    charport::unwind_protect([&]() -> SEXP {
        pattern_opts = StriContainerRegexPattern::getRegexOptions(
            STRI__DEFERRED_WARNINGS, opts_regex
        );
        return R_NilValue;
    });

    charport::charvec::Store output(0, 0);
    int result_counter = 0;
    {
        StriContainerUTF16 str_cont(context, str, vectorize_length);
        StriContainerRegexPattern pattern_cont(
            context, pattern, vectorize_length, pattern_opts
        );

        // BT: this cannot be done with deque, because pattern is reused so i does not
        // go like 0,1,2...n but 0,pat_len,2*pat_len,1,pat_len+1 and so on
        // MG: agreed
        std::vector<int> which(vectorize_length);

        for (R_len_t i = pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            STRI__CONTINUE_ON_EMPTY_OR_NA_PATTERN(str_cont, pattern_cont,
            {if (omit_na1) which[i] = FALSE; else {
                    which[i] = NA_LOGICAL;
                    result_counter++;
                }
            })

            RegexMatcher *matcher = pattern_cont.getMatcher(i); // will be deleted automatically
            matcher->reset(str_cont.get(i));
            UErrorCode status = U_ZERO_ERROR;
            which[i] = (int)matcher->find(status);
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
            if (negate_1) which[i] = !which[i];
            if (which[i]) result_counter++;
        }

        output = ci__subset_by_logical(str_cont, which, result_counter);
    }

    STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/**
 * Substitutes vector elements if a pattern occurs in a string
 *
 * @param str character vector
 * @param pattern character vector
 * @param opts_regex list
 * @param value character vector
 * @return character vector
 *
 * @version 1.0-3 (Marek Gagolewski, 2016-02-03)
 *   #124
 *
 * @version 1.0-3 (Marek Gagolewski, 2016-02-03)
 *   #216: `negate` arg added
 *
 * @version 1.4.7 (Marek Gagolewski, 2020-08-24)
 *    Use StriContainerRegexPattern::getRegexOptions
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-17)
 *    assure LENGTH(pattern) and LENGTH(value) <= LENGTH(str)
 */
SEXP ci_subset_regex_replacement(SEXP str, SEXP pattern, SEXP negate, SEXP opts_regex, SEXP value)
{
    bool negate_1 = ci__prepare_arg_logical_1_notNA(negate, "negate");
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));
    PROTECT(value = ci__prepare_arg_string(value, "value"));

    UText* str_text = NULL; // might be slower, but definitely is more convenient!

    STRI__ERROR_HANDLER_BEGIN(3)
    SEXP ret;
    {
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    R_len_t value_length = ci::checked_r_len(
        context.size(value), "character vectors"
    );
    R_len_t pattern_n = ci::checked_r_len(
        context.size(pattern), "character vectors"
    );
    R_len_t str_n = ci::checked_r_len(
        context.size(str), "character vectors"
    );

    // we are subsetting `str`, therefore recycling is slightly different here
    charport::unwind_protect([&]() -> SEXP {
        // Deviation from stringi: signal controlled validation failures only
        // after the outer C++ boundary has released operation state.
        if (value_length == 0) throw StriException(MSG__REPLACEMENT_ZERO);
        if (pattern_n == 0) throw StriException(MSG__WARN_EMPTY_VECTOR);
        if (str_n > 0 && str_n < pattern_n)  // for value_length, we emit warning later on
            throw StriException(MSG__WARN_RECYCLING_RULE2);
        return R_NilValue;
    });
    if (str_n == 0) {
        charport::charvec::Builder output(0);
        STRI__PROTECT(ret = output.to_sexp());
        STRI__UNPROTECT_ALL
        return ret;
    }
    // Deviation from stringi: queue the substitution recycling warning until
    // regex/Reader/UText owners and the output Builder have been released.
    if ((str_n % pattern_n) != 0)
        context.warn(MSG__WARN_RECYCLING_RULE);
    R_len_t vectorize_length = str_n;

    StriRegexMatcherOptions pattern_opts;
    charport::unwind_protect([&]() -> SEXP {
        pattern_opts = StriContainerRegexPattern::getRegexOptions(
            STRI__DEFERRED_WARNINGS, opts_regex
        );
        return R_NilValue;
    });

    charport::charvec::Builder output(vectorize_length);
    std::vector<int> detected(vectorize_length, 0);
    {
        StriContainerUTF8 value_cont(context, value, value_length);
        StriContainerUTF8 str_cont(context, str, vectorize_length);
        StriContainerRegexPattern pattern_cont(
            context, pattern, vectorize_length, pattern_opts
        );

        for (R_len_t i = pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            if (pattern_cont.isNA(i)) {
                // behave like `[<-`
                detected[i] = false;
                continue;
            }
            STRI__CONTINUE_ON_EMPTY_OR_NA_PATTERN(str_cont, pattern_cont,
            {detected[i] = NA_INTEGER;})

            UErrorCode status = U_ZERO_ERROR;
            RegexMatcher *matcher = pattern_cont.getMatcher(i); // will be deleted automatically
            str_text = utext_openUTF8(
                str_text, str_cont.get(i).data(),
                str_cont.get(i).length(), &status
            );
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
            matcher->reset(str_text);

            bool found = matcher->find(status);
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
            detected[i] = ((found && !negate_1) || (!found && negate_1));
        }

        R_len_t k = 0;  // we must traverse `str_cont` in order now
        for (R_len_t i = 0; i<vectorize_length; ++i) {
            if (detected[i] == NA_INTEGER)
                output.set_na(i);
            else if (detected[i] == 0)
                ci::builder_set(output, i, str_cont.get(i));
            else
                ci::builder_set(output, i, value_cont.get((k++)%value_length));
        }
        if ((k % value_length) != 0)
            context.warn(MSG_REPLACEMENT_MULTIPLE);

        if (str_text) {
            utext_close(str_text);
            str_text = NULL;
        }
    }

    STRI__PROTECT(ret = output.to_sexp());
    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END({
        if (str_text) {
            utext_close(str_text);
            str_text = NULL;
        }
    })
}
