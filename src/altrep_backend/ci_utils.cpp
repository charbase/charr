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
#include "ci_container_listutf8.h"

#include <cstring>


/**
 * Convert list to a character matrix
 *
 * @param x a list
 * @param fill single string
 * @param byrow single logical value
 * @param n_min single integer
 * @return character matrix
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-10-23)
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-04)
 *    new arg: n_min
 */
SEXP ci_list2matrix(SEXP x, SEXP byrow, SEXP fill, SEXP n_min)
{
    bool byrow2 = ci__prepare_arg_logical_1_notNA(byrow, "byrow");
    R_len_t n_min2 = ci__prepare_arg_integer_1_notNA(n_min, "n_min");
    if (n_min2 < 0) Rf_error(MSG__INCORRECT_NAMED_ARG "; " MSG__EXPECTED_NONNEGATIVE, "n_min");
    PROTECT(x = ci__prepare_arg_list_string(x, "x"));
    PROTECT(fill = ci__prepare_arg_string_1(fill, "fill")); // enc2utf8 called in R

    STRI__ERROR_HANDLER_BEGIN(2)
    SEXP ret;
    const R_len_t n = LENGTH(x);
    R_len_t m = n_min2;
    // Deviation from stringi: stage the protected list's child SEXPs before
    // opening any Reader, so later list access cannot run inside a borrow.
    std::vector<SEXP> elements(static_cast<size_t>(n));
    charport::unwind_protect([&]() -> SEXP {
        for (R_len_t i=0; i<n; ++i)
            elements[static_cast<size_t>(i)] = VECTOR_ELT(x, i);
        return R_NilValue;
    });
    {
        ci::ReaderContext context(STRI__DEFERRED_WARNINGS);

        for (R_len_t i=0; i<n; ++i) {
            const R_len_t k = ci::checked_r_len(
                context.size(elements[static_cast<size_t>(i)]),
                "character vectors"
            );
            if (k > m)
                m = k;
        }

        std::shared_ptr<ci::ReaderBorrow> fill_borrow =
            context.acquire(fill);
        const charport::StrView fill_view = fill_borrow->views()[0];

        const R_xlen_t rows = byrow2 ? n : m;
        const R_xlen_t columns = byrow2 ? m : n;
        // Deviation from stringi: the flat Builder needs the matrix product
        // checked before allocation; Rf_allocMatrix performed this internally.
        if (rows > 0 && columns > R_XLEN_T_MAX/rows)
            throw std::length_error("matrix length exceeds R's vector limit");

        charport::charvec::Builder builder(rows*columns);
        for (R_len_t i=0; i<n; ++i) {
            std::shared_ptr<ci::ReaderBorrow> current_borrow =
                context.acquire(elements[static_cast<size_t>(i)]);
            const charport::StrViews& current = current_borrow->views();
            const R_len_t current_size = static_cast<R_len_t>(
                current.size()
            );
            for (R_len_t j=0; j<m; ++j) {
                const R_xlen_t output_index = byrow2 ?
                    i+static_cast<R_xlen_t>(j)*n :
                    j+static_cast<R_xlen_t>(i)*m;
                const charport::StrView value =
                    j < current_size ? current[j] : fill_view;
                if (value.is_na()) {
                    builder.set_na(output_index);
                }
                else {
                    ci::builder_set(
                        builder, output_index, value.ptr,
                        static_cast<size_t>(value.len), value.enc
                    );
                }
            }
        }

        fill_borrow.reset();
        STRI__PROTECT(ret = builder.to_sexp());
    }

    ret = charport::unwind_protect([&]() -> SEXP {
        SEXP dim;
        PROTECT(dim = Rf_allocVector(INTSXP, 2));
        INTEGER(dim)[0] = byrow2 ? n : m;
        INTEGER(dim)[1] = byrow2 ? m : n;
        SEXP result = Rf_setAttrib(ret, R_DimSymbol, dim);
        UNPROTECT(1);
        return result;
    });
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;

    STRI__ERROR_HANDLER_END({/* no-op on err */})
}




/**
* Replace NAs with a given string
*
*
* @param str character vector
* @param replacement single string
* @return character vector
*
* @version 0.2-1 (Bartek Tartanus, 2014-03-15)
*
* @version 0.2-1 (Marek Gagolewski, 2014-04-02)
*          Use Utf8Input for replacement
*
* @version 0.3-1 (Marek Gagolewski, 2014-11-05)
*    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
*/
SEXP ci_replace_na(SEXP str, SEXP replacement) {
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(replacement = ci__prepare_arg_string_1(replacement, "replacement"));

    // @TODO: ci_replace_na(str, character(0)) returns a char vect with no NAs

    STRI__ERROR_HANDLER_BEGIN(2)
    SEXP ret = R_NilValue;
    // An input with no NAs and no attributes already is the exact result.
    // This deliberately does not require a charvec source: rebuilding an
    // ordinary STRSXP would copy every payload byte into fresh slices only to
    // produce a value identical to the input. The base backend has always
    // returned the input here (ci_utils.cpp, !has_na && NO_ATTRIB); gating this
    // on charvec-ness was the source of a 1.5x regression against base.
    const bool source_is_result_shaped = NO_ATTRIB(str) != 0;

    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    charport::charvec::Builder builder(0);
    bool build_output = false;
    {
        std::shared_ptr<ci::ReaderBorrow> str_borrow = context.acquire(str);
        const charport::StrViews& views = str_borrow->views();
        const R_len_t str_len = ci::checked_r_len(
            str_borrow->size(), "character vectors"
        );
        bool direct = true;
        bool has_na = false;
        for (R_len_t i=0; i<str_len; ++i) {
            const charport::StrView value = views[i];
            if (value.is_na()) {
                has_na = true;
                continue;
            }
            if (value.enc == cetype_ext_t::CE_BYTES)
                throw StriException(MSG__BYTESENC);
            if ((value.enc != cetype_ext_t::CE_ASCII &&
                 value.enc != cetype_ext_t::CE_UTF8) ||
                    (value.enc == cetype_ext_t::CE_UTF8 &&
                     STRI__ENC_HAS_BOM_UTF8(value.ptr, value.len))) {
                direct = false;
                break;
            }
        }

        if (direct) {
            if (!has_na && source_is_result_shaped) {
                // Constructed for its validation side effect: an invalid
                // replacement must still signal even when nothing is replaced.
                Utf8Input replacement_cont(
                    context, replacement, 1
                );
                ret = str;
            }
            else {
                // charvec::Builder copies into its own slices, so unlike the
                // base backend (which reuses the source CHARSXPs via
                // SET_STRING_ELT) this rebuild is O(total bytes), not O(n).
                // Unavoidable while Builder has no borrow mode.
                builder.reset(str_len);
                build_output = true;
                {
                    Utf8Input replacement_cont(
                        context, replacement, 1
                    );
                    for (R_len_t i=0; i<str_len; ++i) {
                        const charport::StrView value = views[i];
                        if (value.is_na()) {
                            ci::builder_set(
                                builder, i, replacement_cont.getNAble(0)
                            );
                        }
                        else {
                            ci::builder_set(
                                builder, i, value.ptr,
                                static_cast<size_t>(value.len), value.enc
                            );
                        }
                    }
                }
            }
        }
        else {
            builder.reset(str_len);
            build_output = true;
            {
                Utf8Input str_cont(context, str, str_len);
                Utf8Input replacement_cont(context, replacement, 1);

                for (R_len_t i=0; i<str_len; ++i) {
                    if (str_cont.isNA(i))
                        ci::builder_set(
                            builder, i, replacement_cont.getNAble(0)
                        );
                    else
                        ci::builder_set(builder, i, str_cont.get(i));
                }
            }
        }
    }

    if (build_output)
        STRI__PROTECT(ret = builder.to_sexp());

    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}
