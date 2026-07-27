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
#include "ci_container_utf16.h"
#include "ci_container_listraw.h"
#include "ci_container_listint.h"
#include "ci_string8buf.h"
#include "ci_ucnv.h"
#include "native_to_utf8.h"
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>


#define BUF_MAX_LENGTH 2147483647


namespace charr { namespace base {

namespace {

bool ci__utf8_identity_name(const char* encoding)
{
    return encoding != NULL &&
        ucnv_compareNames(encoding, "UTF-8") == 0;
}


bool ci__valid_utf8_bytes(
    const char* data, R_len_t length, bool& ascii, bool& embedded_nul
)
{
    ascii = true;
    embedded_nul = false;
    R_len_t i = 0;
    while (i < length) {
        const uint8_t byte = static_cast<uint8_t>(data[i]);
        if (byte == 0)
            embedded_nul = true;
        if (byte <= 0x7fU) {
            ++i;
            continue;
        }

        ascii = false;
        UChar32 code_point = 0;
        U8_NEXT(data, i, length, code_point);
        if (code_point < 0)
            return false;
    }
    return true;
}


bool ci__plain_utf8_identity(SEXP input, bool respect_marks)
{
    const R_len_t n = LENGTH(input);
    const SEXP* values = n > 0 ? STRING_PTR_RO(input) : nullptr;
    for (R_len_t i=0; i<n; ++i) {
        SEXP value = values[i];
        if (value == NA_STRING)
            continue;
        if (IS_ASCII(value))
            continue;
        if (respect_marks && !IS_UTF8(value))
            return false;

        bool ascii = false;
        bool embedded_nul = false;
        if (!ci__valid_utf8_bytes(
                CHAR(value), LENGTH(value), ascii, embedded_nul
            ) || embedded_nul) {
            return false;
        }
    }
    return true;
}


size_t ci__transcode_direct(
    UConverter* source_converter, UConverter* target_converter,
    const char* input, R_len_t input_length, String8buf& output
)
{
    const size_t maximum = static_cast<size_t>(BUF_MAX_LENGTH);
    const size_t max_char_size = static_cast<size_t>(
        ucnv_getMaxCharSize(target_converter)
    );
    size_t capacity = 64;
    if (input_length > 0) {
        const size_t input_size = static_cast<size_t>(input_length);
        capacity = input_size > (maximum-1)/max_char_size
            ? maximum-1
            : input_size*max_char_size;
        if (capacity < 64)
            capacity = 64;
    }
    output.resize(capacity-1, false);

    const char empty[] = "";
    const char* source = input_length == 0 && input == nullptr ? empty : input;
    const char* source_limit = source+input_length;
    UChar pivot[1024];
    UChar* pivot_source = pivot;
    UChar* pivot_target = pivot;
    bool reset = true;
    size_t used = 0;

    for (;;) {
        char* target = output.data()+used;
        const char* target_limit = output.data()+output.size();
        UErrorCode status = U_ZERO_ERROR;
        ucnv_convertEx(
            target_converter, source_converter,
            &target, target_limit, &source, source_limit,
            pivot, &pivot_source, &pivot_target, pivot+1024,
            reset, true, &status
        );
        used = static_cast<size_t>(target-output.data());
        if (status != U_BUFFER_OVERFLOW_ERROR) {
            STRI__CHECKICUSTATUS_THROW(status, {})
            return used;
        }

        const size_t old_capacity = output.size();
        if (old_capacity >= maximum)
            throw StriException(MSG__BUF_SIZE_EXCEEDED);
        const size_t new_capacity = old_capacity > maximum/2
            ? maximum
            : old_capacity*2;
        output.resize(new_capacity-1, true);
        // A target overflow leaves converter and pivot state mid-record.
        // Continue that record after growing the buffer; the next record's
        // first call uses reset=true again.
        reset = false;
    }
}

} // namespace

/** Convert from UTF-32
 *
 * @param vec integer vector or list with integer vectors
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-25)
 *          StriException friently;
 *          use StriContainerListInt
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci_enc_fromutf32(SEXP vec)
{
    PROTECT(vec = ci__prepare_arg_list_integer(vec, "vec"));

    STRI__ERROR_HANDLER_BEGIN(1)
    StriContainerListInt vec_cont(vec);
    R_len_t vec_n = vec_cont.get_n();

    // get required buf size
    R_len_t bufsize = 0;
    for (R_len_t i=0; i<vec_n; ++i) {
        if (!vec_cont.isNA(i) && vec_cont.get(i).size() > bufsize)
            bufsize = vec_cont.get(i).size();
    }
    bufsize = U8_MAX_LENGTH*bufsize+1; // this will surely be sufficient
    String8buf buf(bufsize);
    char* bufdata = buf.data();

    SEXP ret;
    STRI__PROTECT(ret = Rf_allocVector(STRSXP, vec_n));

    for (R_len_t i=0; i<vec_n; ++i) {
        if (vec_cont.isNA(i)) {
            SET_STRING_ELT(ret, i, NA_STRING);
            continue;
        }

        const int* cur_data = vec_cont.get(i).data();
        R_len_t    cur_n    = vec_cont.get(i).size();
        UChar32 c = (UChar32)0;
        R_len_t j = 0;
        R_len_t k = 0;
        UBool err = FALSE;
        while (!err && k < cur_n) {
            c = cur_data[k++];
            U8_APPEND((uint8_t*)bufdata, j, bufsize, c, err);

            // Rf_mkCharLenCE detects embedded nuls, but stops execution completely
            if (c == 0) err = TRUE;
        }

        if (err) {
            r_warning(MSG__INVALID_CODE_POINT, (int)c);
            SET_STRING_ELT(ret, i, NA_STRING);
        }
        else
            SET_STRING_ELT(ret, i, Rf_mkCharLenCE(bufdata, j, CE_UTF8));
    }

    STRI__UNPROTECT_ALL;
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/** Convert character vector to UTF-32
 *
 * @param str character vector
 * @return list with integer vectors
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException-friendly
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-26)
 *          use vector<UChar32> buf instead of R_alloc;
 *          warn and set NULL on improper UTF-8 byte sequences
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-12)
 *          Use UChar32* instead of vector<UChar32> as ::data is C++11
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci_enc_toutf32(SEXP str)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    R_len_t n = LENGTH(str);

    STRI__ERROR_HANDLER_BEGIN(1)
    Utf8Input str_cont(str, n);

    R_len_t bufsize = 1; // to avoid allocating an empty buffer
    for (R_len_t i=0; i<n; ++i) {
        if (str_cont.isNA(i)) continue;
        R_len_t ni = str_cont.get(i).length();
        if (ni > bufsize) bufsize = ni;
    }

    UChar32* buf = (UChar32*)R_alloc((size_t)bufsize, (int)sizeof(UChar32)); // at most bufsize UChars32 (bufsize/4 min.)
    STRI_ASSERT(buf);
    if (!buf) throw StriException(MSG__MEM_ALLOC_ERROR);
    // deque<UChar32> was slower than using a common, over-sized buf

    SEXP ret;
    STRI__PROTECT(ret = Rf_allocVector(VECSXP, n)); // all

    for (R_len_t i=0; i<n; ++i) {

        if (str_cont.isNA(i)) {
            SET_VECTOR_ELT(ret, i, R_NilValue);
            continue;
        }

        UChar32 c = (UChar32)0;
        const char* s = str_cont.get(i).data();
        R_len_t sn = str_cont.get(i).length();
        R_len_t j = 0;
        R_len_t k = 0;
        while (c >= 0 && j < sn) {
            U8_NEXT(s, j, sn, c);
            buf[k++] = (int)c;
        }

        if (c < 0) {
            throw StriException(MSG__INVALID_UTF8);
//             SET_VECTOR_ELT(ret, i, R_NilValue);
//             continue;
        }
        else {
            SEXP conv;
            STRI__PROTECT(conv = Rf_allocVector(INTSXP, k));
            memcpy(INTEGER(conv), buf, (size_t)sizeof(int)*k);
            SET_VECTOR_ELT(ret, i, conv);
            STRI__UNPROTECT(1);
        }
    }

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END({ /* do nothing on error */ })
}


/** Convert character vector to UTF-8
 *
 * @param str character vector
 * @param is_unknown_8bit single logical value;
 * if TRUE, then in case of ENC_NATIVE or ENC_LATIN1, UTF-8
 * REPLACEMENT CHARACTERs (U+FFFD) are
 * put for codes > 127
 * @param validate single logical value (or NA)
 *
 * @return character vector
 *
 * @version 0.1-XX (Marek Gagolewski)
 *
 * @version 0.1-XX (Marek Gagolewski, 2013-06-16)
 *                  make StriException-friendly
 *
 * @version 0.2-1  (Marek Gagolewski, 2014-03-26)
 *                 Use one String8buf;
 *                 is_unknown_8bit_logical and UTF-8 tries now to remove BOMs
 *
 * @version 0.2-1  (Marek Gagolewksi, 2014-03-30)
 *                 added validate arg
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci_enc_toutf8(SEXP str, SEXP is_unknown_8bit, SEXP validate)
{
    PROTECT(validate = ci__prepare_arg_logical_1(validate, "validate"));
    bool is_unknown_8bit_logical =
        ci__prepare_arg_logical_1_notNA(is_unknown_8bit, "is_unknown_8bit");
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    R_len_t n = LENGTH(str);

    STRI__ERROR_HANDLER_BEGIN(2)
    SEXP ret;
    if (!is_unknown_8bit_logical) {
        // The UTF-8 input adapter already performed the required conversion.
        // which removes BOMs silently
        Utf8Input str_cont(str, n);
        STRI__PROTECT(ret = str_cont.to_sexp());
    }
    else {
        // get buf size
        size_t bufsize = 0;
        for (R_len_t i=0; i<n; ++i) {
            SEXP curs = STRING_ELT(str, i);
            if (curs == NA_STRING || IS_ASCII(curs) || IS_UTF8(curs))
                continue;

            size_t ni = LENGTH(curs);
            if (ni > bufsize) bufsize = ni;
        }
        String8buf buf(bufsize*3); // either 1 byte < 127 or U+FFFD == 3 bytes UTF-8
        char* bufdata = buf.data();

        STRI__PROTECT(ret = Rf_allocVector(STRSXP, n));
        for (R_len_t i=0; i<n; ++i) {
            SEXP curs = STRING_ELT(str, i);
            if (curs == NA_STRING) {
                SET_STRING_ELT(ret, i, NA_STRING);
                continue;
            }

            if (IS_ASCII(curs) || IS_UTF8(curs)) {
                R_len_t curs_n = LENGTH(curs);
                const char* curs_s = CHAR(curs);  // TODO: ALTREP will be problematic?
                if (curs_n >= 3 &&
                        (uint8_t)(curs_s[0]) == UTF8_BOM_BYTE1 &&
                        (uint8_t)(curs_s[1]) == UTF8_BOM_BYTE2 &&
                        (uint8_t)(curs_s[2]) == UTF8_BOM_BYTE3) {
                    // has BOM - get rid of it
                    SET_STRING_ELT(ret, i, Rf_mkCharLenCE(curs_s+3, curs_n-3, CE_UTF8));
                }
                else
                    SET_STRING_ELT(ret, i, curs);

                continue;
            }

            // otherwise, we have an 8-bit encoding
            R_len_t curn = LENGTH(curs);
            const char* curs_tab = CHAR(curs);  // TODO: ALTREP will be problematic?
            R_len_t k = 0;
            for (R_len_t j=0; j<curn; ++j) {
                if (U8_IS_SINGLE(curs_tab[j]))
                    bufdata[k++] = curs_tab[j];
                else { // 0xEF 0xBF 0xBD
                    bufdata[k++] = (char)UCHAR_REPLACEMENT_UTF8_BYTE1;
                    bufdata[k++] = (char)UCHAR_REPLACEMENT_UTF8_BYTE2;
                    bufdata[k++] = (char)UCHAR_REPLACEMENT_UTF8_BYTE3;
                }
            }
            SET_STRING_ELT(ret, i, Rf_mkCharLenCE(bufdata, k, CE_UTF8));
        }

    }

    // validate utf8 byte stream
    if (LOGICAL(validate)[0] != FALSE) { // NA or TRUE
        R_len_t ret_n = LENGTH(ret);
        for (R_len_t i=0; i<ret_n; ++i) {
            SEXP curs = STRING_ELT(ret, i);
            if (curs == NA_STRING) continue;

            const char* s = CHAR(curs);  // TODO: ALTREP will be problematic?
            R_len_t sn = LENGTH(curs);
            R_len_t j = 0;
            UChar32 c = 0;
            while (c >= 0 && j < sn) {
                U8_NEXT(s, j, sn, c);
            }

            if (c >= 0) continue; // valid, nothing to do

            if (LOGICAL(validate)[0] == NA_LOGICAL) {
                r_warning(MSG__INVALID_CODE_POINT_REPLNA);
                SET_STRING_ELT(ret, i, NA_STRING);
            }
            else {
                size_t bufsize = sn*3; // maximum: 1 byte -> U+FFFD (3 bytes)
                String8buf buf(bufsize); // maximum: 1 byte -> U+FFFD (3 bytes)
                char* bufdata = buf.data();

                j = 0;
                size_t k = 0;
                UBool err = FALSE;
                while (!err && j < sn) {
                    U8_NEXT(s, j, sn, c);
                    if (c >= 0) {
                        U8_APPEND((uint8_t*)bufdata, k, bufsize, c, err);
                    } else {
                        r_warning(MSG__INVALID_CODE_POINT_FIXING);
                        bufdata[k++] = (char)UCHAR_REPLACEMENT_UTF8_BYTE1;
                        bufdata[k++] = (char)UCHAR_REPLACEMENT_UTF8_BYTE2;
                        bufdata[k++] = (char)UCHAR_REPLACEMENT_UTF8_BYTE3;
                    }
                }

                if (err) throw StriException(MSG__INTERNAL_ERROR);
                SET_STRING_ELT(ret, i, Rf_mkCharLenCE(bufdata, k, CE_UTF8));
            }
        }
    }

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/** Convert character vector to ASCII
 *
 * All charcodes > 127 are replaced with subst chars (0x1A)
 *
 * @param str character vector
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException-friendly
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-30)
 *          use single common buf;
 *          warn on invalid utf8 byte stream
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci_enc_toascii(SEXP str)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    R_len_t n = LENGTH(str);

    STRI__ERROR_HANDLER_BEGIN(1)

    // get buf size
    size_t bufsize = 0;
    for (R_len_t i=0; i<n; ++i) {
        SEXP curs = STRING_ELT(str, i);
        if (curs == NA_STRING)
            continue;

        size_t ni = LENGTH(curs);
        if (ni > bufsize) bufsize = ni;
    }
    String8buf buf(bufsize); // no more bytes than this needed
    char* bufdata = buf.data();

    SEXP ret;
    STRI__PROTECT(ret = Rf_allocVector(STRSXP, n));
    for (R_len_t i=0; i<n; ++i) {
        SEXP curs = STRING_ELT(str, i);
        if (curs == NA_STRING || IS_ASCII(curs)) {
            // nothing to do
            SET_STRING_ELT(ret, i, curs);
            continue;
        }

        R_len_t curn = LENGTH(curs);
        const char* curs_tab = CHAR(curs);  // TODO: ALTREP will be problematic?

        if (IS_UTF8(curs)) {
            R_len_t k = 0, j = 0;
            UChar32 c;
            while (j<curn) {
                U8_NEXT(curs_tab, j, curn, c);
                if (c < 0) {
                    r_warning(MSG__INVALID_CODE_POINT_FIXING);
                    bufdata[k++] = ASCII_SUBSTITUTE;
                }
                else if (c > ASCII_MAXCHARCODE)
                    bufdata[k++] = ASCII_SUBSTITUTE;
                else
                    bufdata[k++] = (char)c;
            }
            SET_STRING_ELT(ret, i, Rf_mkCharLenCE(bufdata, k, CE_UTF8));
            // the string will be marked as ASCII anyway by mkCharLenCE
        }
        else { // some 8-bit encoding
            R_len_t k = 0;
            for (R_len_t j=0; j<curn; ++j) {
                if (U8_IS_SINGLE(curs_tab[j]))
                    bufdata[k++] = curs_tab[j];
                else {
                    bufdata[k++] = (char)ASCII_SUBSTITUTE; // subst char in ascii
                }
            }
            SET_STRING_ELT(ret, i, Rf_mkCharLenCE(bufdata, k, CE_UTF8));
            // the string will be marked as ASCII anyway by mkCharLenCE
        }
    }

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


// ------------------------------------------------------------------------

/**
 * Convert character vector between marked encodings and the encoding provided
 *
 * @param str     input character vector
 * @param to    target encoding, \code{NULL} or \code{""} for default enc
 * @param to_raw single logical, should list of raw vectors be returned?
 * @return a converted character vector or list of raw vectors
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-11-12)
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-28)
 *     use StriUcnv
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-01)
 *     calc required buf size a priori
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *     #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.8.4.9001 (Marek Gagolewski, 2024-06-13)
 *     #512: Fixed PROTECT stack imbalance in `ci_encode_from_marked`.
 */
SEXP ci_encode_from_marked(SEXP str, SEXP to, SEXP to_raw)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    const char* selected_to   = ci__prepare_arg_enc(to, "to", true); /* this is R_alloc'ed */
    bool to_raw_logical = ci__prepare_arg_logical_1_notNA(to_raw, "to_raw");

    R_len_t str_n = LENGTH(str);
    if (str_n <= 0) {
        UNPROTECT(1);
        return Rf_allocVector(to_raw_logical?VECSXP:STRSXP, 0);
    }

    STRI__ERROR_HANDLER_BEGIN(1)

    if (!to_raw_logical && ci__utf8_identity_name(selected_to) &&
            ci__plain_utf8_identity(str, true)) {
        SEXP ret;
        STRI__PROTECT(ret = Rf_allocVector(STRSXP, str_n));
        for (R_len_t i=0; i<str_n; ++i)
            SET_STRING_ELT(ret, i, STRING_ELT(str, i));
        STRI__UNPROTECT_ALL
        return ret;
    }

    SEXP ret;
    {
    StriContainerUTF16 str_cont(str, str_n);

    // get the number of strings to convert; if == 0, then you know what's the result

    // Open converters
    StriUcnv ucnv(
        selected_to ? selected_to : "UTF-8", STRI__DEFERRED_WARNINGS
    );
    UConverter* uconv_to = ucnv.getConverter(true /*register_callbacks*/);
    // Staging is only needed where the native encoding differs from the UTF-8
    // side ICU sees. When it is UTF-8 the pass is an identity transcode.
    std::unique_ptr<NativeToUtf8> native_output;
    if (!selected_to) {
        native_output.reset(new NativeToUtf8());
        if (native_output->native_is_utf8())
            native_output.reset();
    }

    // Get target encoding mark
    cetype_t encmark_to = to_raw_logical
        ? CE_BYTES
        : (selected_to ? ucnv.getCE() : CE_NATIVE);

    // calculate required buf size
    size_t bufsize = 0;
    for (R_len_t i=0; i<str_n; ++i) {
        if (!str_cont.isNA(i) && (size_t)str_cont.get(i).length() > bufsize)
            bufsize = str_cont.get(i).length();
    }
    bufsize = UCNV_GET_MAX_BYTES_FOR_STRING(bufsize, ucnv_getMaxCharSize(uconv_to));
    // "The calculated size is guaranteed to be sufficient for this conversion."
    if (bufsize > BUF_MAX_LENGTH)
        bufsize = BUF_MAX_LENGTH;
    String8buf buf(bufsize);

    STRI__PROTECT(ret = unwind_protect([&]() -> SEXP {
        SEXP output = PROTECT(Rf_allocVector(
            to_raw_logical ? VECSXP : STRSXP, str_n
        ));
        try {
            for (R_len_t i=0; i<str_n; ++i) {
                if (str_cont.isNA(i)) {
                    if (to_raw_logical)
                        SET_VECTOR_ELT(output, i, R_NilValue);
                    else
                        SET_STRING_ELT(output, i, NA_STRING);
                    continue;
                }

                R_len_t curn_tmp = str_cont.get(i).length();
                const UChar* curs_tmp = str_cont.get(i).getBuffer();
                if (!curs_tmp)
                    throw StriException(MSG__INTERNAL_ERROR);

                UErrorCode status = U_ZERO_ERROR;
                ucnv_resetFromUnicode(uconv_to);
                size_t bufneed = ucnv_fromUChars(
                    uconv_to, buf.data(), buf.size(),
                    curs_tmp, curn_tmp, &status
                );
                if (bufneed <= buf.size()) {
                    STRI__CHECKICUSTATUS_THROW(
                        status, {/* do nothing special on err */}
                    )
                }
                else {
                    if (bufneed > BUF_MAX_LENGTH)
                        throw StriException(MSG__BUF_SIZE_EXCEEDED);
                    buf.resize(bufneed, false/*destroy contents*/);
                    status = U_ZERO_ERROR;
                    ucnv_resetFromUnicode(uconv_to);
                    bufneed = ucnv_fromUChars(
                        uconv_to, buf.data(), buf.size(),
                        curs_tmp, curn_tmp, &status
                    );
                    STRI__CHECKICUSTATUS_THROW(
                        status, {/* do nothing special on err */}
                    )
                }

                const char* output_data = buf.data();
                size_t output_length = bufneed;
                if (native_output) {
                    const ByteView converted =
                        native_output->utf8_to_native(
                            output_data,
                            static_cast<int>(output_length)
                        );
                    output_data = converted.ptr;
                    output_length =
                        static_cast<size_t>(converted.len);
                }

                if (to_raw_logical) {
                    SEXP outobj = PROTECT(
                        Rf_allocVector(RAWSXP, output_length)
                    );
                    memcpy(RAW(outobj), output_data, output_length);
                    SET_VECTOR_ELT(output, i, outobj);
                    UNPROTECT(1);
                }
                else {
                    SET_STRING_ELT(
                        output, i,
                        Rf_mkCharLenCE(
                            output_data,
                            static_cast<int>(output_length),
                            encmark_to
                        )
                    );
                }
            }
        }
        catch (...) {
            UNPROTECT(1);
            throw;
        }
        UNPROTECT(1);
        return output;
    }));
    }

    STRI__DEFERRED_WARNINGS.emit();

    STRI__UNPROTECT_ALL
    return ret;

    STRI__ERROR_HANDLER_END({/* nothing special on error */})
}


/**
 * Convert character vector between given encodings
 *
 * @param str     input character/raw vector or list of raw vectors
 * @param from  source encoding, \code{NULL} or \code{""} for default enc
 * @param to    target encoding, \code{NULL} or \code{""} for default enc
 * @param to_raw single logical, should list of raw vectors be returned?
 * @return a converted character vector or list of raw vectors
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          arg to_raw_added, encoding marking
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException-friendly
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-08)
 *          use StriContainerListRaw
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-11-20)
 *          BUGFIX call ci_encode_from_marked if necessary
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-28)
 *          use StriUcnv
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-01)
 *          estimate required buf size a priori
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci_encode(SEXP str, SEXP from, SEXP to, SEXP to_raw)
{
    const char* selected_from = ci__prepare_arg_enc(from, "from", true); /* this is R_alloc'ed */
    if (!selected_from && Rf_isVectorAtomic(str) && !isRaw(str))
        return ci_encode_from_marked(str, to, to_raw);
    const char* selected_to   = ci__prepare_arg_enc(to, "to", true); /* this is R_alloc'ed */
    bool to_raw_logical = ci__prepare_arg_logical_1_notNA(to_raw, "to_raw");

    // raw vector, character vector, or list of raw vectors:
    PROTECT(str = ci__prepare_arg_list_raw(str, "str"));


    STRI__ERROR_HANDLER_BEGIN(1)

    // Explicit UTF-8-to-UTF-8 conversion is an identity transform for every
    // well-formed record. Validate once, then reuse canonical CHARSXPs or copy
    // their bytes directly instead of routing through UTF-16 and two ICU
    // converters. Malformed input and embedded NULs retain the generic path.
    if (TYPEOF(str) == STRSXP &&
            ci__utf8_identity_name(selected_from) &&
            ci__utf8_identity_name(selected_to)) {
        const R_len_t n = LENGTH(str);
        if (ci__plain_utf8_identity(str, false)) {
            const SEXP* values = n > 0 ? STRING_PTR_RO(str) : nullptr;
            SEXP ret;
            STRI__PROTECT(ret = Rf_allocVector(
                to_raw_logical ? VECSXP : STRSXP, n
            ));
            for (R_len_t i=0; i<n; ++i) {
                SEXP value = values[i];
                if (value == NA_STRING) {
                    if (!to_raw_logical)
                        SET_STRING_ELT(ret, i, NA_STRING);
                    continue;
                }

                const char* data = CHAR(value);
                const R_len_t length = LENGTH(value);
                if (to_raw_logical) {
                    SEXP raw;
                    STRI__PROTECT(raw = Rf_allocVector(RAWSXP, length));
                    if (length > 0)
                        memcpy(RAW(raw), data, static_cast<size_t>(length));
                    SET_VECTOR_ELT(ret, i, raw);
                    STRI__UNPROTECT(1);
                }
                else if (IS_ASCII(value) || IS_UTF8(value)) {
                    SET_STRING_ELT(ret, i, value);
                }
                else {
                    SET_STRING_ELT(
                        ret, i, Rf_mkCharLenCE(data, length, CE_UTF8)
                    );
                }
            }

            STRI__UNPROTECT_ALL
            return ret;
        }
    }

    StriContainerListRaw str_cont(str);
    R_len_t str_n = str_cont.get_n();

    // get the number of strings to convert; if == 0, then you know what's the result
    if (str_n <= 0) {
        STRI__UNPROTECT_ALL
        return Rf_allocVector(to_raw_logical?VECSXP:STRSXP, 0);
    }

    // Optimized-backend deviation from stringi: R's native encoding follows
    // the current LC_CTYPE, whereas ICU's default converter may be fixed to
    // UTF-8 at build time. Decode default raw input with Riconv; explicit
    // source encodings continue through ICU below.
    //
    // A UTF-8 native encoding needs no staging pass, because the input bytes
    // already are the target encoding. Falling through to ICU is both cheaper
    // and closer to stringi: Riconv rejects malformed input and fails the
    // whole vector, while ICU's callbacks substitute per record and warn.
    std::unique_ptr<NativeToUtf8> raw_native;
    if (!selected_from && !to_raw_logical && selected_to &&
            ucnv_compareNames(selected_to, "UTF-8") == 0) {
        raw_native.reset(new NativeToUtf8());
        if (raw_native->native_is_utf8())
            raw_native.reset();
    }
    if (raw_native) {
        SEXP ret;
        STRI__PROTECT(ret = unwind_protect([&]() -> SEXP {
            SEXP output = PROTECT(Rf_allocVector(STRSXP, str_n));
            try {
                for (R_len_t i=0; i<str_n; ++i) {
                    if (str_cont.isNA(i)) {
                        SET_STRING_ELT(output, i, NA_STRING);
                        continue;
                    }

                    try {
                        const ByteView input = str_cont.get(i);
                        const ByteView converted = raw_native->native(
                            input.data(), input.length()
                        );
                        if (converted.len > 0 && std::memchr(
                                converted.ptr, 0, converted.len)) {
                            throw StriException(
                                "embedded nul in string"
                            );
                        }
                        SET_STRING_ELT(
                            output, i,
                            Rf_mkCharLenCE(
                                converted.ptr, converted.len, CE_UTF8
                            )
                        );
                    }
                    catch (const std::exception& error) {
                        throw StriException("%s", error.what());
                    }
                }
            }
            catch (...) {
                UNPROTECT(1);
                throw;
            }
            UNPROTECT(1);
            return output;
        }));

        STRI__UNPROTECT_ALL
        return ret;
    }

    SEXP ret;
    {
    // Default source and target names are resolved by Riconv. ICU only sees
    // the UTF-8 staging side of those conversions.
    StriUcnv ucnv1(
        selected_from ? selected_from : "UTF-8",
        STRI__DEFERRED_WARNINGS
    );
    StriUcnv ucnv2(
        selected_to ? selected_to : "UTF-8",
        STRI__DEFERRED_WARNINGS
    );
    UConverter* uconv_from = ucnv1.getConverter(true /*register_callbacks*/);
    UConverter* uconv_to   = ucnv2.getConverter(true /*register_callbacks*/);
    // Staging is only needed where the native encoding differs from the UTF-8
    // side ICU sees. When it is UTF-8 the pass is an identity transcode.
    std::unique_ptr<NativeToUtf8> native_input;
    if (!selected_from) {
        native_input.reset(new NativeToUtf8());
        if (native_input->native_is_utf8())
            native_input.reset();
    }
    std::unique_ptr<NativeToUtf8> native_output;
    if (!selected_to) {
        native_output.reset(new NativeToUtf8());
        if (native_output->native_is_utf8())
            native_output.reset();
    }

    // Get target encoding mark
    cetype_t encmark_to = to_raw_logical
        ? CE_BYTES
        : (selected_to ? ucnv2.getCE() : CE_NATIVE);

//   // estimate required buf size
//    size_t bufsize = 0;
//    for (R_len_t i=0; i<str_n; ++i) {
//       if (!str_cont.isNA(i) && (size_t)str_cont.get(i).length() > bufsize)
//          bufsize = str_cont.get(i).length();
//    }
//    bufsize = bufsize*4; // this is just an estimate (for 8bit->utf8 conversions)
//    String8buf buf(bufsize);
    String8buf buf(0);

    STRI__PROTECT(ret = unwind_protect([&]() -> SEXP {
        SEXP output = PROTECT(Rf_allocVector(
            to_raw_logical ? VECSXP : STRSXP, str_n
        ));
        try {
            for (R_len_t i=0; i<str_n; ++i) {
                if (str_cont.isNA(i)) {
                    if (to_raw_logical)
                        SET_VECTOR_ELT(output, i, R_NilValue);
                    else
                        SET_STRING_ELT(output, i, NA_STRING);
                    continue;
                }

                const ByteView input = str_cont.get(i);
                const char* curs = input.data();
                R_len_t curn = input.length();
                if (native_input) {
                    const ByteView converted =
                        native_input->native(curs, curn);
                    curs = converted.ptr;
                    curn = converted.len;
                }

                const size_t bufneed = ci__transcode_direct(
                    uconv_from, uconv_to, curs, curn, buf
                );
                const char* output_data = buf.data();
                size_t output_length = bufneed;
                if (native_output) {
                    const ByteView converted =
                        native_output->utf8_to_native(
                            output_data,
                            static_cast<int>(output_length)
                        );
                    output_data = converted.ptr;
                    output_length =
                        static_cast<size_t>(converted.len);
                }

                if (to_raw_logical) {
                    SEXP outobj = PROTECT(
                        Rf_allocVector(RAWSXP, output_length)
                    );
                    memcpy(RAW(outobj), output_data, output_length);
                    SET_VECTOR_ELT(output, i, outobj);
                    UNPROTECT(1);
                }
                else {
                    SET_STRING_ELT(
                        output, i,
                        Rf_mkCharLenCE(
                            output_data,
                            static_cast<int>(output_length),
                            encmark_to
                        )
                    );
                }
            }
        }
        catch (...) {
            UNPROTECT(1);
            throw;
        }
        UNPROTECT(1);
        return output;
    }));
    }

    STRI__DEFERRED_WARNINGS.emit();

    STRI__UNPROTECT_ALL
    return ret;

    STRI__ERROR_HANDLER_END({/* no special action on error */})
}

} } // namespace charr::base
