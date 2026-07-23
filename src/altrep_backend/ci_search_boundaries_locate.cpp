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

#include <deque>
#include <utility>


/**
 * Locate first or last boundaries
 *
 * @param str character vector
 * @param opts_brkiter list
 * @param first looking for first or last match?
 *
 * @return integer matrix (2 columns)
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-05)
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29)
 *     get_length
 */
SEXP ci__locate_firstlast_boundaries(
    SEXP str, SEXP opts_brkiter, bool first, bool get_length1
) {
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
        return Rf_allocMatrix(INTSXP, str_length, 2);
    }));
    int* ret_tab = INTEGER(ret);

    {
        StriContainerUTF8_indexable str_cont(
            context, str, str_length
        );
        StriRuleBasedBreakIterator brkiter(opts_brkiter2);

        for (R_len_t i = 0; i < str_length; ++i)
        {
            ret_tab[i]            = NA_INTEGER;
            ret_tab[i+str_length] = NA_INTEGER;

            if (str_cont.isNA(i))
                continue;

            if (get_length1) {
                ret_tab[i]            = -1;
                ret_tab[i+str_length] = -1;
            }

            if (str_cont.get(i).length() == 0) {
                continue;
            }

            brkiter.setupMatcher(
                str_cont.get(i).data(), str_cont.get(i).length(),
                STRI__DEFERRED_WARNINGS
            );
            pair<R_len_t,R_len_t> curpair;

            if (first) {
                brkiter.first();
                if (!brkiter.next(curpair)) {
                    continue;
                }
            }
            else {
                brkiter.last();
                if (!brkiter.previous(curpair)) {
                    continue;
                }
            }

            ret_tab[i]            = curpair.first;
            ret_tab[i+str_length] = curpair.second;

            // Adjust UTF8 byte index -> UChar32 index
            str_cont.UTF8_to_UChar32_index(i,
                                           ret_tab+i, ret_tab+i+str_length, 1,
                                           1, // 0-based index -> 1-based
                                           0  // end returns position of next character after match
                                          );

            if (get_length1) ret_tab[i+str_length] -= ret_tab[i] - 1;  // to->length
        }
    }

    charport::unwind_protect([&]() -> SEXP {
        ci__locate_set_dimnames_matrix(ret, get_length1);
        return R_NilValue;
    });
    }
    STRI__DEFERRED_WARNINGS.emit();

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END( ;/* do nothing special on error */ )
}


/**
 * Locate first boundary
 *
 * @param str character vector
 * @param opts_brkiter list
 * @return integer matrix (2 columns)
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-05)
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29)
 *     get_length
 */
SEXP ci_locate_first_boundaries(SEXP str, SEXP opts_brkiter, SEXP get_length)
{
    bool get_length1 = ci__prepare_arg_logical_1_notNA(get_length, "get_length");
    return ci__locate_firstlast_boundaries(str, opts_brkiter, true, get_length1);
}


/**
 * Locate last boundary
 *
 * @param str character vector
 * @param opts_brkiter list
 * @return integer matrix (2 columns)
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-05)
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29)
 *     get_length
 */
SEXP ci_locate_last_boundaries(SEXP str, SEXP opts_brkiter, SEXP get_length)
{
    bool get_length1 = ci__prepare_arg_logical_1_notNA(get_length, "get_length");
    return ci__locate_firstlast_boundaries(str, opts_brkiter, false, get_length1);
}


/** Locate all BreakIterator boundaries
 *
 * @param str character vector
 * @param omit_no_match logical
 * @param opts_brkiter named list
 * @return list
 *
 * @version 0.2-2 (Marek Gagolewski, 2014-04-22)
 *
 * @version 0.2-2 (Marek Gagolewski, 2014-04-23)
 *          removed "title": For Unicode 4.0 and above title boundary
 *          iteration, please use Word Boundary iterator.
 *
 * @version 0.2-2 (Marek Gagolewski, 2014-04-25)
 *          use ci__split_or_locate_boundaries
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-10-29)
 *          use opts_brkiter
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-28)
 *          new args: omit_no_match
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-02)
 *          use StriRuleBasedBreakIterator
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29)
 *     get_length
 */
SEXP ci_locate_all_boundaries(SEXP str, SEXP omit_no_match, SEXP opts_brkiter, SEXP get_length)
{
    bool omit_no_match1 = ci__prepare_arg_logical_1_notNA(omit_no_match, "omit_no_match");
    bool get_length1 = ci__prepare_arg_logical_1_notNA(get_length, "get_length");
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

    {
        StriContainerUTF8_indexable str_cont(
            context, str, str_length
        );
        StriRuleBasedBreakIterator brkiter(opts_brkiter2);
        STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
            return Rf_allocVector(VECSXP, str_length);
        }));
        std::deque< std::pair<R_len_t, R_len_t> > occurrences;
        std::pair<R_len_t, R_len_t> curpair;

        // Deviation from stringi: one loop-level unwind bridge protects every
        // child allocation and list write while the Reader and ICU owners are
        // live. The reusable scratch deque lives outside the callback, so it
        // is destroyed on an R error without paying one bridge per child.
        charport::unwind_protect([&]() -> SEXP {
          for (R_len_t i = 0; i < str_length; ++i)
          {
            ci::UnwindCallbackProtector protector;
            occurrences.clear();
            if (str_cont.isNA(i)) {
                SEXP ans;
                ans = protector.protect(ci__matrix_NA_INTEGER(1, 2));
                SET_VECTOR_ELT(ret, i, ans);
                continue;
            }

            brkiter.setupMatcher(
                str_cont.get(i).data(), str_cont.get(i).length(),
                STRI__DEFERRED_WARNINGS
            );
            brkiter.first();

            while (brkiter.next(curpair))
                occurrences.push_back(curpair);

            R_len_t noccurrences = (R_len_t)occurrences.size();
            if (noccurrences <= 0) {
                SEXP ans;
                ans = protector.protect(ci__matrix_NA_INTEGER(
                    omit_no_match1?0:1, 2,
                    get_length1?-1:NA_INTEGER
                ));
                SET_VECTOR_ELT(ret, i, ans);
                continue;
            }

            SEXP ans;
            ans = protector.protect(Rf_allocMatrix(
                INTSXP, noccurrences, 2
            ));
            int* ans_tab = INTEGER(ans);
            for (R_len_t j = 0; j < noccurrences; ++j) {
                ans_tab[j] = occurrences[j].first;
                ans_tab[j+noccurrences] = occurrences[j].second;
            }

            // Adjust UChar index -> UChar32 index (1-2 byte UTF16 to 1 byte UTF32-code points)
            str_cont.UTF8_to_UChar32_index(i, ans_tab,
                                           ans_tab+noccurrences, noccurrences,
                                           1, // 0-based index -> 1-based
                                           0  // end returns position of next character after match
                                          );

            if (get_length1) {
                for (R_len_t j=0; j < noccurrences; ++j)
                    ans_tab[j+noccurrences] -= ans_tab[j] - 1;  // to->length
            }

            SET_VECTOR_ELT(ret, i, ans);
          }

          ci__locate_set_dimnames_list(ret, get_length1);
          return R_NilValue;
        });
    }
    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END({ /* nothing special t.b.d. on error */ })
}
