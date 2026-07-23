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
#include "ci_container_utf8_indexable.h"
#include "ci_container_integer.h"
#include "ci_brkiter.h"
#include <stdexcept>
#include <utility>
#include <vector>


/**
 * Extract first or last text between boundaries
 *
 * @param str character vector
 * @param opts_brkiter list
 * @param first looking for first or last match?
 * @return character vector
 *
 * @version 0.5-1 (Marek Gagolewski, 2014-12-19)
 */
SEXP ci__extract_firstlast_boundaries(SEXP str, SEXP opts_brkiter, bool first)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));

    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP ret;
    {
    // Deviation from stringi: keep option ICU storage and the lazy Builder
    // inside the unwind-safe scope so both die before warning replay.
    StriBrkIterOptions opts_brkiter2(
        opts_brkiter, "line_break", STRI__DEFERRED_WARNINGS
    );
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    R_len_t str_length = ci::checked_r_len(
        context.size(str), "character vectors"
    );
    charport::charvec::Builder builder(str_length);
    {
        StriContainerUTF8_indexable str_cont(
            context, str, str_length
        );
        StriRuleBasedBreakIterator brkiter(opts_brkiter2);

        for (R_len_t i = 0; i < str_length; ++i)
        {
            builder.set_na(i);

            if (str_cont.isNA(i) || str_cont.get(i).length() == 0) continue;

            brkiter.setupMatcher(
                str_cont.get(i).data(), str_cont.get(i).length(),
                STRI__DEFERRED_WARNINGS
            );
            pair<R_len_t,R_len_t> curpair;

            if (first) {
                brkiter.first();
                if (!brkiter.next(curpair)) continue;
            }
            else {
                brkiter.last();
                if (!brkiter.previous(curpair)) continue;
            }

            ci::builder_set(
                builder, i, str_cont.get(i).data()+curpair.first,
                curpair.second-curpair.first,
                cetype_ext_t::CE_ASCII_OR_UTF8
            );
        }
    }

    STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
        return builder.to_sexp();
    }));
    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END( ;/* do nothing special on error */ )
}


/**
 * Extract first  text between boundaries
 *
 * @param str character vector
 * @param opts_brkiter list
 * @return character vector
 *
 * @version 0.5-1 (Marek Gagolewski, 2014-12-19)
 */
SEXP ci_extract_first_boundaries(SEXP str, SEXP opts_brkiter)
{
    return ci__extract_firstlast_boundaries(str, opts_brkiter, true);
}


/**
 * Extract last  text between boundaries
 *
 * @param str character vector
 * @param opts_brkiter list
 * @return character vector
 *
 * @version 0.5-1 (Marek Gagolewski, 2014-12-19)
 */
SEXP ci_extract_last_boundaries(SEXP str, SEXP opts_brkiter)
{
    return ci__extract_firstlast_boundaries(str, opts_brkiter, false);
}


/** Extract all  text between boundaries
 *
 * @param str character vector
 * @param simplify logical
 * @param omit_no_match logical
 * @param opts_brkiter named list
 * @return list or matrix
 *
 * @version 0.5-1 (Marek Gagolewski, 2014-12-19)
 */
SEXP ci_extract_all_boundaries(SEXP str, SEXP simplify, SEXP omit_no_match, SEXP opts_brkiter)
{
    bool omit_no_match1 = ci__prepare_arg_logical_1_notNA(omit_no_match, "omit_no_match");
    PROTECT(simplify = ci__prepare_arg_logical_1(simplify, "simplify"));
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    int simplify1 = NA_LOGICAL;

    STRI__ERROR_HANDLER_BEGIN(2)
    SEXP ret;
    {
    // Deviation from stringi: keep the option's ICU storage inside the
    // unwind-safe staging scope so it is released before warning replay.
    StriBrkIterOptions opts_brkiter2(
        opts_brkiter, "line_break", STRI__DEFERRED_WARNINGS
    );
    charport::unwind_protect([&]() -> SEXP {
        simplify1 = LOGICAL_RO(simplify)[0];
        return R_NilValue;
    });
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    R_len_t str_length = ci::checked_r_len(
        context.size(str), "character vectors"
    );
    vector<charport::charvec::Store> stores;
    stores.reserve(static_cast<size_t>(str_length));
    for (R_len_t i=0; i<str_length; ++i)
        stores.push_back(charport::charvec::Store(0, 0));
    {
        StriContainerUTF8_indexable str_cont(
            context, str, str_length
        );
        StriRuleBasedBreakIterator brkiter(opts_brkiter2);
        charport::charvec::Builder builder(0);

        for (R_len_t i = 0; i < str_length; ++i)
        {
            charport::charvec::Store& output = stores[
                static_cast<size_t>(i)
            ];
            if (str_cont.isNA(i)) {
                output = charport::charvec::Store::scalar(
                    nullptr, 0, cetype_ext_t::CE_NA
                );
                continue;
            }

            brkiter.setupMatcher(
                str_cont.get(i).data(), str_cont.get(i).length(),
                STRI__DEFERRED_WARNINGS
            );
            brkiter.first();

            deque< pair<R_len_t,R_len_t> > occurrences;
            pair<R_len_t,R_len_t> curpair;
            while (brkiter.next(curpair))
                occurrences.push_back(curpair);

            R_len_t noccurrences = (R_len_t)occurrences.size();
            if (noccurrences <= 0) {
                if (!omit_no_match1)
                    output = charport::charvec::Store::scalar(
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
                output = ci::scalar_store(
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
            output = builder.release_store();
        }
    }

    size_t max_columns = 0;
    for (R_len_t i=0; i<str_length; ++i) {
        if (stores[i].size() > max_columns)
            max_columns = stores[i].size();
    }

    if (simplify1 != NA_LOGICAL && !simplify1) {
        STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
            return Rf_allocVector(VECSXP, str_length);
        }));
        for (R_len_t i=0; i<str_length; ++i) {
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
        const R_xlen_t rows = str_length;
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

        STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
            return matrix_builder.to_sexp();
        }));
        ret = charport::unwind_protect([&]() -> SEXP {
            SEXP dim;
            PROTECT(dim = Rf_allocVector(INTSXP, 2));
            INTEGER(dim)[0] = str_length;
            INTEGER(dim)[1] = static_cast<R_len_t>(max_columns);
            SEXP result = Rf_setAttrib(ret, R_DimSymbol, dim);
            UNPROTECT(1);
            return result;
        });
    }
    }
    STRI__DEFERRED_WARNINGS.emit();

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END({/* no-op */})
}
