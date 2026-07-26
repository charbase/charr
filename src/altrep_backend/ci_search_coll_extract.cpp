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
#include "ci_container_usearch.h"
#include "ci_utf16_cursor.h"
#include <deque>
#include <stdexcept>
#include <utility>
#include <vector>
using namespace std;


/**
 * Extract first occurrence of a fixed pattern in each string [with collation]
 *
 * @param str character vector
 * @param pattern character vector
 * @param opts_collator list
 * @param firs logical - search for the first or the last occurrence?
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-24)
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          new fun: ci__extract_firstlast_coll (opts_collator == NA not allowed)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci__extract_firstlast_coll(SEXP str, SEXP pattern, SEXP opts_collator, bool first)
{
    UCollator* collator = NULL;

    STRI__ERROR_HANDLER_BEGIN(0)
    // Deviation from stringi: keep the copied order, which opens the collator
    // before preparing arguments, but close it before R resumes after an error.
    charport::unwind_protect([&]() -> SEXP {
        collator = ci__ucol_open(
            STRI__DEFERRED_WARNINGS, opts_collator
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

    charport::charvec::Builder builder(vectorize_length);
    {
        ci::Utf16Cursor str_cont(context, str, vectorize_length);
        StriContainerUStringSearch pattern_cont(
            context, pattern, vectorize_length, collator
        );  // collator is not owned by pattern_cont
        vector<char> utf8_buffer;

        for (R_len_t i = pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            STRI__CONTINUE_ON_EMPTY_OR_NA_STR_PATTERN(
                str_cont, pattern_cont,
                builder.set_na(i);, builder.set_na(i);
            )

            UStringSearch *matcher = pattern_cont.getMatcher(
                i, str_cont.get(i)
            );
            usearch_reset(matcher);

            int start;
            if (first) {
                UErrorCode status = U_ZERO_ERROR;
                start = (int)usearch_first(matcher, &status);
                STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
            } else {
                UErrorCode status = U_ZERO_ERROR;
                start = (int)usearch_last(matcher, &status);
                STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
            }

            if (start == USEARCH_DONE) {
                builder.set_na(i);
                continue;
            }

            ci::builder_set(
                builder, i,
                str_cont.substring(
                    i, static_cast<int32_t>(start),
                    static_cast<int32_t>(usearch_getMatchedLength(matcher))
                ),
                utf8_buffer
            );
        }
    }

    SEXP ret;
    STRI__PROTECT(ret = builder.to_sexp());
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
 * Extract first occurrence of a fixed pattern in each string [with collation]
 *
 * @param str character vector
 * @param pattern character vector
 * @param opts_collator list
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-24)
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          new fun: ci_extract_first_coll (opts_collator == NA not allowed)
 */
SEXP ci_extract_first_coll(SEXP str, SEXP pattern, SEXP opts_collator)
{
    return ci__extract_firstlast_coll(str, pattern, opts_collator, true);
}


/**
 * Extract last occurrence of a fixed pattern in each string [with collation]
 *
 * @param str character vector
 * @param pattern character vector
 * @param opts_collator list
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-24)
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          new fun: ci_extract_last_coll (opts_collator == NA not allowed)
 */
SEXP ci_extract_last_coll(SEXP str, SEXP pattern, SEXP opts_collator)
{
    return ci__extract_firstlast_coll(str, pattern, opts_collator, false);
}


/**
 * Extract all occurrences of a fixed pattern in each string [with collation]
 *
 * @param str character vector
 * @param pattern character vector
 * @param opts_collator list
 * @param simplify single logical value
 *
 * @return list of character vectors  or character matrix
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-24)
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          new fun: ci_extract_all_coll (opts_collator == NA not allowed)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-10-24)
 *          added simplify param
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
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
SEXP ci_extract_all_coll(SEXP str, SEXP pattern, SEXP simplify, SEXP omit_no_match, SEXP opts_collator)
{
    bool omit_no_match1 = ci__prepare_arg_logical_1_notNA(omit_no_match, "omit_no_match");
    PROTECT(simplify = ci__prepare_arg_logical_1(simplify, "simplify"));
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));
    const int simplify1 = LOGICAL_RO(simplify)[0];

    UCollator* collator = NULL;

    STRI__ERROR_HANDLER_BEGIN(3)
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

    vector<charport::charvec::Store> stores;
    stores.reserve(static_cast<size_t>(vectorize_length));
    for (R_len_t i=0; i<vectorize_length; ++i)
        stores.push_back(charport::charvec::Store(0, 0));
    {
        StriContainerUTF16 str_cont(
            context, str, vectorize_length
        );
        StriContainerUStringSearch pattern_cont(
            context, pattern, vectorize_length, collator
        );  // collator is not owned by pattern_cont
        vector<char> utf8_buffer;
        charport::charvec::Builder builder(0);

        for (R_len_t i = pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            charport::charvec::Store& current = stores[
                static_cast<size_t>(i)
            ];
            STRI__CONTINUE_ON_EMPTY_OR_NA_STR_PATTERN(
                str_cont, pattern_cont,
                current = charport::charvec::Store::scalar(
                    nullptr, 0, cetype_ext_t::CE_NA
                );,
                if (!omit_no_match1)
                    current = charport::charvec::Store::scalar(
                        nullptr, 0, cetype_ext_t::CE_NA
                    );
            )

            UStringSearch *matcher = pattern_cont.getMatcher(
                i, str_cont.get(i)
            );
            usearch_reset(matcher);

            UErrorCode status = U_ZERO_ERROR;
            int start = (int)usearch_first(matcher, &status);
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

            if (start == USEARCH_DONE) {
                if (!omit_no_match1)
                    current = charport::charvec::Store::scalar(
                        nullptr, 0, cetype_ext_t::CE_NA
                    );
                continue;
            }

            deque< pair<R_len_t, R_len_t> > occurrences;
            while (start != USEARCH_DONE) {
                occurrences.push_back(pair<R_len_t, R_len_t>(
                    start, usearch_getMatchedLength(matcher)
                ));
                start = usearch_next(matcher, &status);
                STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
            }

            const R_len_t noccurrences = static_cast<R_len_t>(
                occurrences.size()
            );
            UnicodeString match_text;
            if (noccurrences == 1) {
                const pair<R_len_t, R_len_t>& match = occurrences.front();
                match_text.setTo(
                    str_cont.get(i), match.first, match.second
                );
                current = ci::scalar_store(match_text, utf8_buffer);
                continue;
            }

            builder.reset(noccurrences);
            R_xlen_t output_i = 0;
            deque< pair<R_len_t, R_len_t> >::iterator iter =
                occurrences.begin();
            for (; iter != occurrences.end(); ++iter) {
                pair<R_len_t, R_len_t> match = *iter;
                match_text.setTo(
                    str_cont.get(i), match.first, match.second
                );
                ci::builder_set(
                    builder, output_i++, match_text, utf8_buffer
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

    SEXP ret;
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

        charport::charvec::Builder matrix(rows*columns);
        for (R_xlen_t i=0; i<rows; ++i) {
            const charport::charvec::Store& current = stores[i];
            const R_xlen_t current_size = static_cast<R_xlen_t>(
                current.size()
            );
            R_xlen_t j = 0;
            for (; j<current_size; ++j)
                matrix.set(i+j*rows, current.view(j));
            for (; j<columns; ++j) {
                if (simplify1 == NA_LOGICAL)
                    matrix.set_na(i+j*rows);
                else
                    ci::builder_set(
                        matrix, i+j*rows, "", 0,
                        cetype_ext_t::CE_ASCII
                    );
            }
        }

        STRI__PROTECT(ret = matrix.to_sexp());
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

    // Deviation from stringi: finish R assembly, then release all staging
    // storage and the collator before deferred warnings enter R.
    vector<charport::charvec::Store>().swap(stores);
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
