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
#include "ci_container_utf16.h"
#include "ci_ucnv.h"

#include <memory>
#include <utility>


/**
 * Default constructor
 *
 */
StriContainerUTF16::StriContainerUTF16()
    : StriContainerBase(), str()
{
}


/** container for nrecycle fresh, brand new, writable UnicodeStrings
 *
 * Each string is initially empty.
 *
 * @param nrecycle number of strings
 */
StriContainerUTF16::StriContainerUTF16(R_len_t _nrecycle)
    : StriContainerBase(), str()
{
    this->init_Base(_nrecycle, _nrecycle, false);
    if (this->n > 0) {
        this->str.reset(new UnicodeString[this->n]);
        STRI_ASSERT(this->str);
        if (!this->str) throw StriException(MSG__MEM_ALLOC_ERROR_WITH_SIZE,
                                                this->n*sizeof(UnicodeString));
    }
}


/**
 * Construct String Container from an R character vector
 *
 * @param rstr R character vector
 * @param nrecycle extend length [vectorization]
 * @param shallowrecycle will \code{this->str} be ever modified?
 *
 * @version 1.0.6 (Marek Gagolewski, 2017-05-25)
 *    #270 latin-1 is windows-1252 on Windows
 */
StriContainerUTF16::StriContainerUTF16(
    ci::ReaderContext& context, SEXP rstr,
    R_len_t _nrecycle, bool _shallowrecycle
) : StriContainerBase(), str()
{
    R_len_t nrstr = ci::checked_r_len(
        context.size(rstr), "character vectors"
    );
    this->init_Base(nrstr, _nrecycle, _shallowrecycle);

    if (this->n == 0)
        return; /* nothing more to do */

    STRI_ASSERT(this->n > 0);
    std::shared_ptr<ci::ReaderBorrow> borrow = context.acquire(rstr);
    const charport::StrViews& views = borrow->views();

    // Deviation from stringi: Reader access and conversion can throw, so keep
    // the partially initialized array under RAII until construction succeeds.
    this->str.reset(new UnicodeString[this->n]);
    for (R_len_t i=0; i<this->n; ++i)
        this->str[i].setToBogus(); // in case it fails during conversion (this is NA)

    /* Important: ICU provides full internationalisation functionality
    without any conversion table data. The common library contains
    code to handle several important encodings algorithmically: US-ASCII,
    ISO-8859-1, UTF-7/8/16/32, SCSU, BOCU-1, CESU-8, and IMAP-mailbox-name */
    //StriUcnv ucnvASCII("US-ASCII");
#if defined(_WIN32) || defined(_WIN64)
    // #270: latin-1 is windows-1252 on Windows
    StriUcnv ucnvLatin1("WINDOWS-1252");
#else
    StriUcnv ucnvLatin1("ISO-8859-1");
#endif
    StriUcnv ucnvNative(NULL);

    for (R_len_t i=0; i<nrstr; ++i) {
        const charport::StrView curs = views[i];
        if (curs.is_na()) {
            continue; // keep NA
        }

        // if (IS_ASCII(curs)) {
        //     // Version 1:
        //     UConverter* ucnv = ucnvASCII.getConverter();
        //     UErrorCode status = U_ZERO_ERROR;
        //     this->str[i].setTo(
        //         UnicodeString((const char*)CHAR(curs), (int32_t)LENGTH(curs), ucnv, status)
        //     );
        //     STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
        //
        //     // Performance improvement attempt #1:
        //     // this->str[i] = new UnicodeString(UnicodeString::fromUTF8(CHAR(curs)));
        //     // if (!this->str) throw StriException(MSG__MEM_ALLOC_ERROR);
        //     // slower than the above
        //
        //     // Performance improvement attempt #2:
        //     // Create UChar buf with LENGTH(curs) items, fill it with (CHAR(curs)[i], 0x00), i=1,...
        //     // This wasn't faster than the ucnvASCII approach.
        //
        //     // Performance improvement attempt #3:
        //     // slightly slower than ucnvASCII
        //     // R_len_t curs_n = LENGTH(curs);
        //     // const char* curs_s = CHAR(curs);
        //     // this->str[i].remove(); // unset bogus (NA)
        //     // UChar* buf = this->str[i].getBuffer(curs_n);
        //     // for (R_len_t k=0; k<curs_n; ++k)
        //     //   buf[k] = (UChar)curs_s[k]; // well, this is ASCII :)
        //     // this->str[i].releaseBuffer(curs_n);
        // }
        // else
        if (curs.enc == cetype_ext_t::CE_UTF8 ||
                curs.enc == cetype_ext_t::CE_ASCII_OR_UTF8 ||
                curs.enc == cetype_ext_t::CE_ASCII) {
            // using ucnvUTF8 is slower for UTF-8
            // the same is done for native encoding && ucnvNative_isUTF8

            // this is slower if IS_ASCII than ucnvASCII, but doesn't limit
            // the input string length to 858993458 characters (#487)
            this->str[i].setTo(
                UnicodeString::fromUTF8(StringPiece(curs.ptr, curs.len))
            );
        }
        else if (curs.enc == cetype_ext_t::CE_LATIN1) {
            UConverter* ucnv = ucnvLatin1.getConverter();
            UErrorCode status = U_ZERO_ERROR;
            this->str[i].setTo(
                UnicodeString(curs.ptr, curs.len, ucnv, status)
            );
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
        }
        else if (curs.enc == cetype_ext_t::CE_BYTES) {
            throw StriException(MSG__BYTESENC);
        }
        else if (curs.enc == cetype_ext_t::CE_NATIVE) {
            // an "unknown" (native) encoding may be set to UTF-8 (speedup)
            if (ucnvNative.isUTF8()) {
                // UTF-8
                this->str[i].setTo(
                    UnicodeString::fromUTF8(StringPiece(curs.ptr, curs.len))
                );
            }
            else {
                UConverter* ucnv = ucnvNative.getConverter();
                UErrorCode status = U_ZERO_ERROR;
                this->str[i].setTo(
                    UnicodeString(curs.ptr, curs.len, ucnv, status)
                );
                STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
            }
        }
        else {
            throw StriException("unknown charport string encoding");
        }
    }

    if (!_shallowrecycle) {
        for (R_len_t i=nrstr; i<this->n; ++i) {
            this->str[i].setTo(this->str[i%nrstr]);
        }
    }
}


/** Copy constructor
 *
 *  @param container source
 */
StriContainerUTF16::StriContainerUTF16(StriContainerUTF16& container)
    : StriContainerBase((StriContainerBase&)container), str()
{
    if (container.str) {
        this->str.reset(new UnicodeString[this->n]);
        for (int i=0; i<this->n; ++i) {
            this->str[i].setTo(container.str[i]);
        }
    }
}


/**
 *  @param container source
 *  @return self
 */
StriContainerUTF16& StriContainerUTF16::operator=(StriContainerUTF16& container)
{
    if (this == &container)
        return *this;

    std::unique_ptr<UnicodeString[]> new_str;
    if (container.str) {
        new_str.reset(new UnicodeString[container.n]);
        for (int i=0; i<container.n; ++i) {
            new_str[i].setTo(container.str[i]);
        }
    }

    // Deviation from stringi: replace the old array without ending this
    // object's lifetime through an explicit destructor call.
    this->str.reset();
    (StriContainerBase&) (*this) = (StriContainerBase&)container;
    this->str = std::move(new_str);
    return *this;
}


/** Destructor
 *
 */
StriContainerUTF16::~StriContainerUTF16() = default;


/** Convert Unicode16-Char indexes to Unicode32 (code points)
 *
 * \code{i1} and \code{i2} must be sorted increasingly
 *
 * @param i element index
 * @param i1 indexes, 1-based [in/out]
 * @param i2 indexes, 1-based [in/out]
 * @param ni size of \code{i1} and \code{i2}
 * @param adj1 adjust for \code{i1}
 * @param adj2 adjust for \code{i2}
 *
 * @version 0.5-1 (Marek Gagolewski, 2014-12-21)
 *    #132 incorrect behaviour for i2[j2] == i2[j2+1]
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29) ignore NA and negative indexes
 */
void StriContainerUTF16::UChar16_to_UChar32_index(
    R_len_t i,
    int* i1, int* i2, const int ni, int adj1, int adj2
) {
    const UnicodeString* str_data = &(this->get(i));
    const UChar* cstr = str_data->getBuffer();
    const int nstr = str_data->length();

    int j1 = 0;
    int j2 = 0;

    int i16 = 0;
    int i32 = 0;
    while (i16 < nstr && (j1 < ni || j2 < ni)) {

        while (j1 < ni && i1[j1] <= i16) {
            if (i1[j1] == NA_INTEGER || i1[j1] < 0) { ++j1; continue; }
#ifndef NDEBUG
            if (j1 < ni-1 && i1[j1+1] != NA_INTEGER && i1[j1+1] >= 0 && i1[j1] > i1[j1+1])
                throw StriException("DEBUG: ci__UChar16_to_UChar32_index 1");
#endif
            i1[j1] = i32 + adj1;
            ++j1;
        }

        while (j2 < ni && i2[j2] <= i16) {
            if (i2[j2] == NA_INTEGER || i2[j2] < 0) { ++j2; continue; }
#ifndef NDEBUG
            if (j2 < ni-1 && i2[j2+1] != NA_INTEGER && i2[j2+1] >= 0 && i2[j2] > i2[j2+1])
                throw StriException("DEBUG: ci__UChar16_to_UChar32_index 2");
#endif
            i2[j2] = i32 + adj2;
            ++j2;
        }

        // Next UChar32
        U16_FWD_1(cstr, i16, nstr);
        ++i32;
    }

    // CONVERT LAST:
    while (j1 < ni && i1[j1] <= nstr) {
        if (i1[j1] == NA_INTEGER || i1[j1] < 0) { ++j1; continue; }
//#ifndef NDEBUG
//      if (j1 < ni-1 && i1[j1] >= i1[j1+1])
//         throw StriException("DEBUG: ci__UChar16_to_UChar32_index 3");
//#endif
        i1[j1] = i32 + adj1;
        ++j1;
    }

    while (j2 < ni && i2[j2] <= nstr) {
        if (i2[j2] == NA_INTEGER || i2[j2] < 0) { ++j2; continue; }
//#ifndef NDEBUG
//      if (j2 < ni-1 && i2[j2] >= i2[j2+1])
//         throw StriException("DEBUG: ci__UChar16_to_UChar32_index 4");
//#endif
        i2[j2] = i32 + adj2;
        ++j2;
    }

    // CHECK:
#ifndef NDEBUG
    if (i16 >= nstr && (j1 < ni || j2 < ni))
        throw StriException("DEBUG: ci__UChar16_to_UChar32_index 5");
#endif
}
