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
#include "ci_container_regex.h"
#include <deque>
#include <string>
#include <utility>
#include <vector>
using namespace std;


namespace {

bool ci__capture_names_present(const vector<string>& names)
{
    for (vector<string>::const_iterator it=names.begin();
            it != names.end(); ++it) {
        if (!it->empty())
            return true;
    }
    return false;
}


SEXP ci__capture_names_to_r(const vector<string>& names)
{
    if (!ci__capture_names_present(names))
        return R_NilValue;

    SEXP result;
    R_len_t n = static_cast<R_len_t>(names.size());
    PROTECT(result = Rf_allocVector(STRSXP, n));
    for (R_len_t i=0; i<n; ++i) {
        SET_STRING_ELT(
            result, i,
            Rf_mkCharLenCE(
                names[static_cast<size_t>(i)].empty()?"":
                    names[static_cast<size_t>(i)].data(),
                names[static_cast<size_t>(i)].size(), CE_UTF8
            )
        );
    }
    UNPROTECT(1);
    return result;
}


void ci__locate_adjust_fromto(
    deque< pair<R_len_t, R_len_t> >& occurrences,
    StriContainerUTF16& str_cont, R_len_t i, bool get_length1
)
{
    R_len_t noccurrences = static_cast<R_len_t>(occurrences.size());
    if (noccurrences <= 0)
        return;

    vector<int> starts(static_cast<size_t>(noccurrences));
    vector<int> ends(static_cast<size_t>(noccurrences));
    deque< pair<R_len_t, R_len_t> >::const_iterator iter = occurrences.begin();
    for (R_len_t j=0; iter != occurrences.end(); ++iter, ++j) {
        starts[static_cast<size_t>(j)] = iter->first;
        ends[static_cast<size_t>(j)] = iter->second;
    }

    // Adjust UChar index -> UChar32 index
    // (1-2 byte UTF16 to 1 byte UTF32-code points)
    if (i < 0) {
        STRI_ASSERT(noccurrences == str_cont.get_nrecycle());
        for (R_len_t j=0; j<noccurrences; ++j) {
            if (str_cont.isNA(j) || starts[j] == NA_INTEGER || starts[j] < 0)
                continue;
            str_cont.UChar16_to_UChar32_index(
                j, &starts[j], &ends[j], 1,
                1, // 0-based index -> 1-based
                0  // end returns position of next character after match
            );
        }
    }
    else {
        str_cont.UChar16_to_UChar32_index(
            i, starts.data(), ends.data(), noccurrences,
            1, // 0-based index -> 1-based
            0  // end returns position of next character after match
        );
    }

    deque< pair<R_len_t, R_len_t> >::iterator output_iter =
        occurrences.begin();
    for (R_len_t j=0; output_iter != occurrences.end(); ++output_iter, ++j) {
        R_len_t start = starts[static_cast<size_t>(j)];
        R_len_t end = ends[static_cast<size_t>(j)];
        if (get_length1 && start != NA_INTEGER && start >= 0)
            end -= start - 1;
        output_iter->first = start;
        output_iter->second = end;
    }
}

} // namespace


/* Converts a deque with (from,to) pairs to a 2-column R matrix
 *
 * does not set dimnames
 *
 * TODO: <refactor> use also in ci_locate_all_fixed etc.
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-20)
 */
SEXP ci__locate_get_fromto_matrix(
    deque< pair<R_len_t, R_len_t> >& occurrences,
    bool omit_no_match1,
    bool get_length1
) {
    SEXP ans;
    ci::UnwindCallbackProtector protector;
    R_len_t noccurrences = (R_len_t)occurrences.size();

    if (noccurrences <= 0) {
        return ci__matrix_NA_INTEGER(
            omit_no_match1?0:1, 2, get_length1?-1:NA_INTEGER
        );
    }

    ans = protector.protect(Rf_allocMatrix(INTSXP, noccurrences, 2));
    int* ans_tab = INTEGER(ans);
    deque< pair<R_len_t, R_len_t> >::iterator iter = occurrences.begin();
    for (R_len_t j = 0; iter != occurrences.end(); ++iter, ++j) {
        pair<R_len_t, R_len_t> match = *iter;
        ans_tab[j]              = match.first;
        ans_tab[j+noccurrences] = match.second;
    }

    return ans;
}


SEXP ci__locate_get_fromto_matrix(
    deque< pair<R_len_t, R_len_t> >& occurrences,
    StriContainerUTF16& str_cont,
    R_len_t i,
    bool omit_no_match1,
    bool get_length1
) {
    SEXP ans;
    ci::UnwindCallbackProtector protector;
    R_len_t noccurrences = (R_len_t)occurrences.size();

    if (noccurrences <= 0) {
        return ci__matrix_NA_INTEGER(
            omit_no_match1?0:1, 2, get_length1?-1:NA_INTEGER
        );
    }

    ans = protector.protect(Rf_allocMatrix(INTSXP, noccurrences, 2));
    int* ans_tab = INTEGER(ans);
    deque< pair<R_len_t, R_len_t> >::iterator iter = occurrences.begin();
    for (R_len_t j = 0; iter != occurrences.end(); ++iter, ++j) {
        pair<R_len_t, R_len_t> match = *iter;
        ans_tab[j]              = match.first;
        ans_tab[j+noccurrences] = match.second;
    }

    // Adjust UChar index -> UChar32 index
    // (1-2 byte UTF16 to 1 byte UTF32-code points)
    if (i < 0) {
        STRI_ASSERT(noccurrences == str_cont.get_nrecycle());
        for (R_len_t j=0; j<noccurrences; ++j) {
            if (str_cont.isNA(j) || ans_tab[j] == NA_INTEGER || ans_tab[j] < 0)
                continue;
            str_cont.UChar16_to_UChar32_index(
                j, &ans_tab[j], &ans_tab[j+noccurrences], 1,
                1, // 0-based index -> 1-based
                0  // end returns position of next character after match
            );
        }
    }
    else {
        str_cont.UChar16_to_UChar32_index(
            i, ans_tab, ans_tab+noccurrences, noccurrences,
            1, // 0-based index -> 1-based
            0  // end returns position of next character after match
        );
    }

    if (get_length1) {
        for (R_len_t j=0; j<noccurrences; ++j) {
            if (ans_tab[j] != NA_INTEGER && ans_tab[j] >= 0)
                ans_tab[j+noccurrences] -= ans_tab[j]-1;
        }
    }

    return ans;
}


/** Locate all occurrences of a regex pattern
 *
 * @param str character vector
 * @param pattern character vector
 * @param opts_regex list
 * @param omit_no_match single logical value
 * @param capture_groups single logical value
 * @return list of integer matrices (2 columns)
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          StriContainerUTF16+deque usage
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-19)
 *          use StriContainerRegexPattern + opts_regex
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-27)
 *    FR #117: omit_no_match arg added
 *
 * @version 1.0-2 (Marek Gagolewski, 2016-01-29)
 *    Issue #214: allow a regex pattern like `.*`  to match an empty string
 *
 * @version 1.4.7 (Marek Gagolewski, 2020-08-24)
 *    Use StriContainerRegexPattern::getRegexOptions
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-20)
 *     #25: capture_groups
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29)
 *     get_length
 */
SEXP ci_locate_all_regex(SEXP str, SEXP pattern, SEXP omit_no_match, SEXP opts_regex, SEXP capture_groups, SEXP get_length)
{
    STRI__ERROR_HANDLER_BEGIN(0)
    // Deviation from stringi: begin the C++ boundary at the copied argument-
    // preparation position so controlled scalar and option warnings stay queued.
    bool omit_no_match1 = false;
    bool capture_groups1 = false;
    bool get_length1 = false;
    charport::unwind_protect([&]() -> SEXP {
        omit_no_match1 = ci__prepare_arg_logical_1_notNA(
            omit_no_match, "omit_no_match", &STRI__DEFERRED_WARNINGS
        );
        capture_groups1 = ci__prepare_arg_logical_1_notNA(
            capture_groups, "capture_groups", &STRI__DEFERRED_WARNINGS
        );
        get_length1 = ci__prepare_arg_logical_1_notNA(
            get_length, "get_length", &STRI__DEFERRED_WARNINGS
        );
        return R_NilValue;
    });
    StriRegexMatcherOptions pattern_opts;
    charport::unwind_protect([&]() -> SEXP {
        pattern_opts = StriContainerRegexPattern::getRegexOptions(
            STRI__DEFERRED_WARNINGS, opts_regex
        );
        return R_NilValue;
    });
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
    // Deviation from stringi: queue the recycling warning until regex/Reader
    // owners and final R metadata are complete.
    if (vectorize_length > 0 &&
            (vectorize_length % str_n != 0 ||
             vectorize_length % pattern_n != 0))
        context.warn(MSG__WARN_RECYCLING_RULE);
    STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
        return Rf_allocVector(VECSXP, vectorize_length);
    }));

    {
        StriContainerUTF16 str_cont(
            context, str, vectorize_length
        );
        StriContainerRegexPattern pattern_cont(
            context, pattern, vectorize_length, pattern_opts
        );
        deque< pair<R_len_t, R_len_t> > occurrences;
        vector< deque< pair<R_len_t, R_len_t> > > capture_occurrences;

        // Deviation from stringi: one loop-level unwind bridge protects every
        // child allocation and list write while the Reader and regex owners
        // are live. Reusable match storage lives outside the callback, so an
        // R error cleans it up without one bridge per child.
        charport::unwind_protect([&]() -> SEXP {
          for (R_len_t i = pattern_cont.vectorize_init();
                  i != pattern_cont.vectorize_end();
                  i = pattern_cont.vectorize_next(i))
          {
            ci::UnwindCallbackProtector protector;
            occurrences.clear();
            capture_occurrences.clear();
            if ((pattern_cont).isNA(i) || (pattern_cont).get(i).length() <= 0) {
                if (!(pattern_cont).isNA(i))
                    context.warn(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);

                SEXP ans;
                ans = protector.protect(ci__matrix_NA_INTEGER(1, 2));
                if (capture_groups1) {
                    SEXP cgs;
                    cgs = protector.protect(Rf_allocVector(VECSXP, 0));
                    Rf_setAttrib(
                        ans,
                        Rf_ScalarString(Rf_mkChar("capture_groups")),
                        cgs
                    );
                }
                SET_VECTOR_ELT(ret, i, ans);
                continue;
            }

            UErrorCode status = U_ZERO_ERROR;
            RegexMatcher *matcher = pattern_cont.getMatcher(i); // will be deleted automatically
            R_len_t pattern_cur_groups = matcher->groupCount();
            if (capture_groups1 && pattern_cur_groups > 0)
                capture_occurrences.resize(pattern_cur_groups);

            if (!(str_cont).isNA(i)) {
                matcher->reset(str_cont.get(i));
                int found = (int)matcher->find(status);
                STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

                while (found) {
                    UErrorCode status = U_ZERO_ERROR;
                    int start = (int)matcher->start(status);
                    STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
                    int end  =  (int)matcher->end(status);
                    STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
                    occurrences.push_back(pair<R_len_t, R_len_t>(start, end));

                    if (capture_groups1) {
                        for (R_len_t j=0; j<pattern_cur_groups; ++j) {
                            start = (int)matcher->start(j+1, status);
                            STRI__CHECKICUSTATUS_THROW(status, {})
                            end  =  (int)matcher->end(j+1, status);
                            STRI__CHECKICUSTATUS_THROW(status, {})
                            if (start >= 0 && end >= 0) {  // e.g., conditional capture group
                                capture_occurrences[j].push_back(
                                    pair<R_len_t, R_len_t>(start, end)
                                );
                            }
                            else {
                                capture_occurrences[j].push_back(
                                    pair<R_len_t, R_len_t>(
                                        get_length1?-1:NA_INTEGER,
                                        get_length1?-1:NA_INTEGER
                                    )
                                );
                            }
                        }
                    }

                    found = (int)matcher->find(status);
                    STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
                };
            }

            bool string_missing = str_cont.isNA(i);
            const vector<string>* capture_names = NULL;
            if (capture_groups1) {
                capture_names = &pattern_cont.getCaptureGroupNames(i);
            }

            SEXP ans;
            if (string_missing) {
                ans = protector.protect(ci__matrix_NA_INTEGER(1, 2));
            }
            else {
                ans = protector.protect(ci__locate_get_fromto_matrix(
                    occurrences, str_cont, i,
                    omit_no_match1, get_length1
                ));
            }

            if (capture_groups1) {
                SEXP cgs;
                cgs = protector.protect(Rf_allocVector(
                    VECSXP, pattern_cur_groups
                ));
                for (R_len_t j=0; j<pattern_cur_groups; ++j) {
                    SEXP ans2;
                    if (string_missing) {
                        ans2 = protector.protect(
                            ci__matrix_NA_INTEGER(1, 2)
                        );
                    }
                    else {
                        ans2 = protector.protect(ci__locate_get_fromto_matrix(
                            capture_occurrences[j],
                            str_cont, i,
                            omit_no_match1, get_length1
                        ));
                    }
                    SET_VECTOR_ELT(cgs, j, ans2);
                    protector.unprotect(1);
                }

                SEXP names;
                names = protector.protect(ci__capture_names_to_r(
                    *capture_names
                ));
                ci__locate_set_dimnames_list(cgs, get_length1);
                if (!Rf_isNull(names))
                    Rf_setAttrib(cgs, R_NamesSymbol, names);
                Rf_setAttrib(
                    ans, Rf_ScalarString(Rf_mkChar("capture_groups")), cgs
                );
            }

            SET_VECTOR_ELT(ret, i, ans);
          }

          ci__locate_set_dimnames_list(ret, get_length1);  // all matrices get from&to colnames
          return R_NilValue;
        });
    }
    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/** Locate first/last occurrence of a regex pattern
 *
 * @param str character vector
 * @param pattern character vector
 * @param opts_regex list
 * @param first search for the first or the last occurrence?
 * @param capture_groups1 extract individual capture groups too?
 * @return list of integer matrices (2 columns)
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          Use StriContainerUTF16
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-19)
 *          Use StriContainerRegexPattern + opts_regex
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.0-2 (Marek Gagolewski, 2016-01-29)
 *    Issue #214: allow a regex pattern like `.*`  to match an empty string
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-20)
 *     #25: capture_groups
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29)
 *     get_length
 */
SEXP ci__locate_firstlast_regex(
    SEXP str, SEXP pattern, SEXP opts_regex, bool first, bool capture_groups1, bool get_length1
) {
    PROTECT(str = ci__prepare_arg_string(str, "str")); // prepare string argument
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern")); // prepare string argument
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
    // Deviation from stringi: queue the recycling warning until regex/Reader
    // owners, capture staging, and final result metadata are complete.
    if (vectorize_length > 0 &&
            (vectorize_length % str_n != 0 ||
             vectorize_length % pattern_n != 0))
        context.warn(MSG__WARN_RECYCLING_RULE);

    StriRegexMatcherOptions pattern_opts;
    charport::unwind_protect([&]() -> SEXP {
        pattern_opts = StriContainerRegexPattern::getRegexOptions(
            STRI__DEFERRED_WARNINGS, opts_regex
        );
        return R_NilValue;
    });

    STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
        return Rf_allocMatrix(INTSXP, vectorize_length, 2);
    }));
    int* ret_tab = INTEGER(ret);

    deque< deque< pair<R_len_t, R_len_t> > > cg_occurrences;
    vector<string> capture_names;
    //cg_occurrences[i] -- i-th capture group

    {
        StriContainerUTF16 str_cont(
            context, str, vectorize_length
        );
        StriContainerRegexPattern pattern_cont(
            context, pattern, vectorize_length, pattern_opts
        );

        for (R_len_t i = pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            ret_tab[i]                  = NA_INTEGER;
            ret_tab[i+vectorize_length] = NA_INTEGER;

            if ((pattern_cont).isNA(i) || (pattern_cont).get(i).length() <= 0) {
                if (!(pattern_cont).isNA(i))
                    context.warn(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
                continue;
            }

            // if str is NA, we may still be generating capture_groups
            if (!(str_cont).isNA(i) && get_length1) {
                ret_tab[i]                  = -1;
                ret_tab[i+vectorize_length] = -1;
            }

            RegexMatcher *matcher = pattern_cont.getMatcher(i); // will be deleted automatically

            R_len_t pattern_cur_groups = matcher->groupCount();
            if (capture_groups1 && pattern_cur_groups > 0) {
                while ((R_len_t)cg_occurrences.size() < pattern_cur_groups) {
                    cg_occurrences.push_back(
                        deque< pair<R_len_t, R_len_t> >(
                            vectorize_length,
                            pair<R_len_t, R_len_t>(
                                NA_INTEGER,
                                NA_INTEGER
                            )
                        )
                    );
                }
            }

            if ((str_cont).isNA(i)) {
                continue;
            }

            matcher->reset(str_cont.get(i));

            UErrorCode status = U_ZERO_ERROR;
            int m_res = (int)matcher->find(status);
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
            if (!m_res) {
                if (capture_groups1 && get_length1) {
                    for (R_len_t j=0; j<pattern_cur_groups; ++j) {
                        cg_occurrences[j][i].first  = -1;
                        cg_occurrences[j][i].second = -1;
                    }
                }
                continue;  // no match
            }

            while (1) {
                ret_tab[i] = (int)matcher->start(status);
                STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
                ret_tab[i+vectorize_length] = (int)matcher->end(status);
                STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

                if (capture_groups1) {
                    for (R_len_t j=0; j<pattern_cur_groups; ++j) {
                        int start = (int)matcher->start(j+1, status);
                        STRI__CHECKICUSTATUS_THROW(status, {})
                        int end  =  (int)matcher->end(j+1, status);
                        STRI__CHECKICUSTATUS_THROW(status, {})
                        if (start >= 0 && end >= 0) {  // e.g., conditional capture group
                            cg_occurrences[j][i].first  = start;
                            cg_occurrences[j][i].second = end;
                        }
                        else {
                            cg_occurrences[j][i].first  = get_length1?-1:NA_INTEGER;
                            cg_occurrences[j][i].second = get_length1?-1:NA_INTEGER;
                        }
                    }
                }

                if (first)
                    break;  // only first match

                m_res = (int)matcher->find(status);
                STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
                if (!m_res) break;
            }

            // Adjust UChar index -> UChar32 index (1-2 byte UTF16 to 1 byte UTF32-code points)
            str_cont.UChar16_to_UChar32_index(
                i,
                ret_tab+i, ret_tab+i+vectorize_length, 1,
                1, // 0-based index -> 1-based
                0  // end returns position of next character after match
            );

            if (get_length1 && ret_tab[i] != NA_INTEGER && ret_tab[i] >= 0)
                ret_tab[i+vectorize_length] -= ret_tab[i] - 1;

        }

        if (capture_groups1) {
            // Deviation from stringi: convert capture offsets and copy names
            // before the containers end, then allocate their R metadata later.
            R_len_t pattern_cur_groups = static_cast<R_len_t>(
                cg_occurrences.size()
            );
            for (R_len_t j=0; j<pattern_cur_groups; ++j) {
                ci__locate_adjust_fromto(
                    cg_occurrences[j], str_cont, -1, get_length1
                );
            }
            if (pattern_cont.get_n() == 1 &&
                    !pattern_cont.isNA(0) &&
                    pattern_cont.get(0).length() > 0) {
                const vector<string>& current_names =
                    pattern_cont.getCaptureGroupNames(0);
                if (ci__capture_names_present(current_names))
                    capture_names = current_names;
            }
        }
    }

    if (capture_groups1) {
        SEXP cgs;
        R_len_t pattern_cur_groups = static_cast<R_len_t>(
            cg_occurrences.size()
        );
        STRI__PROTECT(cgs = charport::unwind_protect([&]() -> SEXP {
            return Rf_allocVector(VECSXP, pattern_cur_groups);
        }));
        for (R_len_t j=0; j<pattern_cur_groups; ++j) {
            SEXP ans2;
            STRI__PROTECT(ans2 = charport::unwind_protect([&]() -> SEXP {
                return ci__locate_get_fromto_matrix(
                    cg_occurrences[j], false, get_length1
                );
            }));
            SET_VECTOR_ELT(cgs, j, ans2);
            STRI__UNPROTECT(1);
        }

        SEXP names;
        STRI__PROTECT(names = charport::unwind_protect([&]() -> SEXP {
            return ci__capture_names_to_r(capture_names);
        }));
        charport::unwind_protect([&]() -> SEXP {
            ci__locate_set_dimnames_list(cgs, get_length1);  // all matrices get from&to colnames
            if (!Rf_isNull(names))
                Rf_setAttrib(cgs, R_NamesSymbol, names);
            return Rf_setAttrib(
                ret, Rf_ScalarString(Rf_mkChar("capture_groups")), cgs
            );
        });
        STRI__UNPROTECT(2);
    }
    charport::unwind_protect([&]() -> SEXP {
        ci__locate_set_dimnames_matrix(ret, get_length1);
        return R_NilValue;
    });
    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/** Locate first or last occurrence of a regex pattern
 *
 * @param str character vector
 * @param pattern character vector
 * @param opts_regex list
 * @param capture_groups single logical value
 * @return list of integer matrices (2 columns)
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          Use StriContainerUTF16
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-19)
 *          Use StriContainerRegexPattern + opts_regex
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-20)
 *     #25: capture_groups
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29)
 *     get_length
 */
SEXP ci_locate_first_regex(SEXP str, SEXP pattern, SEXP opts_regex, SEXP capture_groups, SEXP get_length)
{
    bool capture_groups1 = ci__prepare_arg_logical_1_notNA(capture_groups, "capture_groups");
    bool get_length1 = ci__prepare_arg_logical_1_notNA(get_length, "get_length");
    return ci__locate_firstlast_regex(str, pattern, opts_regex, true, capture_groups1, get_length1);
}


/** Locate first occurrence of a regex pattern
 * @param str character vector
 * @param pattern character vector
 * @param opts_regex list
 * @param capture_groups single logical value
 * @return list of integer matrices (2 columns)
 *
 * @version 0.1-?? (Bartlomiej Tartanus, 2013-06-10)
 *          Use StriContainerUTF16
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-19)
 *          Use StriContainerRegexPattern + opts_regex
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-20)
 *     #25: capture_groups
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29)
 *     get_length
 */
SEXP ci_locate_last_regex(SEXP str, SEXP pattern, SEXP opts_regex, SEXP capture_groups, SEXP get_length)
{
    bool capture_groups1 = ci__prepare_arg_logical_1_notNA(capture_groups, "capture_groups");
    bool get_length1 = ci__prepare_arg_logical_1_notNA(get_length, "get_length");
    return ci__locate_firstlast_regex(str, pattern, opts_regex, false, capture_groups1, get_length1);
}
