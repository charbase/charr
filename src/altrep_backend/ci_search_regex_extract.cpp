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
#include "ci_container_regex.h"
#include <deque>
#include <stdexcept>
#include <utility>
#include <vector>
using namespace std;


/**
 * Extract first occurrence of a regex pattern in each string
 *
 * @param str character vector
 * @param pattern character vector
 * @param opts_regex list
 * @param first logical - search for the first or the last occurrence?
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-20)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.0-2 (Marek Gagolewski, 2016-01-29)
 *    Issue #214: allow a regex pattern like `.*`  to match an empty string
 *
 * @version 1.4.7 (Marek Gagolewski, 2020-08-24)
 *    Use StriContainerRegexPattern::getRegexOptions
 */
SEXP ci__extract_firstlast_regex(SEXP str, SEXP pattern, SEXP opts_regex, bool first)
{
    PROTECT(str = ci__prepare_arg_string(str, "str")); // prepare string argument
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern")); // prepare string argument

    UText* str_text = NULL; // may potentially be slower, but definitely is more convenient!
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
    charport::unwind_protect([&]() -> SEXP {
        vectorize_length = ci__recycling_rule(
            false, 2, str_n, pattern_n
        );
        return R_NilValue;
    });
    // Deviation from stringi: queue the recycling warning until the regex,
    // Reader, UText, and output Builder owners have all been released.
    if (vectorize_length > 0 &&
            (vectorize_length % str_n != 0 ||
             vectorize_length % pattern_n != 0))
        context.warn(MSG__WARN_RECYCLING_RULE);

    StriRegexMatcherOptions pattern_opts;
    // Deviation from stringi: preserve recycling-before-options order while
    // routing option-parser R unwinds through the common error boundary.
    charport::unwind_protect([&]() -> SEXP {
        pattern_opts = StriContainerRegexPattern::getRegexOptions(
            STRI__DEFERRED_WARNINGS, opts_regex
        );
        return R_NilValue;
    });

    charport::charvec::Builder output(vectorize_length);
    {
        StriContainerUTF8 str_cont(context, str, vectorize_length);
        StriContainerRegexPattern pattern_cont(
            context, pattern, vectorize_length, pattern_opts
        );

        for (R_len_t i = pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            STRI__CONTINUE_ON_EMPTY_OR_NA_PATTERN(
                str_cont, pattern_cont, output.set_na(i);
            )

            UErrorCode status = U_ZERO_ERROR;
            RegexMatcher *matcher = pattern_cont.getMatcher(i); // will be deleted automatically
            str_text = utext_openUTF8(
                str_text, str_cont.get(i).data(),
                str_cont.get(i).length(), &status
            );
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

            int m_start = -1;
            int m_end = -1;
            int m_res;
            matcher->reset(str_text);
            m_res = (int)matcher->find(status);
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
            if (m_res) { // find first match
                m_start = (int)matcher->start(status); // The **native** position in the input string :-)
                STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
                m_end   = (int)matcher->end(status);
                STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
            }
            else {
                output.set_na(i);
                continue;
            }

            if (!first) { // continue searching
                while (1) {
                    m_res = (int)matcher->find(status);
                    STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
                    if (!m_res) break;
                    m_start = (int)matcher->start(status);
                    m_end   = (int)matcher->end(status);
                    STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
                }
            }

            ci::builder_set(
                output, i, str_cont.get(i).data()+m_start,
                m_end-m_start, cetype_ext_t::CE_ASCII_OR_UTF8
            );
        }

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
    STRI__ERROR_HANDLER_END(if (str_text) utext_close(str_text);)
    }


/**
 * Extract first occurrence of a regex pattern in each string
 *
 * @param str character vector
 * @param pattern character vector
 * @param opts_regex list
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-20)
 */
SEXP ci_extract_first_regex(SEXP str, SEXP pattern, SEXP opts_regex)
{
    return ci__extract_firstlast_regex(str, pattern, opts_regex, true);
}


/**
 * Extract last occurrence of a regex pattern in each string
 *
 * @param str character vector
 * @param pattern character vector
 * @param opts_regex list
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-20)
 */
SEXP ci_extract_last_regex(SEXP str, SEXP pattern, SEXP opts_regex)
{
    return ci__extract_firstlast_regex(str, pattern, opts_regex, false);
}


/**
 * Extract all occurrences of a regex pattern in each string
 *
 * @param str character vector
 * @param pattern character vector
 * @param opts_regex list
 * @param simplify single logical value
 *
 * @return list of character vectors  or character matrix
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-20)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-10-24)
 *          added simplify param
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-27)
 *    FR #117: omit_no_match arg added
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-04)
 *    allow `simplify=NA`
 *
 * @version 1.0-2 (Marek Gagolewski, 2016-01-29)
 *    Issue #214: allow a regex pattern like `.*`  to match an empty string
 */
SEXP ci_extract_all_regex(SEXP str, SEXP pattern, SEXP simplify, SEXP omit_no_match, SEXP opts_regex)
{
    UText* str_text = NULL; // may potentially be slower, but definitely is more convenient!
    STRI__ERROR_HANDLER_BEGIN(0)
    // Deviation from stringi: begin the C++ boundary at the copied option-
    // parsing position so its controlled warnings can join the operation queue.
    StriRegexMatcherOptions pattern_opts;
    charport::unwind_protect([&]() -> SEXP {
        pattern_opts = StriContainerRegexPattern::getRegexOptions(
            STRI__DEFERRED_WARNINGS, opts_regex
        );
        return R_NilValue;
    });
    bool omit_no_match1 = false;
    charport::unwind_protect([&]() -> SEXP {
        omit_no_match1 = ci__prepare_arg_logical_1_notNA(
            omit_no_match, "omit_no_match", &STRI__DEFERRED_WARNINGS
        );
        return R_NilValue;
    });
    STRI__PROTECT(simplify = charport::unwind_protect([&]() -> SEXP {
        return ci__prepare_arg_logical_1(
            simplify, "simplify", &STRI__DEFERRED_WARNINGS
        );
    }));
    STRI__PROTECT(str = charport::unwind_protect([&]() -> SEXP {
        return ci__prepare_arg_string(
            str, "str", true, &STRI__DEFERRED_WARNINGS
        );
    }));
    STRI__PROTECT(pattern = charport::unwind_protect([&]() -> SEXP {
        return ci__prepare_arg_string(
            pattern, "pattern", true, &STRI__DEFERRED_WARNINGS
        );
    }));
    const int simplify1 = LOGICAL_RO(simplify)[0];
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
    charport::unwind_protect([&]() -> SEXP {
        vectorize_length = ci__recycling_rule(
            false, 2, str_n, pattern_n
        );
        return R_NilValue;
    });
    // Deviation from stringi: queue the recycling warning until the regex,
    // Reader, UText, Builder, Store, and result-staging owners are gone.
    if (vectorize_length > 0 &&
            (vectorize_length % str_n != 0 ||
             vectorize_length % pattern_n != 0))
        context.warn(MSG__WARN_RECYCLING_RULE);

    vector<charport::charvec::Store> stores;
    stores.reserve(static_cast<size_t>(vectorize_length));
    for (R_len_t i=0; i<vectorize_length; ++i)
        stores.push_back(charport::charvec::Store(0, 0));
    {
        StriContainerUTF8 str_cont(context, str, vectorize_length);
        StriContainerRegexPattern pattern_cont(
            context, pattern, vectorize_length, pattern_opts
        );
        charport::charvec::Builder builder(0);

        for (R_len_t i = pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            charport::charvec::Store& current = stores[
                static_cast<size_t>(i)
            ];
            STRI__CONTINUE_ON_EMPTY_OR_NA_PATTERN(
                str_cont, pattern_cont,
                current = charport::charvec::Store::scalar(
                    nullptr, 0, cetype_ext_t::CE_NA
                );
            )

            UErrorCode status = U_ZERO_ERROR;
            RegexMatcher *matcher = pattern_cont.getMatcher(i); // will be deleted automatically
            str_text = utext_openUTF8(
                str_text, str_cont.get(i).data(),
                str_cont.get(i).length(), &status
            );
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

            matcher->reset(str_text);

            deque< pair<R_len_t, R_len_t> > occurrences;
            int m_res;
            while (1) {
                m_res = (int)matcher->find(status);
                STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
                if (!m_res) break;

                occurrences.push_back(pair<R_len_t, R_len_t>(
                                          (R_len_t)matcher->start(status), (R_len_t)matcher->end(status)
                                      ));
                STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
            }

            R_len_t noccurrences = (R_len_t)occurrences.size();
            if (noccurrences <= 0) {
                if (!omit_no_match1)
                    current = charport::charvec::Store::scalar(
                        nullptr, 0, cetype_ext_t::CE_NA
                    );
                continue;
            }

            const char* str_cur_s = str_cont.get(i).data();
            if (noccurrences == 1) {
                const pair<R_len_t, R_len_t>& curo = occurrences.front();
                const char* value = str_cur_s+curo.first;
                const size_t length = static_cast<size_t>(
                    curo.second-curo.first
                );
                current = ci::scalar_store(
                    value, length, cetype_ext_t::CE_ASCII_OR_UTF8
                );
                continue;
            }

            builder.reset(noccurrences);
            R_xlen_t output_i = 0;
            deque< pair<R_len_t, R_len_t> >::iterator iter = occurrences.begin();
            for (; iter != occurrences.end(); ++iter) {
                pair<R_len_t, R_len_t> curo = *iter;
                ci::builder_set(
                    builder, output_i++, str_cur_s+curo.first,
                    curo.second-curo.first,
                    cetype_ext_t::CE_ASCII_OR_UTF8
                );
            }
            current = builder.release_store();
        }

        if (str_text) {
            utext_close(str_text);
            str_text = NULL;
        }
    }

    if (simplify1 != NA_LOGICAL && !simplify1) {
        STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
            return Rf_allocVector(VECSXP, vectorize_length);
        }));
        for (R_len_t i=0; i<vectorize_length; ++i) {
            SEXP current;
            STRI__PROTECT(current = charport::charvec::wrap(
                std::move(stores[i])
            ));
            SET_VECTOR_ELT(ret, i, current);
            STRI__UNPROTECT(1);
        }
    }
    else {
        size_t max_columns = 0;
        for (R_len_t i=0; i<vectorize_length; ++i) {
            if (stores[i].size() > max_columns)
                max_columns = stores[i].size();
        }

        // Deviation from stringi: the direct Store-to-Builder matrix path
        // checks its dimensions before narrowing or multiplying them.
        if (max_columns > static_cast<size_t>(R_LEN_T_MAX))
            throw length_error("matrix columns exceed R's integer limit");
        const R_xlen_t rows = vectorize_length;
        const R_xlen_t columns = static_cast<R_xlen_t>(max_columns);
        if (rows > 0 && columns > R_XLEN_T_MAX/rows)
            throw length_error("matrix length exceeds R's vector limit");

        charport::charvec::Builder matrix_builder(rows*columns);
        for (R_xlen_t i=0; i<rows; ++i) {
            const charport::charvec::Store& current = stores[i];
            const R_xlen_t current_size = static_cast<R_xlen_t>(
                current.size()
            );
            R_xlen_t j = 0;
            for (; j<current_size; ++j)
                matrix_builder.set(i+j*rows, current.view(j));
            for (; j<columns; ++j) {
                if (simplify1 == NA_LOGICAL) {
                    matrix_builder.set_na(i+j*rows);
                }
                else {
                    ci::builder_set(
                        matrix_builder, i+j*rows, "", 0,
                        cetype_ext_t::CE_ASCII
                    );
                }
            }
        }

        STRI__PROTECT(ret = matrix_builder.to_sexp());
        charport::unwind_protect([&]() -> SEXP {
            SEXP dim;
            PROTECT(dim = Rf_allocVector(INTSXP, 2));
            INTEGER(dim)[0] = vectorize_length;
            INTEGER(dim)[1] = static_cast<R_len_t>(max_columns);
            Rf_setAttrib(ret, R_DimSymbol, dim);
            UNPROTECT(1);
            return R_NilValue;
        });
    }

    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(if (str_text) utext_close(str_text);)
    }
