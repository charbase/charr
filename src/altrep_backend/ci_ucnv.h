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


#ifndef __ci_ucnv_h
#define __ci_ucnv_h

#include "ci_stringi.h"
#include "../shared/deferred_warnings.h"
#include "../shared/lint.h"
#include <unicode/ucnv.h>

namespace charr { namespace altrep_backend {


/**
 * A class to manage an encoding converter
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-28)
 *
 * @version 1.0.6 (Marek Gagolewski, 2017-05-25)
 *    #270: latin-1 is windows-1252 on Windows
 *
 * @version 1.7.5.9001 (Marek Gagolewski, 2021-11-27)
 *    #467: R-win-ucrt not marking strings as latin1 #
 */
class CHARR_OWNER_TYPE StriUcnv  {

private:

    UConverter* m_ucnv; // converter
    const char* m_name; // encoding, owned by caller
    shared::DeferredWarnings* m_warnings; // optional queue, owned by caller

    CHARR_CXX_HELPER static void
    STRI__UCNV_FROM_U_CALLBACK_SUBSTITUTE_WARN (
        const void* context,
        UConverterFromUnicodeArgs* fromArgs,
        const UChar* codeUnits,
        int32_t length,
        UChar32 codePoint,
        UConverterCallbackReason reason,
        UErrorCode* err) noexcept;

    CHARR_CXX_HELPER static void
    STRI__UCNV_TO_U_CALLBACK_SUBSTITUTE_WARN (
        const void* context,
        UConverterToUnicodeArgs* toArgs,
        const char* codeUnits,
        int32_t length,
        UConverterCallbackReason reason,
        UErrorCode* err) noexcept;

    CHARR_CXX_HELPER void openConverter();

public:


    CHARR_CXX_HELPER StriUcnv(const char* name=NULL) noexcept {
        m_name = name;
        m_ucnv = NULL; // lazy
        m_warnings = NULL;
    }

    /** The warning queue must outlive this converter. */
    CHARR_CXX_HELPER StriUcnv(
        const char* name, shared::DeferredWarnings& warnings
    ) noexcept {
        m_name = name;
        m_ucnv = NULL; // lazy
        m_warnings = &warnings;
    }

    CHARR_CXX_HELPER ~StriUcnv() noexcept
    {
        if (m_ucnv)
            ucnv_close(m_ucnv);
        m_ucnv = NULL;
    }

    StriUcnv(const StriUcnv&) = delete;
    StriUcnv& operator=(const StriUcnv&) = delete;

    CHARR_CXX_HELPER StriUcnv& operator=(StriUcnv&& obj) noexcept {
        if (this == &obj)
            return *this;
        if (m_ucnv)
            ucnv_close(m_ucnv);
        m_ucnv = obj.m_ucnv;
        m_name = obj.m_name;
        m_warnings = obj.m_warnings;
        obj.m_ucnv = NULL;
        obj.m_name = NULL;
        obj.m_warnings = NULL;
        return *this;
    }

    CHARR_CXX_HELPER UConverter* getConverter();

    /**
     * get R's cetype_t corresponding to this converter
     */
    CHARR_CXX_HELPER cetype_t getCE() {
        openConverter();
        UErrorCode status = U_ZERO_ERROR;
        const char* ucnv_name = ucnv_getName(m_ucnv, &status);
        STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

        if (!strcmp(ucnv_name, "US-ASCII")) {
            return CE_UTF8;
        }
        else if (!strcmp(ucnv_name, "UTF-8")) {
            return CE_UTF8;
        }
#if defined(_WIN32) || defined(_WIN64)
        // #270: latin-1 is windows-1252 on Windows
        // #467: R-win-ucrt not marking strings as latin1
        else if (
            !strcmp(ucnv_name, "windows-1252") ||
            !strcmp(ucnv_name, "ibm-5348_P100-1997") ||
            !strcmp(ucnv_name, "ibm-1252_P100-2000") ||
            !strcmp(ucnv_name, "ISO-8859-1") ||
            !strcmp(ucnv_name, "latin1")
        ) {
#else
        else if (
            !strcmp(ucnv_name, "ISO-8859-1") ||
            !strcmp(ucnv_name, "latin1")
        ) {
#endif
            return CE_LATIN1;
        }
        return CE_BYTES;
    }
};


} } // namespace charr::altrep_backend

#endif
