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
#include <unicode/ucsdet.h>
#include <unicode/locid.h>
#include <unicode/uloc.h>
#include <unicode/locid.h>
#include <unicode/ulocdata.h>
#include <unicode/uniset.h>
#include <map>
#include <vector>
#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include "ci_container_listraw.h"
#include "ci_container_logical.h"
#include "ci_builder.h"
#include "ci_ucnv.h"
using namespace std;


namespace {

struct CiCharsetDetectorCloser {
    void operator()(UCharsetDetector* detector) const noexcept
    {
        if (detector)
            ucsdet_close(detector);
    }
};


struct CiLocaleDataCloser {
    void operator()(ULocaleData* data) const noexcept
    {
        if (data)
            ulocdata_close(data);
    }
};


struct CiUSetCloser {
    void operator()(USet* set) const noexcept
    {
        if (set)
            uset_close(set);
    }
};


static int ci__encoding_icu_c_string_length(const char* value)
{
    // Deviation from stringi: these ICU APIs expose only terminated names.
    // Measure that boundary once, then keep all charr output length-delimited.
    const size_t length = std::strlen(value);
    if (length > static_cast<size_t>(std::numeric_limits<int>::max()))
        throw std::length_error("ICU encoding name exceeds R's string limit");
    return static_cast<int>(length);
}


static void ci__stage_encoding_icu_string(
    charport::charvec::Builder& output, R_len_t i, const char* value
)
{
    if (!value) {
        output.set_na(i);
        return;
    }
    // ICU charset and language identifiers use invariant ASCII bytes. Mark
    // them directly instead of asking the Builder to inspect the payload.
    ci::builder_set(
        output, i, value, ci__encoding_icu_c_string_length(value),
        cetype_ext_t::CE_ASCII
    );
}


struct CiEncodingDetectionResult {
    bool wrong;
    charport::charvec::Store encoding;
    charport::charvec::Store language;
    std::vector<double> confidence;

    CiEncodingDetectionResult()
        : wrong(true), encoding(0, 0), language(0, 0), confidence()
    {
    }

    CiEncodingDetectionResult(CiEncodingDetectionResult&&) noexcept = default;
    CiEncodingDetectionResult& operator=(CiEncodingDetectionResult&&) noexcept = default;

private:
    CiEncodingDetectionResult(const CiEncodingDetectionResult&);
    CiEncodingDetectionResult& operator=(const CiEncodingDetectionResult&);
};


static R_len_t ci__encoding_recycling_rule(
    ci::DeferredWarnings& warnings, R_len_t first, R_len_t second
)
{
    if (first <= 0 || second <= 0)
        return 0;

    const R_len_t vectorize_length = std::max(first, second);
    if (vectorize_length % first != 0 || vectorize_length % second != 0) {
        // Deviation from stringi: detect the copied recycling warning here,
        // but emit it only after the Reader and detector have been released.
        warnings.push(MSG__WARN_RECYCLING_RULE);
    }
    return vectorize_length;
}


static SEXP ci__encoding_detection_results_to_r(
    std::vector<CiEncodingDetectionResult>& results
)
{
    charport::charvec::Store wrong_encoding_store =
        charport::charvec::Store::scalar(
            NULL, 0, cetype_ext_t::CE_NA
        );
    charport::charvec::Store wrong_language_store =
        charport::charvec::Store::scalar(
            NULL, 0, cetype_ext_t::CE_NA
        );

    return charport::unwind_protect([&]() -> SEXP {
        int protected_count = 0;
        try {
            const R_len_t result_length = static_cast<R_len_t>(results.size());
            SEXP ret = PROTECT(Rf_allocVector(VECSXP, result_length));
            ++protected_count;
            SEXP names = PROTECT(Rf_allocVector(STRSXP, 3));
            ++protected_count;
            SET_STRING_ELT(names, 0, Rf_mkChar("Encoding"));
            SET_STRING_ELT(names, 1, Rf_mkChar("Language"));
            SET_STRING_ELT(names, 2, Rf_mkChar("Confidence"));

            SEXP wrong = PROTECT(Rf_allocVector(VECSXP, 3));
            ++protected_count;
            SEXP wrong_encoding = PROTECT(charport::charvec::wrap(
                std::move(wrong_encoding_store)
            ));
            ++protected_count;
            SEXP wrong_language = PROTECT(charport::charvec::wrap(
                std::move(wrong_language_store)
            ));
            ++protected_count;
            SEXP wrong_confidence = PROTECT(Rf_allocVector(INTSXP, 1));
            ++protected_count;
            INTEGER(wrong_confidence)[0] = NA_INTEGER;
            SET_VECTOR_ELT(wrong, 0, wrong_encoding);
            SET_VECTOR_ELT(wrong, 1, wrong_language);
            SET_VECTOR_ELT(wrong, 2, wrong_confidence);
            Rf_setAttrib(wrong, R_NamesSymbol, names);
            UNPROTECT(3);
            protected_count -= 3;

            for (R_len_t i=0; i<result_length; ++i) {
                CiEncodingDetectionResult& current =
                    results[static_cast<size_t>(i)];
                if (current.wrong) {
                    SET_VECTOR_ELT(ret, i, wrong);
                    continue;
                }

                SEXP val_enc = PROTECT(charport::charvec::wrap(
                    std::move(current.encoding)
                ));
                ++protected_count;
                SEXP val_lang = PROTECT(charport::charvec::wrap(
                    std::move(current.language)
                ));
                ++protected_count;
                const R_len_t matches_found = static_cast<R_len_t>(
                    current.confidence.size()
                );
                SEXP val_conf = PROTECT(Rf_allocVector(REALSXP, matches_found));
                ++protected_count;
                for (R_len_t j=0; j<matches_found; ++j) {
                    REAL(val_conf)[j] =
                        current.confidence[static_cast<size_t>(j)];
                }

                SEXP val = PROTECT(Rf_allocVector(VECSXP, 3));
                ++protected_count;
                SET_VECTOR_ELT(val, 0, val_enc);
                SET_VECTOR_ELT(val, 1, val_lang);
                SET_VECTOR_ELT(val, 2, val_conf);
                Rf_setAttrib(val, R_NamesSymbol, names);
                SET_VECTOR_ELT(ret, i, val);
                UNPROTECT(4);
                protected_count -= 4;
            }

            UNPROTECT(3);
            protected_count -= 3;
            return ret;
        }
        catch (...) {
            // Deviation from stringi: nested charvec wrapping translates an R
            // unwind into a C++ exception, so balance this helper's raw
            // protections before letting the operation-level handler run.
            UNPROTECT(protected_count);
            throw;
        }
    });
}

} // namespace


/** Check if a string may be valid 8-bit (including UTF-8) encoded
 *
 *  simple check whether all charcodes are nonzero
 *
 * @param str_cur_s character vector
 * @param str_cur_n number of bytes
 * @param get_confidence determine confidence value or do exact check
 *
 * @return confidence value in [0,1]
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-06)
 *          separate func
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-13)
 *          warnchars count added
 */
double ci__enc_check_8bit(const char* str_cur_s, R_len_t str_cur_n, bool get_confidence) {
    R_len_t warnchars = 0;
    for (R_len_t j=0; j < str_cur_n; ++j) {
        if (str_cur_s[j] == 0)
            return 0.0;
        if (get_confidence && (str_cur_s[j] <= 31 || str_cur_s[j] == 127)) {
            switch (str_cur_s[j]) {
            case 9:  // \t
            case 10: // \n
            case 13: // \r
            case 26: // ASCII SUBSTITUTE
                break; // ignore
            default:
                warnchars++;
            }
        }
    }
    return (get_confidence?(double)warnchars/double(str_cur_n):1.0);
}


/** Check if a string is valid ASCII
 *
 *  simple check whether charcodes are in [1..127]
 * by using U8_IS_SINGLE
 *
 * @param str_cur_s character vector
 * @param str_cur_n number of bytes
 * @param get_confidence determine confidence value or do exact check
 *
 * @return confidence value in [0,1]
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-06)
 *          separate func
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-13)
 *          warnchars count added
 */
double ci__enc_check_ascii(const char* str_cur_s, R_len_t str_cur_n, bool get_confidence) {
    R_len_t warnchars = 0;
    for (R_len_t j=0; j < str_cur_n; ++j) {
        if (!U8_IS_SINGLE(str_cur_s[j]) || str_cur_s[j] == 0) // i.e., 0 < c <= 127
            return 0.0;
        if (get_confidence && (str_cur_s[j] <= 31 || str_cur_s[j] == 127)) {
            switch (str_cur_s[j]) {
            case 9:  // \t
            case 10: // \n
            case 13: // \r
            case 26: // ASCII SUBSTITUTE
                break; // ignore
            default:
                warnchars++;
            }
        }
    }
    return (get_confidence?(double)(str_cur_n-warnchars)/double(str_cur_n):1.0);
}


/** Check if a string is valid UTF-8
 *
 * checks if a string is probably UTF-8-encoded;
 * simple check with U8_NEXT
 *
 *
 * @param str_cur_s character vector
 * @param str_cur_n number of bytes
 * @param get_confidence determine confidence value or do exact check
 *
 * @return confidence value in [0,1]
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-06)
 *          separate func
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-13)
 *          confidence calculation basing on ICU's i18n/csrutf8.cpp
 */
double ci__enc_check_utf8(const char* str_cur_s, R_len_t str_cur_n, bool get_confidence)
{
    if (!get_confidence) {
        UChar32 c;
        for (R_len_t j=0; j < str_cur_n; ) {
            if (str_cur_s[j] == 0)
                return 0.0; // definitely not valid UTF-8

            U8_NEXT(str_cur_s, j, str_cur_n, c);
            if (c < 0) // ICU utf8.h doc for U8_NEXT: c -> output UChar32 variable, set to <0 in case of an error
                return 0.0; // definitely not valid UTF-8
        }
        return 1.0;
    }
    else {
        // Based on ICU's i18n/csrutf8.cpp [with own mods]
        bool hasBOM = (str_cur_n >= 3 &&
                       (uint8_t)(str_cur_s[0]) == UTF8_BOM_BYTE1 &&
                       (uint8_t)(str_cur_s[1]) == UTF8_BOM_BYTE2 &&
                       (uint8_t)(str_cur_s[2]) == UTF8_BOM_BYTE3);
        R_len_t numValid = 0;   // counts only valid UTF-8 multibyte seqs
        R_len_t numInvalid = 0;

        // Scan for multi-byte sequences
        for (R_len_t i=0; i < str_cur_n; i += 1) {
            uint32_t b = str_cur_s[i];

            if ((b & 0x80) == 0) {
                continue;   // ASCII => OK
            }

            // Hi bit on char found.  Figure out how long the sequence should be
            R_len_t trailBytes = 0;
            if ((b & 0x0E0) == 0x0C0)
                trailBytes = 1;
            else if ((b & 0x0F0) == 0x0E0)
                trailBytes = 2;
            else if ((b & 0x0F8) == 0xF0)
                trailBytes = 3;
            else {
                numInvalid += 1;
                if (numInvalid > 5)
                    break; // that's enough => not UTF-8
                continue;
            }

            // Verify that we've got the right number of trail bytes in the sequence
            while (true) {
                i += 1;

                if (i >= str_cur_n)
                    break;

                b = str_cur_s[i];

                if ((b & 0xC0) != 0x080) {
                    numInvalid += 1;
                    break;
                }

                if (--trailBytes == 0) {
                    numValid += 1;
                    break;
                }
            }
        }

        // Cook up some sort of confidence score, based on BOM's presence
        //    and the existence of valid and/or invalid multi-byte sequences.
        if (hasBOM && numInvalid == 0)
            return 1.0;
        else if (hasBOM && numValid > numInvalid*10)
            return 0.75;
        else if (numValid > 3 && numInvalid == 0)
            return 1.0;
        else if (numValid > 0 && numInvalid == 0)
            return 0.50; // too few multibyte UTF-8 seqs to be quite sure
        else if (numValid == 0 && numInvalid == 0)
            // Plain ASCII. => It's OK for UTF-8
            return 0.50;
        else if (numValid > numInvalid*10)
            // Probably corrupt utf-8 data.  Valid sequences aren't likely by chance.
            return 0.25;
        else
            return 0.0;
    }
}


/** Check if a string is valid UTF-16LE or UTF-16BE
 *
 * @param str_cur_s character vector
 * @param str_cur_n number of bytes
 * @param get_confidence determine confidence value or do exact check
 * @param le check for UTF-16LE?
 *
 * @return confidence value in [0,1]
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-09)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-14)
 *          confidence calculation basing on ICU's i18n/csucode.cpp
 */
double ci__enc_check_utf16(const char* str_cur_s, R_len_t str_cur_n,
                             bool get_confidence, bool le)
{
    if (str_cur_n % 2 != 0)
        return 0.0;

    bool hasLE_BOM = STRI__ENC_HAS_BOM_UTF16LE(str_cur_s, str_cur_n);
    bool hasBE_BOM = STRI__ENC_HAS_BOM_UTF16BE(str_cur_s, str_cur_n);

    if ((!le && hasLE_BOM) || (le && hasBE_BOM))
        return 0.0;

    R_len_t warnchars = 0;

    for (R_len_t i=0; i<str_cur_n; i += 2) {
        uint16_t c = (le)?
                     STRI__GET_INT16_LE(str_cur_s, i):
                     STRI__GET_INT16_BE(str_cur_s, i);

        if (U16_IS_SINGLE(c)) {
            if (c == 0)
                return 0.0;
            else if (c >= 0x0530) // last cyrrilic supplement
                warnchars += 2;
            continue;
        }

        if (!U16_IS_SURROGATE_LEAD(c))
            return 0.0;

        i += 2;
        if (i >= str_cur_n)
            return 0.0;
        c = (le)?
            STRI__GET_INT16_LE(str_cur_s, i):
            STRI__GET_INT16_BE(str_cur_s, i);
        if (!U16_IS_SURROGATE_TRAIL(c))
            return 0.0;
    }

    return (get_confidence?(double)(str_cur_n-warnchars)/double(str_cur_n):1.0);
}


/** Check if a string is valid UTF-16BE
 *
 * @param str_cur_s character vector
 * @param str_cur_n number of bytes
 * @param get_confidence determine confidence value or to exact check
 *
 * @return confidence value in [0,1]
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-09)
 */
double ci__enc_check_utf16be(const char* str_cur_s, R_len_t str_cur_n, bool get_confidence)
{
    return ci__enc_check_utf16(str_cur_s, str_cur_n, get_confidence, false);
}


/** Check if a string is valid UTF-16LE
 *
 * @param str_cur_s character vector
 * @param str_cur_n number of bytes
 * @param get_confidence determine confidence value or do exact check
 *
 * @return confidence value in [0,1]
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-09)
 */
double ci__enc_check_utf16le(const char* str_cur_s, R_len_t str_cur_n, bool get_confidence)
{
    return ci__enc_check_utf16(str_cur_s, str_cur_n, get_confidence, true);
}


/** Check if a string is valid UTF-32LE or UTF-32BE
 *
 * @param str_cur_s character vector
 * @param str_cur_n number of bytes
 * @param get_confidence determine confidence value or do exact check
 * @param le check for UTF-32LE?
 *
 * @return confidence value in [0,1]
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-09)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-13)
 *          confidence calculation basing on ICU's i18n/csucode.cpp
 */
double ci__enc_check_utf32(const char* str_cur_s, R_len_t str_cur_n,
                             bool get_confidence, bool le)
{
    if (str_cur_n % 4 != 0)
        return 0.0;

    bool hasLE_BOM = STRI__ENC_HAS_BOM_UTF32LE(str_cur_s, str_cur_n);
    bool hasBE_BOM = STRI__ENC_HAS_BOM_UTF32BE(str_cur_s, str_cur_n);

    if ((!le && hasLE_BOM) || (le && hasBE_BOM))
        return 0.0;

    R_len_t numValid = 0;
    R_len_t numInvalid = 0;

    for (R_len_t i=0; i<str_cur_n; i+=4) {
        int32_t ch = le?
                     (int32_t)STRI__GET_INT32_LE(str_cur_s, i):
                     (int32_t)STRI__GET_INT32_BE(str_cur_s, i);

        if (ch < 0 || ch >= 0x10FFFF || (ch >= 0xD800 && ch <= 0xDFFF)) {
            if (!get_confidence)
                return 0.0;
            else
                numInvalid++;
        }
        else
            numValid++;
    }

    if (!get_confidence)
        return 1.0;

    if ((hasLE_BOM || hasBE_BOM) && numInvalid==0)
        return 1.0;
    else if ((hasLE_BOM || hasBE_BOM) && numValid > numInvalid*10)
        return 0.80;
    else if (numValid > 3 && numInvalid == 0)
        return 1.0;
    else if (numValid > 0 && numInvalid == 0)
        return 0.80;
    else if (numValid > numInvalid*10)
        return 0.25; // Probably corruput UTF-32BE data. Valid sequences aren't likely by chance.
    else
        return 0.0;
}


/** Check if a string is valid UTF-32BE
 *
 * @param str_cur_s character vector
 * @param str_cur_n number of bytes
 * @param get_confidence determine confidence value or do exact check
 *
 * @return confidence value in [0,1]
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-13)
 */
double ci__enc_check_utf32be(const char* str_cur_s, R_len_t str_cur_n, bool get_confidence)
{
    return ci__enc_check_utf32(str_cur_s, str_cur_n, get_confidence, false);
}


/** Check if a string is valid UTF-32LE
 *
 * @param str_cur_s character vector
 * @param str_cur_n number of bytes
 * @param get_confidence determine confidence value or do exact check
 *
 * @return confidence value in [0,1]
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-13)
 */
double ci__enc_check_utf32le(const char* str_cur_s, R_len_t str_cur_n, bool get_confidence)
{
    return ci__enc_check_utf32(str_cur_s, str_cur_n, get_confidence, true);
}


/** Which string is in given encoding
 *
 *
 *  @param str character vector or raw vector or list of raw vectors
 *  @param type (single integer, internal)
 *  @return logical vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-08)
 *          use StriContainerListRaw
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-09)
 *          one function for is_*, do dispatch
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.6-1 (Marek Gagolewski, 2015-07-11)
 *    this is internal function now
 */
SEXP ci_enc_isenc(SEXP str, int _type)
{
    double (*isenc)(const char*, R_len_t, bool) = NULL;
    switch (_type) {
    case 1:
        isenc = ci__enc_check_ascii;
        break;
    case 2:
        isenc = ci__enc_check_utf8;
        break;
    case 3:
        isenc = ci__enc_check_utf16be;
        break;
    case 4:
        isenc = ci__enc_check_utf16le;
        break;
    case 5:
        isenc = ci__enc_check_utf32be;
        break;
    case 6:
        isenc = ci__enc_check_utf32le;
        break;
    default:
        Rf_error(MSG__INCORRECT_INTERNAL_ARG); // error() call allowed here
    }


    PROTECT(str = ci__prepare_arg_list_raw(str, "str"));

    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP ret;
    {
        ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
        R_len_t str_length = 0;
        // Deviation from stringi: determine the result-shell length without
        // opening a Reader, so R allocation happens before the borrow.
        charport::unwind_protect([&]() -> SEXP {
            if (Rf_isNull(str) || isRaw(str))
                str_length = 1;
            else if (Rf_isVectorList(str))
                str_length = LENGTH(str);
            else
                str_length = ci::checked_r_len(
                    context.size(str), "character vectors"
                );
            return R_NilValue;
        });

        STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
            return Rf_allocVector(LGLSXP, str_length);
        }));
        int* ret_tab = LOGICAL(ret); // may be faster than LOGICAL(ret)[i] all the time

        {
            StriContainerListRaw str_cont(context, str);
            for (R_len_t i=0; i < str_length; ++i) {
                if (str_cont.isNA(i)) {
                    ret_tab[i] = NA_LOGICAL;
                    continue;
                }

                bool get_confidence = false; // TO BE DONE
                ret_tab[i] = isenc(
                    str_cont.get(i).data(), str_cont.get(i).length(),
                    get_confidence
                ) != 0.0;
            }
        }
    }

    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;

    STRI__ERROR_HANDLER_END({ /* no-op on error */ })
}


/** Which string is in ASCII
 *
 *  @param str character vector or raw vector or list of raw vectors
 *  @return logical vector
 *
 * @version 0.6-1 (Marek Gagolewski, 2015-07-11)
 *    call ci_enc_isenc
 */
SEXP ci_enc_isascii(SEXP str) {
    return ci_enc_isenc(str, 1);
}


/** Which string is in UTF8
 *
 *  @param str character vector or raw vector or list of raw vectors
 *  @return logical vector
 *
 * @version 0.6-1 (Marek Gagolewski, 2015-07-11)
 *    call ci_enc_isenc
 */
SEXP ci_enc_isutf8(SEXP str) {
    return ci_enc_isenc(str, 2);
}


/** Which string is in UTF-16BE
 *
 *  @param str character vector or raw vector or list of raw vectors
 *  @return logical vector
 *
 * @version 0.6-1 (Marek Gagolewski, 2015-07-11)
 *    call ci_enc_isenc
 */
SEXP ci_enc_isutf16be(SEXP str) {
    return ci_enc_isenc(str, 3);
}


/** Which string is in UTF16-LE
 *
 *  @param str character vector or raw vector or list of raw vectors
 *  @return logical vector
 *
 * @version 0.6-1 (Marek Gagolewski, 2015-07-11)
 *    call ci_enc_isenc
 */
SEXP ci_enc_isutf16le(SEXP str) {
    return ci_enc_isenc(str, 4);
}


/** Which string is in UTF-32BE
 *
 *  @param str character vector or raw vector or list of raw vectors
 *  @return logical vector
 *
 * @version 0.6-1 (Marek Gagolewski, 2015-07-11)
 *    call ci_enc_isenc
 */
SEXP ci_enc_isutf32be(SEXP str) {
    return ci_enc_isenc(str, 5);
}


/** Which string is in UTF32-LE
 *
 *  @param str character vector or raw vector or list of raw vectors
 *  @return logical vector
 *
 * @version 0.6-1 (Marek Gagolewski, 2015-07-11)
 *    call ci_enc_isenc
 */
SEXP ci_enc_isutf32le(SEXP str) {
    return ci_enc_isenc(str, 6);
}


/** Detect encoding and language
 *
 * @param str character vector
 * @param filter_angle_brackets logical vector
 *
 * @return list
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-03)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-08)
 *          use StriContainerListRaw + BUGFIX
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci_enc_detect(SEXP str, SEXP filter_angle_brackets)
{
    PROTECT(str = ci__prepare_arg_list_raw(str, "str"));
    PROTECT(filter_angle_brackets = ci__prepare_arg_logical(filter_angle_brackets, "filter_angle_brackets"));

    STRI__ERROR_HANDLER_BEGIN(2)
    SEXP ret;
    {
        std::vector<CiEncodingDetectionResult> results;
        {
            UErrorCode status = U_ZERO_ERROR;
            // Deviation from stringi: RAII closes the detector before either
            // deferred warnings or translated errors reach R.
            std::unique_ptr<UCharsetDetector, CiCharsetDetectorCloser> ucsdet(
                ucsdet_open(&status)
            );
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

            R_len_t filter_n = 0;
            charport::unwind_protect([&]() -> SEXP {
                filter_n = LENGTH(filter_angle_brackets);
                return R_NilValue;
            });
            StriContainerLogical filter(filter_angle_brackets, filter_n);

            ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
            {
                StriContainerListRaw str_cont(context, str);
                R_len_t str_n = str_cont.get_n();
                R_len_t vectorize_length = ci__encoding_recycling_rule(
                    STRI__DEFERRED_WARNINGS, str_n, filter_n
                );
                str_cont.set_nrecycle(vectorize_length);
                filter.set_nrecycle(vectorize_length);
                results.resize(static_cast<size_t>(vectorize_length));
                charport::charvec::Builder encoding(0);
                charport::charvec::Builder language(0);

                // Deviation from stringi: stage detector output while its
                // pointers are valid, then build R objects after all borrows
                // and ICU owners have been released.
                for (R_len_t i=0; i<vectorize_length; ++i) {
                    if (str_cont.isNA(i) || filter.isNA(i))
                        continue;

                    const char* str_cur_s = str_cont.get(i).data();
                    R_len_t str_cur_n = str_cont.get(i).length();

                    status = U_ZERO_ERROR;
                    ucsdet_setText(
                        ucsdet.get(), str_cur_s, str_cur_n, &status
                    );
                    STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
                    ucsdet_enableInputFilter(ucsdet.get(), filter.get(i));

                    status = U_ZERO_ERROR;
                    int matches_found = 0;
                    const UCharsetMatch** match = ucsdet_detectAll(
                        ucsdet.get(), &matches_found, &status
                    );
                    if (U_FAILURE(status) || !match || matches_found <= 0)
                        continue;

                    encoding.reset(matches_found);
                    language.reset(matches_found);
                    CiEncodingDetectionResult& current =
                        results[static_cast<size_t>(i)];
                    current.confidence.resize(
                        static_cast<size_t>(matches_found)
                    );

                    for (R_len_t j=0; j<matches_found; ++j) {
                        status = U_ZERO_ERROR;
                        const char* name = ucsdet_getName(match[j], &status);
                        if (U_FAILURE(status) || !name)
                            encoding.set_na(j);
                        else
                            ci__stage_encoding_icu_string(encoding, j, name);

                        status = U_ZERO_ERROR;
                        int32_t conf = ucsdet_getConfidence(match[j], &status);
                        current.confidence[static_cast<size_t>(j)] =
                            U_FAILURE(status) ? NA_REAL : (double)(conf)/100.0;

                        status = U_ZERO_ERROR;
                        const char* lang = ucsdet_getLanguage(match[j], &status);
                        if (U_FAILURE(status) || !lang)
                            language.set_na(j);
                        else
                            ci__stage_encoding_icu_string(language, j, lang);
                    }

                    current.encoding = encoding.release_store();
                    current.language = language.release_store();
                    current.wrong = false;
                }
            }
        }

        STRI__PROTECT(ret = ci__encoding_detection_results_to_r(results));
    }

    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;

    STRI__ERROR_HANDLER_END({ /* no-op on error */ })
}


// -----------------------------------------------------------------------
// -----------------------------------------------------------------------
// -----------------------------------------------------------------------


/** locale-dependent 8-bit converter check  [DEPRECATED]
 *
 * help struct for ci_enc_detect2
 *
 * @version 0.1-??
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-18)
 *          be locale-dependent, use ICU ulocdata
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-11-13)
 *          allow only ASCII-supersets
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-28)
 *          use StriUcnv
 */
struct Converter8bit {
    bool isNA;
    bool countChars[256];
    bool badChars[256];
    const char* name;
    const char* friendlyname;

    Converter8bit(const char* _name, const char* _friendlyname, const UnicodeSet* exset) {
        isNA = true;
        name = NULL;
        friendlyname = NULL;
        StriUcnv ucnv_obj(_name);
        if (!ucnv_obj.is8bit())
            return; // not an 8-bit converter

        //ucnv_obj.setCallBackSubstitute(); // restore default (no warn) callbacks
        UConverter* ucnv = ucnv_obj.getConverter();


        // Check which characters in given encoding
        // are not mapped to Unicode [badChars]
        // Deviation from stringi: the converter receives explicit source
        // limits, so this byte table does not need a trailing terminator.
        char allChars[256]; // all bytes 0-255
        for (R_len_t i=0; i<256; ++i)
            allChars[i] = (char)i;

        // reset tabs
        for (R_len_t i=0; i<256; ++i) {
            countChars[i] = false;
            badChars[i]   = false;
        }

        UnicodeSet curset;
        std::map<UChar32, uint8_t> curmap;
        const char* text_start = allChars+1;
        const char* text_end   = allChars+256;
        ucnv_reset(ucnv);
        for (R_len_t i=1; i<256; ++i) {
            UErrorCode status = U_ZERO_ERROR;
            UChar32 c = ucnv_getNextUChar(ucnv, &text_start, text_end, &status);
            if (U_FAILURE(status)) {
                return;
            }
            if (i >= 32 && i <= 127 && c != (UChar32)i) {
                // allow only ASCII supersets
                return;
            }

            if (c == UCHAR_REPLACEMENT || c < 0) {
                badChars[i] = true;
            }
            else {
                if (!u_isdefined(c) || u_isalpha(c))
                    badChars[i] = true;
                curset.add(c);
                curmap[c] = (uint8_t)i;
            }
        }


        if (!curset.containsAll(*exset)) {
            // not all characters are representable in given encoding
            return;
        }


        // now mark all characters form exset to be counted
        R_len_t exset_size = exset->size();
        for (R_len_t k=0; k<exset_size; ++k) {
            UChar32 c = exset->charAt(k);
            if (c >= 0) {
                uint8_t ind = curmap[c];
                countChars[ind] = true;
            }
        }

        isNA = false;
        this->name = _name;
        this->friendlyname = _friendlyname;
    }
};


// -----------------------------------------------------------------------
// -----------------------------------------------------------------------
// -----------------------------------------------------------------------


/** Guesses text encoding; help struct for ci_enc_detect2  [DEPRECATED]
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-18)
 *          locale-dependent, use ulocdata
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-11-13)
 *          allow qloc==NULL in 8bit check
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-02-24)
 *          #146 warnings removed
 */
struct EncGuess {
    const char* name;
    const char* friendlyname;
    double confidence;

    EncGuess(const char* _name, const char* _friendlyname, double _confidence) {
        name = _name;
        friendlyname = _friendlyname;
        confidence = _confidence;
    }

    bool operator<(const EncGuess& e2) const {
        return (this->confidence > e2.confidence); // decreasing sort
    }

    static void do_utf32(vector<EncGuess>& guesses, const char* str_cur_s,
                         R_len_t str_cur_n)
    {
        /* check UTF-32LE, UTF-32BE or UTF-32+BOM */
        double isutf32le = ci__enc_check_utf32le(str_cur_s, str_cur_n, true);
        double isutf32be = ci__enc_check_utf32be(str_cur_s, str_cur_n, true);
        if (isutf32le >= 0.25 && isutf32be >= 0.25) {
            // no BOM, both valid
            // i think this will never happen
            guesses.push_back(EncGuess("UTF-32LE", "UTF-32LE", isutf32le));
            guesses.push_back(EncGuess("UTF-32BE", "UTF-32BE", isutf32be));
        }
        else if (isutf32le >= 0.25) {
            if (STRI__ENC_HAS_BOM_UTF32LE(str_cur_s, str_cur_n))
                guesses.push_back(EncGuess("UTF-32", "UTF-32", isutf32le)); // with BOM
            else
                guesses.push_back(EncGuess("UTF-32LE", "UTF-32LE", isutf32le));
        }
        else if (isutf32be >= 0.25) {
            if (STRI__ENC_HAS_BOM_UTF32BE(str_cur_s, str_cur_n))
                guesses.push_back(EncGuess("UTF-32", "UTF-32", isutf32be)); // with BOM
            else
                guesses.push_back(EncGuess("UTF-32BE", "UTF-32BE", isutf32be));
        }
    }

    static void do_utf16(vector<EncGuess>& guesses, const char* str_cur_s,
                         R_len_t str_cur_n)
    {
        /* check UTF-16LE, UTF-16BE or UTF-16+BOM */
        double isutf16le = ci__enc_check_utf16le(str_cur_s, str_cur_n, true);
        double isutf16be = ci__enc_check_utf16be(str_cur_s, str_cur_n, true);
        if (isutf16le >= 0.25 && isutf16be >= 0.25) {
            // no BOM, both valid
            // this may sometimes happen
            guesses.push_back(EncGuess("UTF-16LE", "UTF-16LE", isutf16le));
            guesses.push_back(EncGuess("UTF-16BE", "UTF-16BE", isutf16be));
        }
        else if (isutf16le >= 0.25) {
            if (STRI__ENC_HAS_BOM_UTF16LE(str_cur_s, str_cur_n))
                guesses.push_back(EncGuess("UTF-16", "UTF-16", isutf16le)); // with BOM
            else
                guesses.push_back(EncGuess("UTF-16LE", "UTF-16LE", isutf16le));
        }
        else if (isutf16be >= 0.25) {
            if (STRI__ENC_HAS_BOM_UTF16BE(str_cur_s, str_cur_n))
                guesses.push_back(EncGuess("UTF-16", "UTF-16", isutf16be)); // with BOM
            else
                guesses.push_back(EncGuess("UTF-16BE", "UTF-16BE", isutf16be));
        }
    }

    static void do_8bit(vector<EncGuess>& guesses, const char* str_cur_s,
                        R_len_t str_cur_n, const char* qloc)
    {
        double is8bit = ci__enc_check_8bit(str_cur_s, str_cur_n, false);
        if (is8bit != 0.0) {
            // may be an 8-bit encoding
            double isascii = ci__enc_check_ascii(str_cur_s, str_cur_n, true);
            if (isascii >= 0.25) // i.e., equal to 1.0 => nothing more to check
                guesses.push_back(EncGuess("US-ASCII", "US-ASCII", isascii));
            else {
                // not ascii
                double isutf8 = ci__enc_check_utf8(str_cur_s, str_cur_n, true);
                if (isutf8 >= 0.25)
                    guesses.push_back(EncGuess("UTF-8", "UTF-8", isutf8));
                if (isutf8 < 1.0 && qloc) {
                    do_8bit_locale(guesses, str_cur_s, str_cur_n, qloc);
                }
            }
        }
    }

    static void do_8bit_locale(vector<EncGuess>& guesses, const char* str_cur_s,
                               R_len_t str_cur_n, const char* qloc)
    {
        vector<Converter8bit> converters;
        if (!qloc) throw StriException(MSG__INTERNAL_ERROR); // just to be sure

        {
            // Deviation from stringi: own both locale handles so every error
            // path releases them before the outer handler signals to R.
            UErrorCode status = U_ZERO_ERROR;
            std::unique_ptr<ULocaleData, CiLocaleDataCloser> uld(
                ulocdata_open(qloc, &status)
            );
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

            std::unique_ptr<USet, CiUSetCloser> exset_tmp(
                ulocdata_getExemplarSet(
                    uld.get(), NULL, USET_ADD_CASE_MAPPINGS,
                    ULOCDATA_ES_STANDARD, &status
                )
            );
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
            UnicodeSet* exset = UnicodeSet::fromUSet(exset_tmp.get());
            exset->removeAllStrings();

            R_len_t ucnv_count = (R_len_t)ucnv_countAvailable();
            for (R_len_t i=0; i<ucnv_count; ++i) { // for each converter
                const char* converter_name = ucnv_getAvailableName(i);
                Converter8bit conv(
                    converter_name,
                    StriUcnv::getFriendlyName(converter_name), exset
                );
                if (!conv.isNA) converters.push_back(conv);
            }
        }

        if (converters.size() <= 0)
            return;

        // count all bytes with codes >= 128 in str_cur_s
        R_len_t counts[256];
        R_len_t countsge128 = 0; // total count
        for (R_len_t k=0; k<256; ++k)
            counts[k] = 0; // reset tab
        for (R_len_t j=0; j<str_cur_n; ++j) {
            if ((uint8_t)(str_cur_s[j]) >= (uint8_t)128) {
                counts[(uint8_t)(str_cur_s[j])]++;
                countsge128++;
            }
        }
        // assert: countsge128 > 0 (otherwise ASCII, so this function hasn't been not called)

        std::vector<int> badCounts(converters.size(), 0); // filled with 0
        std::vector<int> desiredCounts(converters.size(),0);
        R_len_t maxDesiredCounts = 0;


        for (R_len_t j=0; j<(R_len_t)converters.size(); ++j) { // for each converter
            for (R_len_t k=128; k<256; ++k) { // for each character
                // 1. Count bytes that are BAD and NOT COUNTED in this encoding
                if (converters[j].badChars[k] && !converters[j].countChars[k]) {
                    badCounts[j] += (int)counts[k];
                }
                // 2. Count indicated characters
                if (converters[j].countChars[k]) {
                    desiredCounts[j] += (int)counts[k];
                }
            }
            if (desiredCounts[j] > maxDesiredCounts)
                maxDesiredCounts = desiredCounts[j];
        }

        // add guesses
        for (R_len_t j=0; j<(R_len_t)converters.size(); ++j) { // for each converter
            // some heuristic:
            double conf = min(1.0, max(0.0,
                                       (double)(countsge128-0.5*badCounts[j]-maxDesiredCounts+desiredCounts[j])/
                                       (double)(countsge128)));
            if (conf > 0.25)
                guesses.push_back(EncGuess(converters[j].name, converters[j].friendlyname, conf));
        }
    }
};


// -----------------------------------------------------------------------
// -----------------------------------------------------------------------


/** Detect encoding with initial guess  [DEPRECATED]
 *
 * @param str character or raw vector or a list of raw vectors
 * @param loc locale id
 *
 * @return list
 *
 * @version 0.1-?? (2013-08-15, Marek Gagolewski)
 *
 * @version 0.1-?? (2013-08-18, Marek Gagolewski)
 *          improved 8-bit confidence measurement,
 *          some code moved to structs, use locale & ICU locdata
 *
 * @version 0.1-?? (2013-11-13, Marek Gagolewski)
 *          added loc NA handling (no locale)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci_enc_detect2(SEXP str, SEXP loc)
{
    const char* qloc = /* this is R_alloc'ed */
        ci__prepare_arg_locale(loc, "locale");
    // raw vector, character vector, or list of raw vectors:
    PROTECT(str = ci__prepare_arg_list_raw(str, "str"));

    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP ret;
    {
        std::vector<CiEncodingDetectionResult> results;
        {
            ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
            StriContainerListRaw str_cont(context, str);
            R_len_t str_n = str_cont.get_n();
            results.resize(static_cast<size_t>(str_n));
            charport::charvec::Builder encoding(0);
            charport::charvec::Builder language(0);

            // Deviation from stringi: stage the variable-size character
            // results until the Reader and all temporary converters are gone.
            for (R_len_t i=0; i<str_n; ++i) {
                if (str_cont.isNA(i))
                    continue;

                const char* str_cur_s = str_cont.get(i).data();
                R_len_t str_cur_n = str_cont.get(i).length();
                if (str_cur_n <= 0)
                    continue;

                vector<EncGuess> guesses;
                guesses.reserve(6);

                EncGuess::do_utf32(guesses, str_cur_s, str_cur_n);
                EncGuess::do_utf16(guesses, str_cur_s, str_cur_n);
                EncGuess::do_8bit(
                    guesses, str_cur_s, str_cur_n, qloc
                );  // includes UTF-8

                R_len_t matches_found = (R_len_t)guesses.size();
                if (matches_found <= 0)
                    continue;

                std::stable_sort(guesses.begin(), guesses.end());

                encoding.reset(matches_found);
                language.reset(matches_found);
                CiEncodingDetectionResult& current =
                    results[static_cast<size_t>(i)];
                current.confidence.resize(
                    static_cast<size_t>(matches_found)
                );

                for (R_len_t j=0; j<matches_found; ++j) {
                    ci__stage_encoding_icu_string(
                        encoding, j, guesses[static_cast<size_t>(j)].friendlyname
                    );
                    current.confidence[static_cast<size_t>(j)] =
                        guesses[static_cast<size_t>(j)].confidence;
                    language.set_na(j); // always no lang
                }

                current.encoding = encoding.release_store();
                current.language = language.release_store();
                current.wrong = false;
            }
        }

        STRI__PROTECT(ret = ci__encoding_detection_results_to_r(results));
    }

    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;

    STRI__ERROR_HANDLER_END({ /* no-op on error */ })
}
