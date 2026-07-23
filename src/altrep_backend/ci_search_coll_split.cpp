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
#include "ci_container_integer.h"
#include "ci_container_logical.h"
#include <deque>
#include <stdexcept>
#include <utility>
#include <vector>
using namespace std;


/**
 * Split a string into parts [with collation]
 *
 * The pattern matches identify delimiters that separate the input into fields.
 * The input data between the matches becomes the fields themselves.
 *
 * @param str character vector
 * @param pattern character vector
 * @param n integer vector
 * @param omit_empty logical vector
 * @param opts_collator passed to ci__ucol_open(),
 * if \code{NA}, then \code{ci_detect_fixed_byte} is called
 * @param tokens_only single logical value
 * @param simplify single logical value
 *
 * @return list of character vectors or character matrix
 *
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-25)
 *          StriException friendly, use StriContainerUTF16
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-07-10)
 *          BUGFIX: wrong behavior on empty str
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          new fun: ci_split_coll (opts_collator == NA not allowed)
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
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-04)
 *    allow `simplify=NA`; FR #126: pass n to ci_list2matrix
 */
SEXP ci_split_coll(SEXP str, SEXP pattern, SEXP n, SEXP omit_empty,
                     SEXP tokens_only, SEXP simplify, SEXP opts_collator)
{
    bool tokens_only1 = ci__prepare_arg_logical_1_notNA(tokens_only, "tokens_only");
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));
    PROTECT(n = ci__prepare_arg_integer(n, "n"));
    PROTECT(omit_empty = ci__prepare_arg_logical(omit_empty, "omit_empty"));
    PROTECT(simplify = ci__prepare_arg_logical_1(simplify, "simplify"));
    const int simplify1 = LOGICAL_RO(simplify)[0];

    UCollator* collator = NULL;

    STRI__ERROR_HANDLER_BEGIN(5)
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
    R_len_t n_n = LENGTH(n);
    R_len_t omit_empty_n = LENGTH(omit_empty);
    R_len_t vectorize_length = 0;
    // Deviation from stringi: queue recycling warnings while the collator is
    // live and emit them after the collator closes.
    charport::unwind_protect([&]() -> SEXP {
        vectorize_length = ci__recycling_rule(
            false, 4, str_n, pattern_n, n_n, omit_empty_n
        );
        if (vectorize_length > 0 &&
                (vectorize_length%str_n != 0 ||
                 vectorize_length%pattern_n != 0 ||
                 vectorize_length%n_n != 0 ||
                 vectorize_length%omit_empty_n != 0))
            STRI__DEFERRED_WARNINGS.push(MSG__WARN_RECYCLING_RULE);
        return R_NilValue;
    });

    // Deviation from stringi: preinitialize lazy empty vectors because the
    // vectorization order is not sequential, then replace each visited slot
    // with a scalar or exact-size Store once its field count is known.
    vector<charport::charvec::Store> stores;
    stores.reserve(static_cast<size_t>(vectorize_length));
    for (R_len_t i=0; i<vectorize_length; ++i)
        stores.emplace_back(0, 0);
    {
        StriContainerUTF16 str_cont(
            context, str, vectorize_length
        );
        StriContainerUStringSearch pattern_cont(
            context, pattern, vectorize_length, collator
        );  // collator is not owned by pattern_cont
        StriContainerInteger n_cont(n, vectorize_length);
        StriContainerLogical omit_empty_cont(
            omit_empty, vectorize_length
        );
        vector<char> utf8_buffer;
        charport::charvec::Builder output(0);

        for (R_len_t i = pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            if (n_cont.isNA(i)) {
                stores[i] = charport::charvec::Store::scalar(
                    nullptr, 0, cetype_ext_t::CE_NA
                );
                continue;
            }

            int n_cur = n_cont.get(i);
            int omit_empty_cur =
                !omit_empty_cont.isNA(i) && omit_empty_cont.get(i);

            STRI__CONTINUE_ON_EMPTY_OR_NA_STR_PATTERN(
                str_cont, pattern_cont,
                stores[i] = charport::charvec::Store::scalar(
                    nullptr, 0, cetype_ext_t::CE_NA
                );,
            {   if (omit_empty_cont.isNA(i))
                    stores[i] = charport::charvec::Store::scalar(
                        nullptr, 0, cetype_ext_t::CE_NA
                    );
                else if (!(omit_empty_cur || n_cur == 0))
                    stores[i] = charport::charvec::Store::scalar(
                        "", 0, cetype_ext_t::CE_ASCII
                    );
            })

            UStringSearch *matcher = pattern_cont.getMatcher(
                i, str_cont.get(i)
            );
            usearch_reset(matcher);

            if (n_cur >= INT_MAX-1)
                throw StriException(
                    MSG__INCORRECT_NAMED_ARG "; " MSG__EXPECTED_SMALLER,
                    "n"
                );
            else if (n_cur < 0)
                n_cur = INT_MAX;
            else if (n_cur == 0) {
                continue;
            }
            else if (tokens_only1)
                n_cur++; // we need to do one split ahead here

            R_len_t k;
            deque< pair<R_len_t, R_len_t> > fields; // UTF-16 indices
            fields.push_back(pair<R_len_t, R_len_t>(0,0));
            UErrorCode status = U_ZERO_ERROR;

            for (k=1; k < n_cur &&
                    USEARCH_DONE != usearch_next(matcher, &status) &&
                    !U_FAILURE(status); ) {
                R_len_t s1 = (R_len_t)usearch_getMatchedStart(matcher);
                R_len_t s2 =
                    (R_len_t)usearch_getMatchedLength(matcher) + s1;

                if (omit_empty_cur && fields.back().first == s1)
                    fields.back().first = s2; // don't start any new field
                else {
                    fields.back().second = s1;
                    fields.push_back(
                        pair<R_len_t, R_len_t>(s2, s2)
                    ); // start a new field here
                    ++k; // another field
                }
            }
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
            fields.back().second = str_cont.get(i).length();
            if (omit_empty_cur &&
                    fields.back().first == fields.back().second)
                fields.pop_back();

            if (tokens_only1 && n_cur < INT_MAX) {
                n_cur--; // one split ahead could have been made, see above
                while (fields.size() > (size_t)n_cur)
                    fields.pop_back(); // get rid of the remainder
            }

            if (fields.size() == 1) {
                const pair<R_len_t, R_len_t>& curoccur = fields.front();
                if (curoccur.second == curoccur.first &&
                        omit_empty_cont.isNA(i)) {
                    stores[i] = charport::charvec::Store::scalar(
                        nullptr, 0, cetype_ext_t::CE_NA
                    );
                }
                else {
                    UnicodeString field;
                    field.setTo(
                        str_cont.get(i), curoccur.first,
                        curoccur.second-curoccur.first
                    );
                    stores[i] = ci::scalar_store(field, utf8_buffer);
                }
            }
            else if (!fields.empty()) {
                output.reset(static_cast<R_xlen_t>(fields.size()));
                UnicodeString field;
                deque< pair<R_len_t, R_len_t> >::iterator iter =
                    fields.begin();
                for (k = 0; iter != fields.end(); ++iter, ++k) {
                    pair<R_len_t, R_len_t> curoccur = *iter;
                    if (curoccur.second == curoccur.first &&
                            omit_empty_cont.isNA(i)) {
                        output.set_na(k);
                    }
                    else {
                        field.setTo(
                            str_cont.get(i), curoccur.first,
                            curoccur.second-curoccur.first
                        );
                        ci::builder_set(output, k, field, utf8_buffer);
                    }
                }
                stores[i] = output.release_store();
            }
        }
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
        R_len_t n_min = 0;
        charport::unwind_protect([&]() -> SEXP {
            R_len_t n_length = LENGTH(n);
            const int* n_tab = INTEGER_RO(n);
            for (R_len_t i=0; i<n_length; ++i) {
                if (n_tab[i] != NA_INTEGER && n_min < n_tab[i])
                    n_min = n_tab[i];
            }
            return R_NilValue;
        });

        R_len_t matrix_ncol = n_min;
        for (R_len_t i=0; i<vectorize_length; ++i) {
            R_len_t current_size = ci::checked_r_len(
                static_cast<R_xlen_t>(stores[i].size()),
                "split results"
            );
            if (matrix_ncol < current_size)
                matrix_ncol = current_size;
        }

        // Deviation from stringi: reject a matrix that cannot be represented
        // before passing an overflowed product to the flat Builder.
        if (vectorize_length > 0 &&
                matrix_ncol > R_XLEN_T_MAX/vectorize_length)
            throw length_error("matrix length exceeds R's vector limit");
        R_xlen_t matrix_size =
            static_cast<R_xlen_t>(vectorize_length)*matrix_ncol;
        charport::charvec::Builder matrix(matrix_size);
        for (R_len_t i=0; i<vectorize_length; ++i) {
            R_len_t current_size = static_cast<R_len_t>(
                stores[i].size()
            );
            R_len_t j = 0;
            for (; j<current_size; ++j) {
                matrix.set(
                    i+static_cast<R_xlen_t>(j)*vectorize_length,
                    stores[i].view(static_cast<size_t>(j))
                );
            }
            for (; j<matrix_ncol; ++j) {
                R_xlen_t output_i =
                    i+static_cast<R_xlen_t>(j)*vectorize_length;
                if (simplify1 == NA_LOGICAL)
                    matrix.set_na(output_i);
                else
                    ci::builder_set(
                        matrix, output_i, "", 0,
                        cetype_ext_t::CE_ASCII
                    );
            }
        }

        STRI__PROTECT(ret = matrix.to_sexp());
        ret = charport::unwind_protect([&]() -> SEXP {
            SEXP dim;
            PROTECT(dim = Rf_allocVector(INTSXP, 2));
            INTEGER(dim)[0] = vectorize_length;
            INTEGER(dim)[1] = matrix_ncol;
            SEXP result = Rf_setAttrib(ret, R_DimSymbol, dim);
            UNPROTECT(1);
            return result;
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
