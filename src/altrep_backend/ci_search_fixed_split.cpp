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
#include "ci_utf8.h"
#include "fixed/pattern_set.h"
#include "io/integer_input.h"
#include "io/logical_input.h"
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace charr { namespace altrep_backend {
using namespace std;


namespace search_fixed_split {

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

} // namespace search_fixed_split

using namespace search_fixed_split;


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
 *          StriException friendly, use io::Utf8Input
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
 *    use shared::ByteSearchMatcher
 */
SEXP ci_split_fixed(SEXP str, SEXP pattern, SEXP n,
                      SEXP omit_empty, SEXP tokens_only, SEXP simplify, SEXP opts_fixed)
{
    uint32_t pattern_flags = fixed::PatternSet::getByteSearchFlags(opts_fixed);
    bool tokens_only1 = ci__prepare_arg_logical_1_notNA(tokens_only, "tokens_only");
    PROTECT(simplify = ci__prepare_arg_logical_1(simplify, "simplify"));
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));
    PROTECT(n = ci__prepare_arg_integer(n, "n"));
    PROTECT(omit_empty = ci__prepare_arg_logical(omit_empty, "omit_empty"));
    const int simplify_1 = LOGICAL_RO(simplify)[0];

    STRI__ERROR_HANDLER_BEGIN(5)
    SEXP ret;
    {
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    R_len_t str_n = ci::checked_r_len(
        context.size(str), "character vectors"
    );
    R_len_t pattern_n = ci::checked_r_len(
        context.size(pattern), "character vectors"
    );
    R_len_t n_n = 0;
    R_len_t omit_empty_n = 0;
    R_len_t vectorize_length = 0;
    ci::unwind_protect([&]() -> SEXP {
        n_n = LENGTH(n);
        omit_empty_n = LENGTH(omit_empty);
        vectorize_length = ci__recycling_rule(
            STRI__DEFERRED_WARNINGS, 4,
            str_n, pattern_n, n_n, omit_empty_n
        );
        return R_NilValue;
    });

    // Deviation from stringi: preinitialize lazy empty vectors because the
    // vectorization order is not sequential, then replace each visited slot
    // with a scalar or exact-size Store once its field count is known.
    std::vector<charport::charvec::Store> stores;
    stores.reserve(static_cast<size_t>(vectorize_length));
    for (R_len_t i=0; i<vectorize_length; ++i)
        stores.emplace_back(0, 0);
    {
        io::IntegerInput n_cont(n, vectorize_length);
        io::LogicalInput omit_empty_cont(omit_empty, vectorize_length);
        io::Utf8Input str_cont(context, str, vectorize_length);
        fixed::PatternSet pattern_cont(
            context, pattern, vectorize_length, pattern_flags
        );
        charport::charvec::Builder output(0);
        vector<CiSplitField> fields;
        fields.reserve(16);

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
            int  n_cur        = n_cont.get(i);
            int  omit_empty_cur   = !omit_empty_cont.isNA(i) && omit_empty_cont.get(i);

            STRI__CONTINUE_ON_EMPTY_OR_NA_STR_PATTERN(str_cont, pattern_cont,
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

            const io::Utf8Record& str_cur = str_cont.get(i);
            R_len_t     str_cur_n = str_cur.length();
            const char* str_cur_s = str_cur.data();
            const cetype_ext_t field_encoding = str_cur.isASCII()
                ? cetype_ext_t::CE_ASCII
                : cetype_ext_t::CE_ASCII_OR_UTF8;

            if (n_cur >= INT_MAX-1)
                throw StriException(MSG__INCORRECT_NAMED_ARG "; " MSG__EXPECTED_SMALLER, "n");
            else if (n_cur < 0)
                n_cur = INT_MAX;
            else if (n_cur == 0) {
                continue;
            }
            else if (tokens_only1)
                n_cur++; // we need to do one split ahead here

            const io::Utf8Record& pattern_cur = pattern_cont.get(i);
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
                shared::ByteSearchMatcher* matcher = pattern_cont.getMatcher(i);
                matcher->reset(str_cur_s, str_cur_n);
                ci__collect_split_fields(
                    str_cur_n, n_cur, omit_empty_cur, tokens_only1,
                    [&](R_len_t& start, R_len_t& end) {
                        if (shared::ByteSearchMatcher::not_found == matcher->find_next())
                            return false;
                        start = matcher->matched_start();
                        end = start+matcher->matched_length();
                        return true;
                    },
                    fields
                );
            }

            if (fields.size() == 1) {
                const CiSplitField& curoccur = fields.front();
                if (curoccur.second == curoccur.first &&
                        omit_empty_cont.isNA(i)) {
                    stores[i] = charport::charvec::Store::scalar(
                        nullptr, 0, cetype_ext_t::CE_NA
                    );
                }
                else {
                    const char* value = str_cur_s+curoccur.first;
                    size_t value_length = static_cast<size_t>(
                        curoccur.second-curoccur.first
                    );
                    stores[i] = ci::scalar_store(
                        value, value_length, field_encoding
                    );
                }
            }
            else if (!fields.empty()) {
                output.reset(static_cast<R_xlen_t>(fields.size()));
                vector<CiSplitField>::const_iterator iter =
                    fields.begin();
                R_len_t k = 0;
                for (k = 0; iter != fields.end(); ++iter, ++k) {
                    const CiSplitField& curoccur = *iter;
                    if (curoccur.second == curoccur.first &&
                            omit_empty_cont.isNA(i)) {
                        output.set_na(k);
                    }
                    else {
                        ci::builder_set(
                            output, k, str_cur_s+curoccur.first,
                            curoccur.second-curoccur.first, field_encoding
                        );
                    }
                }
                stores[i] = output.release_store();
            }
        }
    }

    if (simplify_1 != NA_LOGICAL && !simplify_1) {
        STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
            return Rf_allocVector(VECSXP, vectorize_length);
        }));
        ci::unwind_protect([&]() -> SEXP {
            for (R_len_t i=0; i<vectorize_length; ++i) {
                SEXP ans = PROTECT(charport::charvec::wrap(
                    std::move(stores[i])
                ));
                SET_VECTOR_ELT(ret, i, ans);
                UNPROTECT(1);
            }
            return R_NilValue;
        });
    }
    else {
        R_len_t n_min = 0;
        ci::unwind_protect([&]() -> SEXP {
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
            static_cast<R_xlen_t>(vectorize_length) * matrix_ncol;
        charport::charvec::Builder matrix(matrix_size);
        for (R_len_t i=0; i<vectorize_length; ++i) {
            R_len_t current_size = static_cast<R_len_t>(stores[i].size());
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
                if (simplify_1 == NA_LOGICAL)
                    matrix.set_na(output_i);
                else
                    ci::builder_set(
                        matrix, output_i, "", 0,
                        cetype_ext_t::CE_ASCII
                    );
            }
        }

        STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
            return matrix.to_sexp();
        }));
        ret = ci::unwind_protect([&]() -> SEXP {
            SEXP dim;
            PROTECT(dim = Rf_allocVector(INTSXP, 2));
            INTEGER(dim)[0] = vectorize_length;
            INTEGER(dim)[1] = matrix_ncol;
            SEXP result = Rf_setAttrib(ret, R_DimSymbol, dim);
            UNPROTECT(1);
            return result;
        });
    }

    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(; /* nothing interesting on error */)
}

} } // namespace charr::altrep_backend
