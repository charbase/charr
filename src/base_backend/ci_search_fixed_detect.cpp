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
#include "fixed/pattern_set.h"

#include <cstring>


namespace charr { namespace base_backend {

/**
 * Detect if a pattern occurs in a string [fast but dummy bitewise compare]
 *
 * @param str character vector
 * @param pattern character vector
 * @param negate single bool
 * @param max_count single int
 * @return logical vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *    use a UTF-8 input adapter; BUGFIX: the loop could go too far
 *
 * @version 0.1-?? (Marek Gagolewski)
 *    corrected behavior on empty str/pattern
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-23)
 *    make StriException-friendly, use fixed::PatternSet
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          ci_detect_fixed now uses byte search only
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-07)
 *    FR #110, #23: opts_fixed arg added
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-02-14)
 *    use shared::ByteSearchMatcher
 *
 * @version 1.0-3 (Marek Gagolewski, 2016-02-03)
 *    #216: `negate` arg added
 *
 * @version 1.3.1 (Marek Gagolewski, 2019-02-08)
 *    #232: `max_count` arg added
 */
SEXP ci_detect_fixed(SEXP str, SEXP pattern, SEXP negate,
                       SEXP max_count, SEXP opts_fixed)
{
    bool negate_1 = ci__prepare_arg_logical_1_notNA(negate, "negate");
    int max_count_1 = ci__prepare_arg_integer_1_notNA(max_count, "max_count");
    uint32_t pattern_flags = fixed::PatternSet::getByteSearchFlags(opts_fixed);
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));

    STRI__ERROR_HANDLER_BEGIN(2)
    int vectorize_length = ci__recycling_rule(true, 2, LENGTH(str), LENGTH(pattern));

    SEXP ret;
    STRI__PROTECT(ret = Rf_allocVector(LGLSXP, vectorize_length));
    int* ret_tab = LOGICAL(ret);
    R_len_t general_start = 0;

    // A scalar ASCII byte needs no encoding conversion or search object.
    // Other encodings and fixed-search options retain the general path below.
    if (pattern_flags == 0 && LENGTH(pattern) == 1) {
        SEXP pattern_elt = STRING_ELT(pattern, 0);
        if (pattern_elt != NA_STRING && LENGTH(pattern_elt) == 1 &&
                (IS_ASCII(pattern_elt) || IS_UTF8(pattern_elt)) &&
                static_cast<unsigned char>(CHAR(pattern_elt)[0]) <= 0x7f) {
            const unsigned char pattern_byte = static_cast<unsigned char>(
                CHAR(pattern_elt)[0]
            );
            bool direct = true;

            for (R_len_t i = 0; i < vectorize_length; ++i) {
                SEXP str_elt = STRING_ELT(str, i);
                if (str_elt != NA_STRING &&
                        !IS_ASCII(str_elt) && !IS_UTF8(str_elt)) {
                    direct = false;
                    general_start = i;
                    break;
                }

                if (max_count_1 == 0) {
                    ret_tab[i] = NA_LOGICAL;
                    continue;
                }
                if (str_elt == NA_STRING) {
                    ret_tab[i] = NA_LOGICAL;
                    continue;
                }

                const R_len_t str_length = LENGTH(str_elt);
                const bool found = str_length > 0 && std::memchr(
                    CHAR(str_elt), pattern_byte,
                    static_cast<std::size_t>(str_length)
                ) != NULL;
                ret_tab[i] = negate_1 ? !found : found;
                if (max_count_1 > 0 && ret_tab[i])
                    --max_count_1;
            }

            if (direct) {
                STRI__UNPROTECT_ALL
                return ret;
            }
        }
    }

    io::Utf8Input str_cont(str, vectorize_length);
    fixed::PatternSet pattern_cont(pattern, vectorize_length, pattern_flags);

    // general_start is set only by the scalar-pattern path, so the pattern
    // container advances with a unit stride from that index.
    for (R_len_t i = general_start > 0 ?
                general_start : pattern_cont.vectorize_init();
            i != pattern_cont.vectorize_end();
            i = pattern_cont.vectorize_next(i))
    {
        if (max_count_1 == 0) {
            ret_tab[i] = NA_LOGICAL;
            continue;
        }

        STRI__CONTINUE_ON_EMPTY_OR_NA_STR_PATTERN(str_cont, pattern_cont,
                ret_tab[i] = NA_LOGICAL,
        {   ret_tab[i] = negate_1;
            if (max_count_1 > 0 && ret_tab[i]) --max_count_1;
        })

        shared::ByteSearchMatcher* matcher = pattern_cont.getMatcher(i);
        matcher->reset(str_cont.get(i).data(), str_cont.get(i).length());
        ret_tab[i] = (int)(matcher->find_first() != shared::ByteSearchMatcher::not_found);
        if (negate_1) ret_tab[i] = !ret_tab[i];
        if (max_count_1 > 0 && ret_tab[i]) --max_count_1;
    }

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END( ;/* do nothing special on error */ )


//   Version 2 -- slower for long strings
//   UText *uts = NULL;
//   UText *utp = NULL;
//   URegularExpression* matcher = NULL;
//
//   STRI__ERROR_HANDLER_BEGIN
//   int vectorize_length = ci__recycling_rule(true, 2, LENGTH(str), LENGTH(pattern));
//   io::Utf8Input str_cont(str, vectorize_length);
//   fixed::PatternSet pattern_cont(pattern, vectorize_length);
//
//   SEXP ret;
//   PROTECT(ret = Rf_allocVector(LGLSXP, vectorize_length));
//   int* ret_tab = LOGICAL(ret);
//
//
//   const io::Utf8Record* last_s = NULL;
//   const io::Utf8Record* last_p = NULL;
//   UErrorCode err = U_ZERO_ERROR;
//
//   for (R_len_t i = pattern_cont.vectorize_init();
//         i != pattern_cont.vectorize_end();
//         i = pattern_cont.vectorize_next(i))
//   {
//      STRI__CONTINUE_ON_EMPTY_OR_NA_STR_PATTERN(str_cont, pattern_cont,
//         ret_tab[i] = NA_LOGICAL,
//         ret_tab[i] = FALSE)
//
//      const io::Utf8Record* cur_s = &(str_cont.get(i));
//      const io::Utf8Record* cur_p = &(pattern_cont.get(i));
//
//      if (last_p != cur_p) {
//         last_p = cur_p;
//         if (matcher) uregex_close(matcher);
//         utp = utext_openUTF8(utp, last_p->c_str(), last_p->length(), &err);
//         matcher = uregex_openUText(utp, UREGEX_LITERAL, NULL, &err);
//         STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
//      }
//
//      if (last_s != cur_s) {
//         last_s = cur_s;
//         uts = utext_openUTF8(uts, last_s->c_str(), last_s->length(), &err);
//      }
//
//      uregex_setUText(matcher, uts, &err);
//      uregex_reset(matcher, 0, &err);
//      int found = (int)uregex_find(matcher, -1, &err);
//      if (U_FAILURE(err))
//         throw StriException(MSG__REGEX_FAILED);
//      LOGICAL(ret)[i] = found;
//   }
//
//   if (matcher) { uregex_close(matcher); matcher=NULL; }
//   if (uts) { utext_close(uts); uts=NULL; }
//   if (utp) { utext_close(utp); utp=NULL; }
//   UNPROTECT(1);
//   return ret;
//   STRI__ERROR_HANDLER_END({
//      if (matcher) { uregex_close(matcher); matcher=NULL; }
//      if (uts) { utext_close(uts); uts=NULL; }
//      if (utp) { utext_close(utp); utp=NULL; }
//   })
}

} } // namespace charr::base_backend
