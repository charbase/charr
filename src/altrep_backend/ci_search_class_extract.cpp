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
#include "ci_container_charclass.h"
#include "ci_container_logical.h"
#include <deque>
#include <stdexcept>
#include <utility>
#include <vector>
using namespace std;


/**
 * Extract first or last occurrences of a character class in each string
 *
 * @param str character vector
 * @param pattern character vector
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-08)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-15)
 *          Use StrContainerCharClass
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException-friendly
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-03)
 *          detects invalid UTF-8 byte stream
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-05)
 *          StriContainerCharClass now relies on UnicodeSet
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci__extract_firstlast_charclass(SEXP str, SEXP pattern, bool first)
{
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
    charport::unwind_protect([&]() -> SEXP {
        vectorize_length = ci__recycling_rule(
            STRI__DEFERRED_WARNINGS, 2, str_n, pattern_n
        );
        return R_NilValue;
    });

    charport::charvec::Builder builder(vectorize_length);
    {
        StriContainerUTF8 str_cont(context, str, vectorize_length);
        StriContainerCharClass pattern_cont(
            context, pattern, vectorize_length
        );

        for (R_len_t i = pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            builder.set_na(i);

            if (str_cont.isNA(i) || pattern_cont.isNA(i))
                continue;

            const UnicodeSet* pattern_cur = &pattern_cont.get(i);
            R_len_t     str_cur_n = str_cont.get(i).length();
            const char* str_cur_s = str_cont.get(i).data();
            R_len_t j, jlast;
            UChar32 chr;

            if (first) {
                for (jlast=j=0; j<str_cur_n; ) {
                    U8_NEXT(str_cur_s, j, str_cur_n, chr);
                    if (chr < 0) // invalid utf-8 sequence
                        throw StriException(MSG__INVALID_UTF8);
                    if (pattern_cur->contains(chr)) {
                        ci::builder_set(
                            builder, i, str_cur_s+jlast, j-jlast,
                            cetype_ext_t::CE_ASCII_OR_UTF8
                        );
                        break; // that's enough for first
                    }
                    jlast = j;
                }
            }
            else {
                for (jlast=j=str_cur_n; j>0; ) {
                    U8_PREV(str_cur_s, 0, j, chr); // go backwards
                    if (chr < 0) // invalid utf-8 sequence
                        throw StriException(MSG__INVALID_UTF8);
                    if (pattern_cur->contains(chr)) {
                        ci::builder_set(
                            builder, i, str_cur_s+j, jlast-j,
                            cetype_ext_t::CE_ASCII_OR_UTF8
                        );
                        break; // that's enough for last
                    }
                    jlast = j;
                }
            }
        }
    }

    STRI__PROTECT(ret = builder.to_sexp());
    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/**
 * Extract first occurrence of a character class in each string
 *
 * @param str character vector
 * @param pattern character vector
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-08)
 */
SEXP ci_extract_first_charclass(SEXP str, SEXP pattern)
{
    return ci__extract_firstlast_charclass(str, pattern, true);
}


/**
 * Extract last occurrence of a character class in each string
 *
 * @param str character vector
 * @param pattern character vector
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-08)
 */
SEXP ci_extract_last_charclass(SEXP str, SEXP pattern)
{
    return ci__extract_firstlast_charclass(str, pattern, false);
}


/**
 * Extract all occurrences of a character class in each string
 *
 * @param str character vector
 * @param pattern character vector
 * @param simplify single logical value
 *
 * @return list of character vectors  or character matrix
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-08)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-15)
 *          Use StrContainerCharClass
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException-friendly
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-03)
 *          detects invalid UTF-8 byte stream
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-05)
 *          StriContainerCharClass now relies on UnicodeSet
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-10-24)
 *          added simplify param
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-02)
 *          using StriContainerCharClass::locateAll;
 *          no longer vectorized over merge
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-27)
 *    FR #117: omit_no_match arg added
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-04)
 *    allow `simplify=NA`
 */
SEXP ci_extract_all_charclass(SEXP str, SEXP pattern, SEXP merge, SEXP simplify, SEXP omit_no_match)
{
    bool merge_cur = ci__prepare_arg_logical_1_notNA(merge, "merge");
    bool omit_no_match1 = ci__prepare_arg_logical_1_notNA(omit_no_match, "omit_no_match");
    PROTECT(simplify = ci__prepare_arg_logical_1(simplify, "simplify"));
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));
    const int simplify1 = LOGICAL_RO(simplify)[0];

    STRI__ERROR_HANDLER_BEGIN(3)
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
            STRI__DEFERRED_WARNINGS, 2, str_n, pattern_n
        );
        return R_NilValue;
    });

    vector<charport::charvec::Store> stores;
    stores.reserve(static_cast<size_t>(vectorize_length));
    for (R_len_t i=0; i<vectorize_length; ++i)
        stores.push_back(charport::charvec::Store(0, 0));

    {
        StriContainerUTF8 str_cont(context, str, vectorize_length);
        StriContainerCharClass pattern_cont(
            context, pattern, vectorize_length
        );
        charport::charvec::Builder builder(0);

        for (R_len_t i = pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            charport::charvec::Store& current = stores[
                static_cast<size_t>(i)
            ];
            if (pattern_cont.isNA(i) || str_cont.isNA(i)) {
                current = charport::charvec::Store::scalar(
                    nullptr, 0, cetype_ext_t::CE_NA
                );
                continue;
            }

            R_len_t str_cur_n     = str_cont.get(i).length();
            const char* str_cur_s = str_cont.get(i).data();
            deque< pair<R_len_t, R_len_t> > occurrences;
            StriContainerCharClass::locateAll(
                occurrences, &pattern_cont.get(i),
                str_cur_s, str_cur_n, merge_cur,
                false /* byte-based indexes */
            );

            R_len_t noccurrences = (R_len_t)occurrences.size();
            if (noccurrences == 0) {
                if (!omit_no_match1)
                    current = charport::charvec::Store::scalar(
                        nullptr, 0, cetype_ext_t::CE_NA
                    );
                continue;
            }

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
    }

    size_t max_columns = 0;
    for (R_len_t i=0; i<vectorize_length; ++i) {
        if (stores[i].size() > max_columns)
            max_columns = stores[i].size();
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
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}
