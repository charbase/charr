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
#include "ci_ucnv.h"


namespace charr { namespace base {

// The stringr surface does not expose stringi's mutable ICU-default converter.
// Charr resolves native encodings explicitly for each operation instead of
// changing process-global ICU state that may be shared with other packages.


/** Fetch information on an encoding
 *
 * @param enc either NULL or "" for default encoding,
 *        or one string with encoding name
 * @return R list object with many components (see R doc for details)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.2-1 (Marek Gagolewski)
 *          use StriUcnv; make StriException-friendly
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci_enc_info(SEXP enc)
{
    const char* selected_enc = ci__prepare_arg_enc(enc, "enc", true/*default ok*/); /* this is R_alloc'ed */

    STRI__ERROR_HANDLER_BEGIN(0)
    StriUcnv uconv_obj(selected_enc);
    //uconv_obj.setCallBackSubstitute(); // restore default callbacks (no warning)
    UConverter* uconv = uconv_obj.getConverter(false);
    UErrorCode status = U_ZERO_ERROR;

    // get the list of available standards
    vector<const char*> standards = StriUcnv::getStandards();
    R_len_t standards_n = (R_len_t)standards.size();

    // alloc output list
    SEXP vals;
    SEXP names;
    const int nval = standards_n+2+5;
    STRI__PROTECT(names = Rf_allocVector(STRSXP, nval));
    SET_STRING_ELT(names, 0, Rf_mkChar("Name.friendly"));
    SET_STRING_ELT(names, 1, Rf_mkChar("Name.ICU"));
    for (R_len_t i=0; i<standards_n; ++i) {
        if (standards[i])
            SET_STRING_ELT(names, i+2, Rf_mkChar((string("Name.")+standards[i]).c_str()));
    }
    SET_STRING_ELT(names, nval-5, Rf_mkChar("ASCII.subset"));
    SET_STRING_ELT(names, nval-4, Rf_mkChar("Unicode.1to1"));
    SET_STRING_ELT(names, nval-3, Rf_mkChar("CharSize.8bit"));
    SET_STRING_ELT(names, nval-2, Rf_mkChar("CharSize.min"));
    SET_STRING_ELT(names, nval-1, Rf_mkChar("CharSize.max"));

    STRI__PROTECT(vals = Rf_allocVector(VECSXP, nval));


    // get canonical (ICU) name
    status = U_ZERO_ERROR;
    const char* canname = ucnv_getName(uconv, &status);
    if (U_FAILURE(status) || !canname) {
        SET_VECTOR_ELT(vals, 1, Rf_ScalarString(NA_STRING));
        r_warning(MSG__ENC_ERROR_GETNAME);
    }
    else {
        SET_VECTOR_ELT(vals, 1, ci__make_character_vector_char_ptr(1, canname));

        // friendly name
        const char* frname = StriUcnv::getFriendlyName(canname);
        if (frname)  SET_VECTOR_ELT(vals, 0, ci__make_character_vector_char_ptr(1, frname));
        else         SET_VECTOR_ELT(vals, 0, Rf_ScalarString(NA_STRING));

        // has ASCII as its subset?
        SET_VECTOR_ELT(vals, nval-5, Rf_ScalarLogical((int)uconv_obj.hasASCIIsubset()));

        // min,max character size, is 8bit?
        int mincharsize = (int)ucnv_getMinCharSize(uconv);
        int maxcharsize = (int)ucnv_getMaxCharSize(uconv);
        int is8bit = (mincharsize==1 && maxcharsize == 1);
        SET_VECTOR_ELT(vals, nval-3, Rf_ScalarLogical(is8bit));
        SET_VECTOR_ELT(vals, nval-2, Rf_ScalarInteger(mincharsize));
        SET_VECTOR_ELT(vals, nval-1, Rf_ScalarInteger(maxcharsize));

        // is there a one-to-one correspondence with Unicode?
        if (!is8bit)
            SET_VECTOR_ELT(vals, nval-4, Rf_ScalarLogical(NA_LOGICAL));
        else
            SET_VECTOR_ELT(vals, nval-4, Rf_ScalarLogical((int)uconv_obj.is1to1Unicode()));

        // other standard names
        for (R_len_t i=0; i<standards_n; ++i) {
            if (!standards[i]) continue;

            status = U_ZERO_ERROR;
            const char* stdname = ucnv_getStandardName(canname, standards[i], &status);
            if (U_FAILURE(status) || !stdname)
                SET_VECTOR_ELT(vals, i+2, Rf_ScalarString(NA_STRING));
            else
                SET_VECTOR_ELT(vals, i+2, ci__make_character_vector_char_ptr(1, stdname));
        }
    }
    Rf_setAttrib(vals, R_NamesSymbol, names);
    STRI__UNPROTECT_ALL
    return vals;

    STRI__ERROR_HANDLER_END({/* no special action on error */})
}


/** Get Declared Encodings of Each String
 *
 * @param str a character vector or an object coercible to
 * @return a character vector
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-25)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci_enc_mark(SEXP str) {
    PROTECT(str = ci__prepare_arg_string(str, "str"));    // prepare string argument

    STRI__ERROR_HANDLER_BEGIN(1)
    R_len_t str_len = LENGTH(str);

    // some of them will not be used in this call, but we're lazy
    SEXP mark_ascii, mark_latin1, mark_utf8, mark_native, mark_bytes;
    STRI__PROTECT(mark_ascii  = Rf_mkChar("ASCII"));
    STRI__PROTECT(mark_latin1 = Rf_mkChar("latin1"));
    STRI__PROTECT(mark_utf8   = Rf_mkChar("UTF-8"));
    STRI__PROTECT(mark_native = Rf_mkChar("native"));
    STRI__PROTECT(mark_bytes  = Rf_mkChar("bytes"));

    SEXP ret;
    STRI__PROTECT(ret = Rf_allocVector(STRSXP, str_len));

    for (R_len_t i=0; i<str_len; ++i) {
        SEXP curs = STRING_ELT(str, i);
        if (curs == NA_STRING) {
            SET_STRING_ELT(ret, i, NA_STRING);
            continue;
        }

        if (IS_ASCII(curs))
            SET_STRING_ELT(ret, i, mark_ascii);
        else if (IS_UTF8(curs))
            SET_STRING_ELT(ret, i, mark_utf8);
        else if (IS_BYTES(curs))
            SET_STRING_ELT(ret, i, mark_bytes);
        else if (IS_LATIN1(curs))
            SET_STRING_ELT(ret, i, mark_latin1);
        else
            SET_STRING_ELT(ret, i, mark_native);
    }

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}

} } // namespace charr::base
