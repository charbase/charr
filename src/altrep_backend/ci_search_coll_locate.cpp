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
#include <utility>
#include <vector>

namespace charr { namespace altrep_backend {
using namespace std;


/**
 * Locate first or last occurrences of pattern in a string [with collation]
 *
 * @param str character vector
 * @param pattern character vector
 * @param opts_collator passed to ci__ucol_open(),
 * if \code{NA}, then \code{ci__locate_firstlast_fixed_byte} is called
 * @param first looking for first or last match?
 * @return integer matrix (2 columns)
 *
 * @version 0.1-?? (Bartlomiej Tartanus)
 *
 * @version 0.1-?? (Bartlomiej Tartanus, 2013-06-09)
 *          io::Utf16Input & collator
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-23)
 *          StriException friendly, use collation::PatternSet
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          new fun: ci_locate_firstlast_coll (opts_collator == NA not allowed)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29)
 *     get_length
 */
SEXP ci__locate_firstlast_coll(SEXP str, SEXP pattern, SEXP opts_collator, bool first, bool get_length1)
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
        return Rf_allocMatrix(INTSXP, vectorize_length, 2);
    }));
    int* ret_tab = INTEGER(ret);

    {
        ci::Utf16Cursor str_cont(
            context, str, vectorize_length
        );
        collation::PatternSet pattern_cont(
            context, pattern, vectorize_length, collator
        );  // collator is not owned by pattern_cont

        for (R_len_t i = pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            ret_tab[i]                  = NA_INTEGER;
            ret_tab[i+vectorize_length] = NA_INTEGER;
            STRI__CONTINUE_ON_EMPTY_OR_NA_STR_PATTERN(
                str_cont, pattern_cont,
                ;/*nothing on NA - keep NA_INTEGER*/,
                { if (get_length1) ret_tab[i] = ret_tab[i+vectorize_length] = -1; }
            )

            UStringSearch *matcher = pattern_cont.getMatcher(i, str_cont.get(i));
            usearch_reset(matcher);
            UErrorCode status = U_ZERO_ERROR;

            int start;
            if (first) {
                start = usearch_first(matcher, &status);
            } else {
                start = usearch_last(matcher, &status);
            }
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})


            if (start != USEARCH_DONE) {  // there is a match
                ret_tab[i]                  = start;
                ret_tab[i+vectorize_length] = start + usearch_getMatchedLength(matcher);

                // Adjust UChar index -> UChar32 index (1-2 byte UTF16 to 1 byte UTF32-code points)
                str_cont.UChar16_to_UChar32_index(i,
                                                  ret_tab+i, ret_tab+i+vectorize_length, 1,
                                                  1, // 0-based index -> 1-based
                                                  0  // end returns position of next character after match
                                                 );

                if (get_length1) ret_tab[i+vectorize_length] -= ret_tab[i] - 1;  // to->length
            }
            else if (get_length1) {
                // not found
                ret_tab[i+vectorize_length] = ret_tab[i] = -1;
            }
            // else NA_INTEGER already
        }
    }

    ci::unwind_protect([&]() -> SEXP {
        ci__locate_set_dimnames_matrix(ret, get_length1);
        return R_NilValue;
    });
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


/**
 * Locate first occurrences of pattern in a string [with collation]
 *
 * @param str character vector
 * @param pattern character vector
 * @param opts_collator list
 * @return integer matrix (2 columns)
 *
 * @version 0.1-?? (Bartlomiej Tartanus)
 *
 * @version 0.1-?? (Bartlomiej Tartanus, 2013-06-09)
 *          io::Utf16Input & collator
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-23)
 *          use ci_locate_firstlast_fixed
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          new fun: ci_locate_first_coll (opts_collator == NA not allowed)
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29)
 *     get_length
 */
SEXP ci_locate_first_coll(SEXP str, SEXP pattern, SEXP opts_collator, SEXP get_length)
{
    bool get_length1 = ci__prepare_arg_logical_1_notNA(get_length, "get_length");
    return ci__locate_firstlast_coll(str, pattern, opts_collator, true, get_length1);
}


/**
 * Locate all pattern occurrences in a string [with collation]
 *
 * @param str character vector
 * @param pattern character vector
 * @param opts_collator passed to ci__ucol_open(),
 * if \code{NA}, then \code{ci__locate_all_fixed_byte} is called
 * @return list of integer matrices (2 columns)
 *
 * @version 0.1-?? (Bartlomiej Tartanus)
 *
 * @version 0.1-?? (Bartlomiej Tartanus, 2013-06-09)
 *          io::Utf16Input & collator
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-23)
 *          StriException friendly, use collation::PatternSet
 *
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          new fun: ci_locate_all_coll (opts_collator == NA not allowed)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-27)
 *    FR #117: omit_no_match arg added
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29)
 *     get_length
 */
SEXP ci_locate_all_coll(SEXP str, SEXP pattern, SEXP omit_no_match, SEXP opts_collator, SEXP get_length)
{
    bool omit_no_match1 = ci__prepare_arg_logical_1_notNA(omit_no_match, "omit_no_match");
    bool get_length1 = ci__prepare_arg_logical_1_notNA(get_length, "get_length");
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
    {
        ci::Utf16Cursor str_cont(
            context, str, vectorize_length
        );
        collation::PatternSet pattern_cont(
            context, pattern, vectorize_length, collator
        );  // collator is not owned by pattern_cont
        STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
            return Rf_allocVector(VECSXP, vectorize_length);
        }));
        vector< pair<R_len_t, R_len_t> > occurrences;
        // These two result shapes are immutable and identical within a call.
        // Sharing them avoids repeated matrix allocation; R duplicates a
        // child under copy-on-modify if the caller later changes it.
        SEXP argument_na = R_NilValue;
        SEXP no_match = R_NilValue;

        // Deviation from stringi: one loop-level unwind bridge protects every
        // child allocation and list write while the Reader and ICU owners are
        // live. Reusable contiguous scratch lives outside the callback, so it
        // is destroyed on an R error without paying one bridge per child.
        ci::unwind_protect([&]() -> SEXP {
          for (R_len_t i = pattern_cont.vectorize_init();
                  i != pattern_cont.vectorize_end();
                  i = pattern_cont.vectorize_next(i))
          {
            ci::UnwindCallbackProtector protector;
            occurrences.clear();
            const UnicodeString& source = str_cont.get(i);
            if (source.isBogus() || pattern_cont.isNA(i) ||
                    pattern_cont.get(i).length() <= 0) {
                if (argument_na == R_NilValue)
                    argument_na = protector.hold(
                        ci__matrix_NA_INTEGER(1, 2)
                    );
                SET_VECTOR_ELT(ret, i, argument_na);
                continue;
            }
            else if (source.length() <= 0) {
                if (no_match == R_NilValue)
                    no_match = protector.hold(ci__matrix_NA_INTEGER(
                        omit_no_match1?0:1, 2,
                        get_length1?-1:NA_INTEGER
                    ));
                SET_VECTOR_ELT(ret, i, no_match);
                continue;
            }

            UStringSearch *matcher = pattern_cont.getMatcher(i, source);
            usearch_reset(matcher);

            UErrorCode status = U_ZERO_ERROR;
            int start = (int)usearch_first(matcher, &status);
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

            if (start == USEARCH_DONE) {
                if (no_match == R_NilValue)
                    no_match = protector.hold(ci__matrix_NA_INTEGER(
                        omit_no_match1?0:1, 2,
                        get_length1?-1:NA_INTEGER
                    ));
                SET_VECTOR_ELT(ret, i, no_match);
                continue;
            }

            while (start != USEARCH_DONE) {
                occurrences.emplace_back(
                    start, start+usearch_getMatchedLength(matcher)
                );
                start = usearch_next(matcher, &status);
                STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
            }

            R_len_t noccurrences = (R_len_t)occurrences.size();
            SEXP ans;
            ans = protector.hold(Rf_allocMatrix(
                INTSXP, noccurrences, 2
            ));
            int* ans_tab = INTEGER(ans);
            for (R_len_t j = 0; j < noccurrences; ++j) {
                ans_tab[j] = occurrences[j].first;
                ans_tab[j+noccurrences] = occurrences[j].second;
            }

            // Adjust UChar index -> UChar32 index (1-2 byte UTF16 to 1 byte UTF32-code points)
            str_cont.UChar16_to_UChar32_index(i, ans_tab,
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
