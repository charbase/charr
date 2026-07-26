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
#include "ci_utf8.h"
#include "ci_container_bytesearch.h"
#include "ci_container_integer.h"
#include "ci_container_logical.h"
#include <cstring>
#include <utility>
#include <vector>
namespace charr { namespace base {

using namespace std;


namespace {

typedef pair<R_len_t, R_len_t> CiSplitField;


template <typename FindNext>
void ci__collect_split_fields(
    R_len_t string_length,
    int n_cur,
    bool omit_empty,
    bool tokens_only,
    FindNext find_next,
    vector<CiSplitField>& fields
)
{
    fields.clear();
    fields.emplace_back(0, 0);

    R_len_t start = 0;
    R_len_t end = 0;
    int k = 1;
    while (k < n_cur && find_next(start, end)) {
        if (omit_empty && fields.back().first == start) {
            fields.back().first = end;
        }
        else {
            fields.back().second = start;
            fields.emplace_back(end, end);
            ++k;
        }
    }

    fields.back().second = string_length;
    if (omit_empty && fields.back().first == fields.back().second)
        fields.pop_back();

    if (tokens_only && n_cur < INT_MAX) {
        --n_cur;
        if (fields.size() > static_cast<size_t>(n_cur))
            fields.resize(static_cast<size_t>(n_cur));
    }
}


bool ci__split_ascii_scalar_direct(
    SEXP str, SEXP pattern, uint32_t pattern_flags,
    R_len_t vectorize_length, const StriContainerInteger& n_cont,
    const StriContainerLogical& omit_empty_cont, bool tokens_only,
    SEXP output
)
{
    if (pattern_flags != 0 || LENGTH(pattern) != 1)
        return false;

    const SEXP pattern_value = STRING_ELT(pattern, 0);
    if (pattern_value == NA_STRING || !IS_ASCII(pattern_value) ||
            LENGTH(pattern_value) != 1) {
        return false;
    }

    const R_len_t source_length = LENGTH(str);
    const SEXP* values = STRING_PTR_RO(str);
    // Plain CHARSXP payloads remain stable while `str` is protected. When
    // every record is already ASCII or UTF-8, splitting can borrow them
    // directly instead of constructing a parallel normalized record table.
    for (R_len_t i=0; i<source_length; ++i) {
        const SEXP value = values[i];
        if (value != NA_STRING && !IS_ASCII(value) && !IS_UTF8(value))
            return false;
    }

    const unsigned char needle = static_cast<unsigned char>(
        CHAR(pattern_value)[0]
    );
    vector<CiSplitField> fields;
    fields.reserve(16);

    for (R_len_t i=0; i<vectorize_length; ++i) {
        const SEXP value = values[i % source_length];
        if (n_cont.isNA(i) || value == NA_STRING) {
            SET_VECTOR_ELT(output, i, ci__vector_NA_strings(1));
            continue;
        }

        int n_cur = n_cont.get(i);
        const bool omit_empty =
            !omit_empty_cont.isNA(i) && omit_empty_cont.get(i);
        const char* data = CHAR(value);
        R_len_t length = LENGTH(value);
        if (!IS_ASCII(value) && STRI__ENC_HAS_BOM_UTF8(data, length)) {
            data += 3;
            length -= 3;
        }
        if (length <= 0) {
            SET_VECTOR_ELT(
                output, i,
                omit_empty_cont.isNA(i) ? ci__vector_NA_strings(1) :
                ci__vector_empty_strings((omit_empty || n_cur == 0) ? 0 : 1)
            );
            continue;
        }

        if (n_cur >= INT_MAX-1)
            throw StriException(
                MSG__INCORRECT_NAMED_ARG "; " MSG__EXPECTED_SMALLER, "n"
            );
        if (n_cur < 0)
            n_cur = INT_MAX;
        else if (n_cur == 0) {
            SET_VECTOR_ELT(output, i, Rf_allocVector(STRSXP, 0));
            continue;
        }
        else if (tokens_only)
            ++n_cur;

        R_len_t search_from = 0;
        ci__collect_split_fields(
            length, n_cur, omit_empty, tokens_only,
            [&](R_len_t& start, R_len_t& end) {
                const void* match = std::memchr(
                    data+search_from, needle,
                    static_cast<size_t>(length-search_from)
                );
                if (match == NULL)
                    return false;
                start = static_cast<R_len_t>(
                    static_cast<const char*>(match)-data
                );
                end = start+1;
                search_from = end;
                return true;
            },
            fields
        );

        SEXP answer;
        PROTECT(answer = Rf_allocVector(STRSXP, fields.size()));
        for (R_len_t k=0; k<static_cast<R_len_t>(fields.size()); ++k) {
            const CiSplitField& field = fields[static_cast<size_t>(k)];
            SET_STRING_ELT(
                answer, k,
                field.second == field.first && omit_empty_cont.isNA(i) ?
                NA_STRING : Rf_mkCharLenCE(
                    data+field.first, field.second-field.first, CE_UTF8
                )
            );
        }
        SET_VECTOR_ELT(output, i, answer);
        UNPROTECT(1);
    }

    return true;
}

} // namespace


/**
 * Split a string into parts [byte compare]
 *
 * The pattern matches identify delimiters that separate the input into fields.
 * The input data between the matches becomes the fields themselves.
 *
 * @param str character vector
 * @param pattern character vector
 * @param n integer vector
 * @param omit_empty logical vector
 * @param tokens_only single logical value
 * @param simplify single logical value
 *
 * @return list of character vectors  or character matrix
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-25)
 *          StriException friendly, use a UTF-8 input adapter
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-07-10)
 *          BUGFIX: wrong behavior on empty str
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          ci_split_fixed now uses byte search only
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-10-19)
 *          added tokens_only param
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-10-23)
 *          added split param
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-10-24)
 *          allow omit_empty=NA
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-04)
 *    allow `simplify=NA`; FR #126: pass n to ci_list2matrix
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-07)
 *    FR #110, #23: opts_fixed arg added
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-02-14)
 *    use StriByteSearchMatcher
 */
SEXP ci_split_fixed(SEXP str, SEXP pattern, SEXP n,
                      SEXP omit_empty, SEXP tokens_only, SEXP simplify, SEXP opts_fixed)
{
    uint32_t pattern_flags = StriContainerByteSearch::getByteSearchFlags(opts_fixed);
    bool tokens_only1 = ci__prepare_arg_logical_1_notNA(tokens_only, "tokens_only");
    PROTECT(simplify = ci__prepare_arg_logical_1(simplify, "simplify"));
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));
    PROTECT(n = ci__prepare_arg_integer(n, "n"));
    PROTECT(omit_empty = ci__prepare_arg_logical(omit_empty, "omit_empty"));

    STRI__ERROR_HANDLER_BEGIN(5)
    R_len_t vectorize_length = ci__recycling_rule(true, 4,
                               LENGTH(str), LENGTH(pattern), LENGTH(n), LENGTH(omit_empty));
    StriContainerInteger n_cont(n, vectorize_length);
    StriContainerLogical omit_empty_cont(omit_empty, vectorize_length);

    SEXP ret;
    STRI__PROTECT(ret = Rf_allocVector(VECSXP, vectorize_length));
    if (!ci__split_ascii_scalar_direct(
            str, pattern, pattern_flags, vectorize_length,
            n_cont, omit_empty_cont, tokens_only1, ret
    )) {
        Utf8Input str_cont(str, vectorize_length);
        StriContainerByteSearch pattern_cont(
            pattern, vectorize_length, pattern_flags
        );
        vector<CiSplitField> fields;
        fields.reserve(16);

        for (R_len_t i = pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            if (n_cont.isNA(i)) {
                SET_VECTOR_ELT(ret, i, ci__vector_NA_strings(1));
                continue;
            }
            int  n_cur        = n_cont.get(i);
            int  omit_empty_cur   = !omit_empty_cont.isNA(i) && omit_empty_cont.get(i);

            STRI__CONTINUE_ON_EMPTY_OR_NA_STR_PATTERN(str_cont, pattern_cont,
                    SET_VECTOR_ELT(ret, i, ci__vector_NA_strings(1));,
                    SET_VECTOR_ELT(ret, i,
                                   (omit_empty_cont.isNA(i))?ci__vector_NA_strings(1):
                                   ci__vector_empty_strings((omit_empty_cur || n_cur == 0)?0:1));)

            const Utf8Record& str_cur = str_cont.get(i);
            R_len_t     str_cur_n = str_cur.length();
            const char* str_cur_s = str_cur.data();

            if (n_cur >= INT_MAX-1)
                throw StriException(MSG__INCORRECT_NAMED_ARG "; " MSG__EXPECTED_SMALLER, "n");
            else if (n_cur < 0)
                n_cur = INT_MAX;
            else if (n_cur == 0) {
                SET_VECTOR_ELT(ret, i, Rf_allocVector(STRSXP, 0));
                continue;
            }
            else if (tokens_only1)
                n_cur++; // we need to do one split ahead here

            const Utf8Record& pattern_cur = pattern_cont.get(i);
            if (!pattern_cont.isCaseInsensitive() && pattern_cur.length() == 1) {
                R_len_t search_from = 0;
                const unsigned char needle =
                    static_cast<unsigned char>(pattern_cur.data()[0]);
                ci__collect_split_fields(
                    str_cur_n, n_cur, omit_empty_cur, tokens_only1,
                    [&](R_len_t& start, R_len_t& end) {
                        const size_t remaining = static_cast<size_t>(
                            str_cur_n-search_from
                        );
                        const void* match = std::memchr(
                            str_cur_s+search_from, needle, remaining
                        );
                        if (!match)
                            return false;
                        start = static_cast<R_len_t>(
                            static_cast<const char*>(match)-str_cur_s
                        );
                        end = start+1;
                        search_from = end;
                        return true;
                    },
                    fields
                );
            }
            else {
                StriByteSearchMatcher* matcher = pattern_cont.getMatcher(i);
                matcher->reset(str_cur_s, str_cur_n);
                ci__collect_split_fields(
                    str_cur_n, n_cur, omit_empty_cur, tokens_only1,
                    [&](R_len_t& start, R_len_t& end) {
                        if (USEARCH_DONE == matcher->findNext())
                            return false;
                        start = matcher->getMatchedStart();
                        end = start+matcher->getMatchedLength();
                        return true;
                    },
                    fields
                );
            }

            SEXP ans;
            STRI__PROTECT(ans = Rf_allocVector(STRSXP, fields.size()));

            R_len_t k = 0;
            vector<CiSplitField>::const_iterator iter = fields.begin();
            for (; iter != fields.end(); ++iter, ++k) {
                const CiSplitField& curoccur = *iter;
                if (curoccur.second == curoccur.first && omit_empty_cont.isNA(i))
                    SET_STRING_ELT(ans, k, NA_STRING);
                else
                    SET_STRING_ELT(ans, k,
                                   Rf_mkCharLenCE(str_cur_s+curoccur.first,
                                                  curoccur.second-curoccur.first, CE_UTF8));
            }

            SET_VECTOR_ELT(ret, i, ans);
            STRI__UNPROTECT(1);
        }
    }

    if (LOGICAL(simplify)[0] == NA_LOGICAL || LOGICAL(simplify)[0]) {
        R_len_t n_min = 0;
        R_len_t n_length = LENGTH(n);
        int* n_tab = INTEGER(n);
        for (R_len_t i=0; i<n_length; ++i) {
            if (n_tab[i] != NA_INTEGER && n_min < n_tab[i])
                n_min = n_tab[i];
        }
        SEXP robj_TRUE, robj_n_min, robj_na_strings, robj_empty_strings;
        STRI__PROTECT(robj_TRUE = Rf_ScalarLogical(TRUE));
        STRI__PROTECT(robj_n_min = Rf_ScalarInteger(n_min));
        STRI__PROTECT(robj_na_strings = ci__vector_NA_strings(1));
        STRI__PROTECT(robj_empty_strings = ci__vector_empty_strings(1));
        STRI__PROTECT(ret = ci_list2matrix(ret, robj_TRUE,
                                             (LOGICAL(simplify)[0] == NA_LOGICAL)?robj_na_strings
                                             :robj_empty_strings,
                                             robj_n_min))
    }

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(; /* nothing interesting on error */)
}

} } // namespace charr::base
