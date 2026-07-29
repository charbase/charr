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
#include "io/utf16_input.h"
#include <unicode/normalizer2.h>
namespace charr { namespace base_backend {



#define STRI_UNINORM_NFC 10
#define STRI_UNINORM_NFD 20
#define STRI_UNINORM_NFKC 11
#define STRI_UNINORM_NFKD 21
#define STRI_UNINORM_NFKC_CF 12

/** Get Desired Normalizer2 instance
 *
 * @param type R object, will be tested whether it's an integer vector of length 1
 * @return unmodifiable singleton instance. Do not delete it.
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-29)
 *          don't use getNFCInstance as it's in ICU DRAFT API
 *
 * @version 0.2-1  (Marek Gagolewski, 2014-03-23)
 *          getNFCInstance is stable as of ICU 49 and we require ICU >= 50
 */
const Normalizer2* ci__normalizer_get(int _type)
{
    UErrorCode status = U_ZERO_ERROR;
    const Normalizer2* normalizer = NULL;

    switch (_type) {
    case STRI_UNINORM_NFC:
//         normalizer = Normalizer2::getInstance(NULL, "nfc", UNORM2_COMPOSE, status);
        normalizer = Normalizer2::getNFCInstance(status);
        break;

    case STRI_UNINORM_NFD:
//         normalizer = Normalizer2::getInstance(NULL, "nfc", UNORM2_DECOMPOSE, status);
        normalizer = Normalizer2::getNFDInstance(status);
        break;

    case STRI_UNINORM_NFKC:
//         normalizer = Normalizer2::getInstance(NULL, "nfkc", UNORM2_COMPOSE, status);
        normalizer = Normalizer2::getNFKCInstance(status);
        break;

    case STRI_UNINORM_NFKD:
//         normalizer = Normalizer2::getInstance(NULL, "nfkc", UNORM2_DECOMPOSE, status);
        normalizer = Normalizer2::getNFKDInstance(status);
        break;

    case STRI_UNINORM_NFKC_CF:
//         normalizer = Normalizer2::getInstance(NULL, "nfkc_cf", UNORM2_COMPOSE, status);
        normalizer = Normalizer2::getNFKCCasefoldInstance(status);
        break;

    default:
        Rf_error(MSG__INCORRECT_INTERNAL_ARG); // error() allowed here
    }

    STRI__CHECKICUSTATUS_RFERROR(status, {/* do nothing special on err */})  /* Rf_error */

    return normalizer;
}


/**
 * Perform Unicode Normalization
 *
 * @param str character vector
 * @param type normalization type [internal]
 * @return character vector
 *
 * @version 0.1 (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          use io::Utf16Input & ICU facilities
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException-friendly
 *
 * @version 0.2-2 (Marek Gagolewski, 2014-04-19)
 *          renamed: ci_enc_nf -> ci_trans_nf
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.6-1 (Marek Gagolewski, 2015-07-11)
 *    This is now an internal function
 */
SEXP ci_trans_nf(SEXP str, int type)
{
    // As of ICU 52.1 (Unicode 6.3.0), the "most expansive" decomposition
    // is 1 UChar -> 18 UChars (data/unidata/norm2/nfkc.txt)
    // FDFA>0635 0644 0649 0020 0627 0644 0644 0647 0020
    //      0639 0644 064A 0647 0020 0648 0633 0644 0645

    // C API will not be faster here
    // In ICU 52.1 unorm2_normalize does UnicodeString destString(dest, 0, capacity);
    // and so on, thus it is a simple wrapper for C++ API

    const Normalizer2* normalizer =
        ci__normalizer_get(type); // auto `type` check here, call before ERROR_HANDLER

    PROTECT(str = ci__prepare_arg_string(str, "str"));    // prepare string argument
    R_len_t str_length = LENGTH(str);

    STRI__ERROR_HANDLER_BEGIN(1)
    io::Utf16Output str_cont(str, str_length); // writable, no recycle

    for (R_len_t i=0; i<str_length; ++i) {
        if (str_cont.isNA(i)) continue;
        UErrorCode status = U_ZERO_ERROR;
        str_cont.set(i, normalizer->normalize(str_cont.get(i), status));
        STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
    }

    // normalizer shall not be deleted at all
    STRI__UNPROTECT_ALL
    return str_cont.toR();
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/**
 * Check if String is in NFC
 *
 * @param str character vector
 * @return logical vector
 *
 * @version 0.6-1 (Marek Gagolewski, 2015-07-11)
 *    call ci_trans_nf
 */
SEXP ci_trans_nfc(SEXP str) {
    return ci_trans_nf(str, STRI_UNINORM_NFC);
}

} } // namespace charr::base_backend
