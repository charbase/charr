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
#include "ci_container_base.h"
#include "ci_container_utf8.h"
#include "ci_container_integer.h"
#include "ci_container_listutf8.h"
#include "ci_string8buf.h"
#include <utility>
#include <vector>
using namespace std;


namespace {


struct ScalarStringInfo {
    bool is_na;
    bool is_empty;
};


// Deviation from stringi: inspect only scalar NA and byte length through a
// short Reader borrow. Normalization, including bytes errors, stays deferred
// to the copied container boundary instead of materializing a CHARSXP here.
SEXP ci__inspect_scalar_string(SEXP source, ScalarStringInfo& info)
{
    STRI__ERROR_HANDLER_BEGIN(0)
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    {
        std::shared_ptr<ci::ReaderBorrow> borrow = context.acquire(source);
        if (borrow->size() != 1)
            throw StriException(MSG__INTERNAL_ERROR);

        const charport::StrView value = borrow->views()[0];
        info.is_na = value.is_na();
        info.is_empty = !info.is_na && value.len == 0;
    }
    context.emitWarnings();
    return R_NilValue;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


} // namespace


/**
 * Prepare list argument -- ignore empty vectors if needed, used by ci_paste
 *
 * @param x a list of strings
 * @param ignore_null FALSE to do nothing
 * @return a list vector
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-27)
 */
SEXP ci__prepare_arg_list_ignore_null(SEXP x, bool ignore_null)
{
    if (!ignore_null)
        return x;

    PROTECT(x);

#ifndef NDEBUG
    if (!Rf_isVectorList(x))
        Rf_error("ci__prepare_arg_list_ignore_null:: !NDEBUG: not a list"); // error() allowed here
#endif

    R_len_t narg = LENGTH(x);
    if (narg <= 0) {
        UNPROTECT(1);
        return x;
    }
//   else if (narg == 1 && LENGTH(VECTOR_ELT(x, 0)) == 0) {
//      UNPROTECT(1);
//      return Rf_allocVector(VECSXP, 0);
//   }

    SEXP ret;
//   if (ignore_null != NA_INTEGER && ignore_null < 0) { // remove NULL elements
    R_len_t nret = 0;
    for (R_len_t i=0; i<narg; ++i) {
#ifndef NDEBUG
        if (!Rf_isVector(VECTOR_ELT(x, i)))
            Rf_error("ci__prepare_arg_list_ignore_null:: !NDEBUG: not a vector element"); // error() allowed here
#endif
        if (LENGTH(VECTOR_ELT(x, i)) > 0)
            ++nret;
    }

    PROTECT(ret = Rf_allocVector(VECSXP, nret));
    for (R_len_t i=0, j=0; i<narg; ++i) {
        if (LENGTH(VECTOR_ELT(x, i)) > 0)
            SET_VECTOR_ELT(ret, j++, VECTOR_ELT(x, i));
    }
//   }
//   else { // insert one empty string
//      PROTECT(ret = Rf_allocVector(VECSXP, narg));
//      for (R_len_t i=0; i<narg; ++i) {
//         if (LENGTH(VECTOR_ELT(x, i)) > 0)
//            SET_VECTOR_ELT(ret, i, VECTOR_ELT(x, i));
//         else if (ignore_null != NA_INTEGER)
//            SET_VECTOR_ELT(ret, i, ci__vector_empty_strings(1));
////         else
////            SET_VECTOR_ELT(ret, i, ci__vector_NA_strings(1));
//      }
//   }
    UNPROTECT(2);
    return ret;
}


/** Duplicate given strings
 *
 *
 *  @param str character vector
 *  @param times integer vector
 *  @return character vector
 *
 *  The function is vectorized over str and times
 *  if str is NA or times is NA the result will be NA
 *  if times < 0, the result will be NA
 *  if times==0, the result will be an empty string
 *  if str or times is an empty vector, then the result is an empty vector
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *     use StriContainerUTF8's vectorization
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-15)
 *     use StriContainerInteger
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *     make StriException friendly
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.7.6.9001 (Marek Gagolewski, 2022-03-15)
 *    #473: use size_t
*/
SEXP ci_dup(SEXP str, SEXP times)
{
    PROTECT(str = ci__prepare_arg_string(str, "str")); // prepare string argument
    PROTECT(times = ci__prepare_arg_integer(times, "times")); // prepare string argument
    R_len_t vectorize_length = ci__recycling_rule(true, 2, LENGTH(str), LENGTH(times));

    STRI__ERROR_HANDLER_BEGIN(2)
    if (vectorize_length <= 0) {
        charport::charvec::Builder builder(0);
        SEXP ret;
        STRI__PROTECT(ret = builder.to_sexp());
        STRI__UNPROTECT_ALL
        return ret;
    }

    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    charport::charvec::Builder builder(vectorize_length);
    StriContainerInteger times_cont(times, vectorize_length);
    {
        StriContainerUTF8 str_cont(context, str, vectorize_length);

        // STEP 1.
        // Calculate the required buffer length
        size_t bufsize = 0;
        for (R_len_t i=0; i<vectorize_length; ++i) {
            if (str_cont.isNA(i) || times_cont.isNA(i) || times_cont.get(i) < 0)
                continue;

            size_t cursize = times_cont.get(i) * str_cont.get(i).length();
            if (cursize > bufsize)
                bufsize = cursize;
        }

        if (bufsize > POW_2_31_M_1)
            throw StriException(MSG__CHARSXP_2147483647);

        // STEP 2.
        // Alloc buffer & result vector
        String8buf buf(bufsize);
        // STEP 3.
        // Duplicate
        const String8* str_last = NULL; // this will allow for reusing buffer...
        size_t str_last_index  = 0;    // ...useful for ci_dup('a', 1:1000) or ci_dup('a', 1000:1)

        for (R_len_t i = str_cont.vectorize_init(); // this iterator allows for...
                i != str_cont.vectorize_end();        // ...smart buffer reusage
                i = str_cont.vectorize_next(i))
        {
            R_len_t times_cur;
            if (str_cont.isNA(i) || times_cont.isNA(i) || (times_cur = times_cont.get(i)) < 0) {
                builder.set_na(i);
                continue;
            }

            const String8* str_cur = &(str_cont.get(i));
            R_len_t str_cur_n = str_cur->length();
            if (times_cur <= 0 || str_cur_n <= 0) {
                ci::builder_set(
                    builder, i, "", 0, cetype_ext_t::CE_ASCII
                );
                continue;
            }

            // all right, here the result will neither be NA nor an empty string

            if (str_cur != str_last) {
                // well, no reuse possible - resetting
                str_last = str_cur;
                str_last_index = 0;
            }

            // we paste only "additional" duplicates
            size_t max_index = str_cur_n*times_cur;
            for (; str_last_index < max_index; str_last_index += str_cur_n) {
                if (buf.size() < str_last_index+str_cur_n) {
                    throw StriException(MSG__INTERNAL_ERROR);
                }
                memcpy(buf.data()+str_last_index, str_cur->data(), (size_t)str_cur_n);
            }

            // the result is always in UTF-8
            ci::builder_set(
                builder, i, buf.data(), max_index,
                cetype_ext_t::CE_ASCII_OR_UTF8
            );
        }
    }


    // STEP 4.
    // Clean up & finish

    SEXP ret;
    STRI__PROTECT(ret = builder.to_sexp());
    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/** Join two character vectors, element by element, no separator, no collapse
 *
 * Vectorized over e1 and e2. Optimized for |e1| >= |e2|
 * (but no harm otherwise)
 *
 * This is used by %s+% operator in stringi R code.
 *
 * @param e1 character vector
 * @param e2 character vector
 * @return character vector, res_i=s1_i + s2_i for |e1|==|e2|
 *  if e1 or e2 is NA then result is NA
 *  if e1 or e2 is empty, then the result is just e1 or e2
 *
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *    use StriContainerUTF8's vectorization
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *    make StriException friendly
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
*/
SEXP ci_join2(SEXP e1, SEXP e2) // a.k.a. ci_join2_nocollapse
{
    PROTECT(e1 = ci__prepare_arg_string(e1, "e1")); // prepare string argument
    PROTECT(e2 = ci__prepare_arg_string(e2, "e2")); // prepare string argument

    R_len_t e1_length = LENGTH(e1);
    R_len_t e2_length = LENGTH(e2);
    R_len_t vectorize_length = ci__recycling_rule(true, 2, e1_length, e2_length);

    if (e1_length <= 0) {
        UNPROTECT(2);
        return e1;
    }
    if (e2_length <= 0) {
        UNPROTECT(2);
        return e2;
    }

    STRI__ERROR_HANDLER_BEGIN(2)
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    charport::charvec::Builder builder(vectorize_length);
    {
        StriContainerUTF8 e1_cont(context, e1, vectorize_length);
        StriContainerUTF8 e2_cont(context, e2, vectorize_length);

        // 1. find maximal length of the buffer needed
        size_t nchar = 0;
        for (int i=0; i<vectorize_length; ++i) {
            if (e1_cont.isNA(i) || e2_cont.isNA(i))
                continue;

            size_t c1 = e1_cont.get(i).length();
            size_t c2 = e2_cont.get(i).length();

            if (c1+c2 > nchar) nchar = c1+c2;
        }

        // 2. Create buf & retval
        if (nchar > POW_2_31_M_1)
            throw StriException(MSG__CHARSXP_2147483647);

        String8buf buf(nchar);
        // 3. Set retval
        const String8* last_string_1 = NULL;
        R_len_t last_buf_idx = 0;
        for (R_len_t i = e1_cont.vectorize_init(); // this iterator allows for...
                i != e1_cont.vectorize_end();        // ...smart buffer reusage
                i = e1_cont.vectorize_next(i))
        {
            if (e1_cont.isNA(i) || e2_cont.isNA(i)) {
                builder.set_na(i);
                continue;
            }

            // If e1 has length < length of e2, this will be faster:
            const String8* cur_string_1 = &(e1_cont.get(i));
            if (cur_string_1 != last_string_1) {
                last_string_1 = cur_string_1;
                last_buf_idx = cur_string_1->length();
                memcpy(buf.data(), cur_string_1->data(), (size_t)last_buf_idx);
            }
            // else reuse string #1

            const String8* cur_string_2 = &(e2_cont.get(i));
            R_len_t  cur_len_2 = cur_string_2->length();
            memcpy(buf.data()+last_buf_idx, cur_string_2->data(), (size_t)cur_len_2);
            // the result is always in UTF-8
            ci::builder_set(
                builder, i, buf.data(), last_buf_idx+cur_len_2,
                cetype_ext_t::CE_ASCII_OR_UTF8
            );
        }
    }

    // 4. Cleanup & finish
    SEXP ret;
    STRI__PROTECT(ret = builder.to_sexp());
    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/** Join and flatten two character vectors, no separator between elements but possibly with collapse
 *
 * Vectorized over e1 and e2.
 *
 * @param e1 character vector
 * @param e2 character vector
 * @param collapse single string or NULL
 * @return character vector
 *
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-18)
 *          first version;
 *          This is much faster than ci_flatten(ci_join2(...), ...)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 *  @version 0.4-1 (Marek Gagolewski, 2014-11-26)
 *    #114: inconsistent behaviour w.r.t. paste()
*/
SEXP ci_join2_withcollapse(SEXP e1, SEXP e2, SEXP collapse)
{
    if (Rf_isNull(collapse)) {
        // no collapse - used, e.g., by the %s+% operator
        return ci_join2(e1, e2); // a.k.a. ci_join2_nocollapse
    }

    PROTECT(e1 = ci__prepare_arg_string(e1, "e1")); // prepare string argument
    PROTECT(e2 = ci__prepare_arg_string(e2, "e2")); // prepare string argument
    PROTECT(collapse = ci__prepare_arg_string_1(collapse, "collapse"));

    ScalarStringInfo collapse_info = {false, false};
    ci__inspect_scalar_string(collapse, collapse_info);
    R_len_t e1_length = 0;
    R_len_t e2_length = 0;
    R_len_t vectorize_length = 0;
    if (!collapse_info.is_na) {
        e1_length = LENGTH(e1);
        e2_length = LENGTH(e2);
        vectorize_length = ci__recycling_rule(
            true, 2, e1_length, e2_length
        );
    }

    STRI__ERROR_HANDLER_BEGIN(3)
    if (collapse_info.is_na) {
        charport::charvec::Store output =
            charport::charvec::Store::scalar(
                NULL, 0, cetype_ext_t::CE_NA
            );
        SEXP ret;
        STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
        STRI__UNPROTECT_ALL
        return ret;
    }

    if (e1_length <= 0 || e2_length <= 0) {
        charport::charvec::Store output = ci::scalar_store(
            "", 0, cetype_ext_t::CE_ASCII
        );
        SEXP ret;
        STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
        STRI__UNPROTECT_ALL
        return ret;
    }

    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    charport::charvec::Store output(0, 0);
    {
        StriContainerUTF8 e1_cont(context, e1, vectorize_length);
        StriContainerUTF8 e2_cont(context, e2, vectorize_length);
        StriContainerUTF8 collapse_cont(context, collapse, 1);
        R_len_t collapse_nbytes = collapse_cont.get(0).length();
        const char* collapse_s = collapse_cont.get(0).data();


        // find maximal length of the buffer needed:
        size_t nchar = 0;
        bool has_na = false;
        for (int i=0; i<vectorize_length; ++i) {
            if (e1_cont.isNA(i) || e2_cont.isNA(i)) {
                has_na = true;
                break;
            }

            nchar += e1_cont.get(i).length() + e2_cont.get(i).length()
                     + ((i>0)?collapse_nbytes:0);
        }


        if (has_na) {
            output = charport::charvec::Store::scalar(
                NULL, 0, cetype_ext_t::CE_NA
            );
        }
        else {
            if (nchar > POW_2_31_M_1)
                throw StriException(MSG__CHARSXP_2147483647);
            String8buf buf(nchar);
            R_len_t last_buf_idx = 0;
            for (R_len_t i = 0; i < vectorize_length; ++i) // don't change this order, see #114
            {
                // no need to detect NAs - they already have been excluded
                if (collapse_nbytes > 0 && i > 0) { // copy collapse (separator)
                    memcpy(buf.data()+last_buf_idx, collapse_s, (size_t)collapse_nbytes);
                    last_buf_idx += collapse_nbytes;
                }

                const String8* cur_string_1 = &(e1_cont.get(i));
                R_len_t  cur_len_1 = cur_string_1->length();
                memcpy(buf.data()+last_buf_idx, cur_string_1->data(), (size_t)cur_len_1);
                last_buf_idx += cur_len_1;

                const String8* cur_string_2 = &(e2_cont.get(i));
                R_len_t  cur_len_2 = cur_string_2->length();
                memcpy(buf.data()+last_buf_idx, cur_string_2->data(), (size_t)cur_len_2);
                last_buf_idx += cur_len_2;
            }

            output = ci::scalar_store(
                buf.data(), last_buf_idx,
                cetype_ext_t::CE_ASCII_OR_UTF8
            );
        }
    }

    SEXP ret;
    STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/**
 * Concatenate Character Vectors, with no collapse
 *
 * @param strlist list of character vectors
 * @param sep single string
 * @param ignore_null single integer
 * @return character vector
 *
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          use StriContainerUTF8's vectorization
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException-friendly, useStriContainerListUTF8
 *
 * @version 0.1-12 (Marek Gagolewski, 2013-12-04)
 *          fixed bug #49
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-18)
 *          ci_join has been split to ci_join_nocollapse
 *          and ci_join_withcollapse (for efficiency reasons)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-27)
 *    FR #116: ignore_null arg added
 */
SEXP ci_join_nocollapse(SEXP strlist, SEXP sep, SEXP ignore_null)
{
    bool ignore_null1 = ci__prepare_arg_logical_1_notNA(ignore_null, "ignore_null");
    PROTECT(strlist = ci__prepare_arg_list_ignore_null(
                          ci__prepare_arg_list_string(strlist, "..."), ignore_null1
                      ));
    R_len_t strlist_length = LENGTH(strlist);
    if (strlist_length <= 0) {
        STRI__ERROR_HANDLER_BEGIN(1)
        charport::charvec::Builder builder(0);
        SEXP ret;
        STRI__PROTECT(ret = builder.to_sexp());
        STRI__UNPROTECT_ALL
        return ret;
        STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
    }

    // get length of the longest character vector on the list, i.e., vectorize_length
    R_len_t vectorize_length = 0;
    for (R_len_t i=0; i<strlist_length; ++i) {
        R_len_t strlist_cur_length = LENGTH(VECTOR_ELT(strlist, i));
        if (strlist_cur_length <= 0) {
            STRI__ERROR_HANDLER_BEGIN(1)
            charport::charvec::Builder builder(0);
            SEXP ret;
            STRI__PROTECT(ret = builder.to_sexp());
            STRI__UNPROTECT_ALL
            return ret;
            STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
        }
        if (strlist_cur_length > vectorize_length)
            vectorize_length = strlist_cur_length;
    }

    PROTECT(sep = ci__prepare_arg_string_1(sep, "sep"));
    ScalarStringInfo sep_info = {false, false};
    ci__inspect_scalar_string(sep, sep_info);
    if (sep_info.is_na) {
        STRI__ERROR_HANDLER_BEGIN(2)
        charport::charvec::Builder builder(vectorize_length);
        for (R_len_t i=0; i<vectorize_length; ++i)
            builder.set_na(i);
        SEXP ret;
        STRI__PROTECT(ret = builder.to_sexp());
        STRI__UNPROTECT_ALL
        return ret;
        STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
    }


    // * special case *
    if (sep_info.is_empty && strlist_length == 2) {
        // sep==empty string and 2 vectors --
        // an often occurring case - we have some specialized functions for this :-)
        SEXP ret;
        PROTECT(ret = ci_join2(VECTOR_ELT(strlist, 0), VECTOR_ELT(strlist, 1))); // a.k.a. ci_join2_nocollapse
        UNPROTECT(3);
        return ret;
    }

    // note that if 1 vector is given
    // we cannot return VECTOR_ELT(strlist, 0) directly
    // -- it needs to be converted to UTF8
    // so we proceed

    STRI__ERROR_HANDLER_BEGIN(2)
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    charport::charvec::Builder builder(vectorize_length);
    {
        StriContainerUTF8 sep_cont(context, sep, 1);
        const char* sep_char = sep_cont.get(0).data();
        R_len_t     sep_len  = sep_cont.get(0).length();

        StriContainerListUTF8 strlist_cont(
            context, strlist, vectorize_length
        );


        // 4. Get buf size and determine where NAs will occur
        size_t buf_maxbytes = 0;
        vector<bool> whichNA(vectorize_length, false); // where are NAs in out?
        for (R_len_t i=0; i<vectorize_length; ++i) {

            size_t curchar = 0;
            for (R_len_t j=0; j<strlist_length; ++j) {
                if (strlist_cont.get(j).isNA(i)) {
                    whichNA[i] = true;
                    break;
                }
                else {
                    curchar += strlist_cont.get(j).get(i).length()
                               + ((j>0)?sep_len:0);
                }
            }
            if (!whichNA[i] && curchar > buf_maxbytes)
                buf_maxbytes = curchar;
        }

        // 5. Create ret val
        if (buf_maxbytes > POW_2_31_M_1)
            throw StriException(MSG__CHARSXP_2147483647);
        String8buf buf(buf_maxbytes);

        for (R_len_t i=0; i<vectorize_length; ++i) {
            if (whichNA[i]) {
                builder.set_na(i);
                continue;
            }

            size_t cursize = 0;
            for (R_len_t j=0; j<strlist_length; ++j) {

                if (sep_len >= 0 && j > 0) {
                    memcpy(buf.data()+cursize, sep_char, (size_t)sep_len);
                    cursize += sep_len;
                }

                const String8* curstring = &(strlist_cont.get(j).get(i));
                size_t curstring_n = curstring->length();
                memcpy(buf.data()+cursize, curstring->data(), (size_t)curstring_n);
                cursize += curstring_n;
            }

            ci::builder_set(
                builder, i, buf.data(), cursize,
                cetype_ext_t::CE_ASCII_OR_UTF8
            );
        }
    }

    // nothing more to do:
    SEXP ret;
    STRI__PROTECT(ret = builder.to_sexp());
    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return ret;

    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/**
 * Concatenate Character Vectors, possibly with collapse
 *
 * @param strlist list of character vectors
 * @param sep single string
 * @param collapse single string or NULL
 * @param ignore_null single integer
 * @return character vector
 *
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-18)
 *          a specialized version of the original ci_join, which
 *          called ci_flatten at the end, if it was requested;
 *          now collapsing is done directly (for time and memory efficiency);
 *          Now calling specialized functions
 *          ci_join2_withcollapse and ci_flatten_withressep, if needed.
 *          If collapse!=NULL and sep=NA, then the result will be single NA
 *          (and not n*NA);
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-27)
 *    FR #116: ignore_null arg added
 */
SEXP ci_join(SEXP strlist, SEXP sep, SEXP collapse, SEXP ignore_null)
{
    // no collapse-case is handled separately:
    if (Rf_isNull(collapse))
        return ci_join_nocollapse(strlist, sep, ignore_null);

    // *result will surely be a single string*

    bool ignore_null1 = ci__prepare_arg_logical_1_notNA(ignore_null, "ignore_null");
    PROTECT(strlist = ci__prepare_arg_list_ignore_null(
                          ci__prepare_arg_list_string(strlist, "..."), ignore_null1
                      ));
    R_len_t strlist_length = LENGTH(strlist);
    if (strlist_length <= 0) {
        STRI__ERROR_HANDLER_BEGIN(1)
        charport::charvec::Store output = ci::scalar_store(
            "", 0, cetype_ext_t::CE_ASCII
        );
        SEXP ret;
        STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
        STRI__UNPROTECT_ALL
        return ret;
        STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
    }
    else if (strlist_length == 1) {
        // one vector + collapse string -- another frequently occurring case
        // sep is ignored here
        SEXP ret;
        PROTECT(ret = ci_flatten(VECTOR_ELT(strlist, 0), collapse)); // a.k.a. ci_flatten_withressep
        UNPROTECT(2);
        return ret;
    }

    PROTECT(sep = ci__prepare_arg_string_1(sep, "sep"));
    PROTECT(collapse = ci__prepare_arg_string_1(collapse, "collapse"));
    ScalarStringInfo sep_info = {false, false};
    ScalarStringInfo collapse_info = {false, false};
    ci__inspect_scalar_string(sep, sep_info);
    if (!sep_info.is_na)
        ci__inspect_scalar_string(collapse, collapse_info);
    if (sep_info.is_na || collapse_info.is_na) {
        STRI__ERROR_HANDLER_BEGIN(3)
        charport::charvec::Store output =
            charport::charvec::Store::scalar(
                NULL, 0, cetype_ext_t::CE_NA
            );
        SEXP ret;
        STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
        STRI__UNPROTECT_ALL
        return ret;
        STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
    }
    else if (sep_info.is_empty && strlist_length == 2) {
        // sep==empty string and 2 vectors --
        // an often occurring case - we have some specialized functions for this :-)
        SEXP ret;
        PROTECT(ret = ci_join2_withcollapse(VECTOR_ELT(strlist, 0), VECTOR_ELT(strlist, 1), collapse));
        UNPROTECT(4);
        return ret;
    }

    // get length of the longest character vector on the list, i.e., vectorize_length
    R_len_t vectorize_length = 0;
    for (R_len_t i=0; i<strlist_length; ++i) {
        R_len_t strlist_cur_length = LENGTH(VECTOR_ELT(strlist, i));
        if (strlist_cur_length <= 0) {
            STRI__ERROR_HANDLER_BEGIN(3)
            charport::charvec::Store output = ci::scalar_store(
                "", 0, cetype_ext_t::CE_ASCII
            );
            SEXP ret;
            STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
            STRI__UNPROTECT_ALL
            return ret;
            STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
        }
        if (strlist_cur_length > vectorize_length)
            vectorize_length = strlist_cur_length;
    }


    STRI__ERROR_HANDLER_BEGIN(3)
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    charport::charvec::Store output(0, 0);
    {
        StriContainerListUTF8 strlist_cont(
            context, strlist, vectorize_length
        );

        StriContainerUTF8 sep_cont(context, sep, 1); // definitely not NA
        const char* sep_s = sep_cont.get(0).data();
        R_len_t     sep_n = sep_cont.get(0).length();

        StriContainerUTF8 collapse_cont(
            context, collapse, 1
        ); // definitely not NA
        const char* collapse_s = collapse_cont.get(0).data();
        R_len_t     collapse_n = collapse_cont.get(0).length();

        // Get required buffer size
        size_t buf_maxbytes = 0;
        bool has_na = false;
        for (R_len_t i=0; i<vectorize_length; ++i) {   // for each vectorized string (vertically)
            for (R_len_t j=0; j<strlist_length; ++j) {  // for each character vector  (horizontally)
                if (strlist_cont.get(j).isNA(i)) {
                    has_na = true;
                    break;
                }

                buf_maxbytes += strlist_cont.get(j).get(i).length()+ ((j>0)?sep_n:0);
            }

            if (has_na)
                break;
            if (i>0) buf_maxbytes += collapse_n;
        }

        if (has_na) {
            output = charport::charvec::Store::scalar(
                NULL, 0, cetype_ext_t::CE_NA
            );
        }
        else {
            // 5. Create ret val
            if (buf_maxbytes > POW_2_31_M_1)
                throw StriException(MSG__CHARSXP_2147483647);
            String8buf buf(buf_maxbytes);
            size_t last_buf_idx = 0;

            for (R_len_t i=0; i<vectorize_length; ++i) {
                // there is no NA anywhere

                if (collapse_n > 0 && i > 0) {
                    memcpy(buf.data()+last_buf_idx, collapse_s, (size_t)collapse_n);
                    last_buf_idx += collapse_n;
                }

                for (R_len_t j=0; j<strlist_length; ++j) {

                    if (sep_n > 0 && j > 0) {
                        memcpy(buf.data()+last_buf_idx, sep_s, (size_t)sep_n);
                        last_buf_idx += sep_n;
                    }

                    const String8* curstring = &(strlist_cont.get(j).get(i));
                    size_t curstring_n = curstring->length();
                    memcpy(buf.data()+last_buf_idx, curstring->data(), (size_t)curstring_n);
                    last_buf_idx += curstring_n;
                }
            }

#ifndef NDEBUG
            if (buf_maxbytes != last_buf_idx)
                throw StriException("ci_join_withcollapse: buffer overrun");
#endif

            output = ci::scalar_store(
                buf.data(), last_buf_idx,
                cetype_ext_t::CE_ASCII_OR_UTF8
            );
        }
    }

    // we are done
    SEXP ret;
    STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return ret;

    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/** String vector flatten, with no separator (i.e., empty) between each string
 *
 *  if any of s is NA, the result will be NA_character_
 *
 *  @param s character vector
 *  @return if s is not empty, then a character vector of length 1
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          StriContainerUTF8 - any R Encoding
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException friendly
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-18)
 *          This function hasn't been used at all before (strange, isn't it?);
 *          From now on it's being called by ci_flatten_withressep
 *          (a small performance gain)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.2.1 (Marek Gagolewski, 2018-04-20)
 *    na_empty arg added
 *
 * @version 1.6.2 (Marek Gagolewski, 2021-05-10)
 *    #428 na_empty=NA support
 */
SEXP ci_flatten_noressep(SEXP str, int na_empty)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    R_len_t str_length = LENGTH(str);
    if (str_length <= 0) {
        STRI__ERROR_HANDLER_BEGIN(1)
        charport::charvec::Store output = ci::scalar_store(
            "", 0, cetype_ext_t::CE_ASCII
        );
        SEXP ret;
        STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
        STRI__UNPROTECT_ALL
        return ret;
        STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
    }

    STRI__ERROR_HANDLER_BEGIN(1)
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    charport::charvec::Store output(0, 0);
    {
        StriContainerUTF8 str_cont(context, str, str_length);

        // 1. Get required buffer size
        size_t nchar = 0;
        bool has_na = false;
        for (int i=0; i<str_length; ++i) {
            if (str_cont.isNA(i)) {
                if (na_empty == NA_LOGICAL || na_empty) {
                    nchar += 0; // ignore
                }
                else {
                    has_na = true;
                    break;
                }
            }
            else {
                nchar += str_cont.get(i).length();
            }
        }

        if (has_na) {
            output = charport::charvec::Store::scalar(
                NULL, 0, cetype_ext_t::CE_NA
            );
        }
        else {
            // 2. Fill the buf!
            if (nchar > POW_2_31_M_1)
                throw StriException(MSG__CHARSXP_2147483647);
            String8buf buf(nchar);
            size_t cur = 0;
            for (int i=0; i<str_length; ++i) {
                if (!str_cont.isNA(i)) {
                    size_t ncur = str_cont.get(i).length();
                    memcpy(buf.data()+cur, str_cont.get(i).data(), (size_t)ncur);
                    cur += ncur;
                }
            }

            output = ci::scalar_store(
                buf.data(), cur,
                cetype_ext_t::CE_ASCII_OR_UTF8
            );
        }
    }

    // 3. Get ret val & good bye
    SEXP ret;
    STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/** String vector flatten, with separator between each string
 *
 *  if any of str is NA, the result will be NA_character_
 *
 *  @param str character vector
 *  @param collapse a single string
 *  @return if s is not empty, then a character vector of length 1
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Bartek Tartanus)
 *          collapse arg added (1 sep supported)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          StriContainerUTF8 - any R Encoding
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException friendly
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-18)
 *          Call ci_flatten_noressep if needed
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.2.1 (Marek Gagolewski, 2018-04-20)
 *    na_empty, omit_empty arg added
 *
 * @version 1.6.2 (Marek Gagolewski, 2021-05-10)
 *    #428 na_empty=NA support
 *
 */
SEXP ci_flatten(SEXP str, SEXP collapse, SEXP na_empty, SEXP omit_empty) // a.k.a. C_ci_flatten_withressep
{
    PROTECT(collapse = ci__prepare_arg_string_1(collapse, "collapse"));
    int na_empty_1 = ci__prepare_arg_logical_1_NA(na_empty, "na_empty");
    bool omit_empty_1 = ci__prepare_arg_logical_1_notNA(omit_empty, "omit_empty");

    ScalarStringInfo collapse_info = {false, false};
    ci__inspect_scalar_string(collapse, collapse_info);
    if (collapse_info.is_na) {
        STRI__ERROR_HANDLER_BEGIN(1)
        charport::charvec::Store output =
            charport::charvec::Store::scalar(
                NULL, 0, cetype_ext_t::CE_NA
            );
        SEXP ret;
        STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
        STRI__UNPROTECT_ALL
        return ret;
        STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
    }

    // if collapse is an empty string, we may use the following
    // specialized function:
    if (collapse_info.is_empty) {
        UNPROTECT(1);
        return ci_flatten_noressep(str, na_empty_1);
    }

    PROTECT(str = ci__prepare_arg_string(str, "str")); // prepare string argument
    R_len_t str_length = LENGTH(str);
    if (str_length <= 0) {
        STRI__ERROR_HANDLER_BEGIN(2)
        charport::charvec::Store output = ci::scalar_store(
            "", 0, cetype_ext_t::CE_ASCII
        );
        SEXP ret;
        STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
        STRI__UNPROTECT_ALL
        return ret;
        STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
    }

    STRI__ERROR_HANDLER_BEGIN(2)
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    charport::charvec::Store output(0, 0);
    {
        StriContainerUTF8 str_cont(context, str, str_length);
        StriContainerUTF8 collapse_cont(context, collapse, 1);
        R_len_t collapse_nbytes = collapse_cont.get(0).length();
        const char* collapse_s = collapse_cont.get(0).data();


        // 1. Get required minimal buffer size
        size_t nbytes = 0;
        bool has_na = false;
        for (int i=0; i<str_length; ++i) {
            if (str_cont.isNA(i)) {
                if (na_empty_1 == NA_LOGICAL) {
                    nbytes += 0;  // do nothing
                } else if (na_empty_1) {
                    nbytes += ((i>0 && !omit_empty_1)?collapse_nbytes:0);
                }
                else {
                    has_na = true;
                    break;
                }
            }
            else {
                nbytes += str_cont.get(i).length() + ((i>0)?collapse_nbytes:0);
            }
        }


        if (has_na) {
            output = charport::charvec::Store::scalar(
                NULL, 0, cetype_ext_t::CE_NA
            );
        }
        else {
            // 2. Fill the buf!
            if (nbytes > POW_2_31_M_1)
                throw StriException(MSG__CHARSXP_2147483647);
            String8buf buf(nbytes);
            size_t cur = 0;
            bool already_started = false;
            for (int i=0; i<str_length; ++i) {
                if (na_empty_1 == NA_LOGICAL && str_cont.isNA(i))
                    continue;

                if (omit_empty_1 && (str_cont.isNA(i) || str_cont.get(i).length() == 0))
                    continue;

                if (already_started) {
                    if (collapse_nbytes > 0) {
                        memcpy(buf.data()+cur, collapse_s, (size_t)collapse_nbytes);
                        cur += collapse_nbytes;
                    }
                }
                else
                    already_started = true;

                if (!str_cont.isNA(i)) {
                    size_t ncur = str_cont.get(i).length();
                    memcpy(buf.data()+cur, str_cont.get(i).data(), (size_t)ncur);
                    cur += ncur;
                }
            }

            output = ci::scalar_store(
                buf.data(), cur,
                cetype_ext_t::CE_ASCII_OR_UTF8
            );
        }
    }

    // 3. Get ret val & return
    SEXP ret;
    STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/**
 * Concatenate strings in a list
 *
 * @param x list of character vectors
 * @param sep single string
 * @param collapse single string or NULL
 * @return character vector
 *
 * @version 1.0-3 (Marek Gagolewski, 2016-02-07)
 *    FR#175
 */
SEXP ci_join_list(SEXP x, SEXP sep, SEXP collapse)
{
    PROTECT(x = ci__prepare_arg_list_ignore_null(
                    ci__prepare_arg_list_string(x, "x"), true
                ));

    R_len_t strlist_length = LENGTH(x);
    if (strlist_length <= 0) {
        STRI__ERROR_HANDLER_BEGIN(1)
        charport::charvec::Builder builder(0);
        SEXP ret;
        STRI__PROTECT(ret = builder.to_sexp());
        STRI__UNPROTECT_ALL
        return ret;
        STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
    }

    PROTECT(sep = ci__prepare_arg_string_1(sep, "sep"));
    if (Rf_isNull(collapse))
        PROTECT(collapse);
    else
        PROTECT(collapse = ci__prepare_arg_string_1(collapse, "collapse"));

    STRI__ERROR_HANDLER_BEGIN(3)

    // Deviation from stringi: run the nested flatten entry points before
    // opening output Readers or a Builder. A protected list keeps their lazy
    // scalar results alive without materializing them as CHARSXPs.
    SEXP flattened;
    STRI__PROTECT(flattened = charport::unwind_protect([&]() -> SEXP {
        return Rf_allocVector(VECSXP, strlist_length);
    }));
    for (R_len_t j=0; j<strlist_length; ++j) {
        SEXP ret2;
        STRI__PROTECT(ret2 = charport::unwind_protect([&]() -> SEXP {
            return ci_flatten(VECTOR_ELT(x, j), sep);
        }));
        SET_VECTOR_ELT(flattened, j, ret2);
        STRI__UNPROTECT(1);
    }

    charport::charvec::Builder builder(strlist_length);
    for (R_len_t j=0; j<strlist_length; ++j) {
        SEXP ret2 = VECTOR_ELT(flattened, j);
        {
            ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
            {
                StriContainerUTF8 ret2_cont(context, ret2, 1);
                ci::builder_set(builder, j, ret2_cont.getNAble(0));
            }
        }
    }
    SEXP ret;
    STRI__PROTECT(ret = builder.to_sexp());
    if (!Rf_isNull(collapse)) {
        STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
            return ci_flatten(ret, collapse);
        }));
    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;

    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}
