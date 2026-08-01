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

#include <cstdio>

namespace charr { namespace altrep_backend {


namespace ucnv {

CHARR_CXX_HELPER static void ci__queue_converter_warning(
    shared::DeferredWarnings* warnings, UErrorCode* status,
    const char* message
) noexcept
{
    if (!warnings) {
        *status = U_INTERNAL_PROGRAM_ERROR;
        return;
    }

    try {
        warnings->push(message);
    }
    catch (...) {
        // Exceptions must not cross an ICU C callback boundary.
        *status = U_MEMORY_ALLOCATION_ERROR;
    }
}


} // namespace ucnv

using namespace ucnv;


/**
 * Opens (on demand) a desired converter
 *
 * The converter is opened if necessary.
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-10)
 *          Use own error callbacks
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-28)
 *          moved to StriUcnv;
 *          throws StriException instead of calling Rf_error
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-01)
 *    don't register callbacks by default
 */
void StriUcnv::openConverter() {
    if (m_ucnv)
        return;

    UErrorCode status = U_ZERO_ERROR;

    m_ucnv = ucnv_open(m_name, &status);
    STRI__CHECKICUSTATUS_THROW(status, { m_ucnv = NULL; })

    if (m_warnings) {
        // Deviation from stringi: converter callbacks queue owned warning
        // messages. The entry point emits them after this converter closes.
        status = U_ZERO_ERROR;
        ucnv_setFromUCallBack((UConverter*)m_ucnv,
                              (UConverterFromUCallback)STRI__UCNV_FROM_U_CALLBACK_SUBSTITUTE_WARN,
                              (const void *)m_warnings, (UConverterFromUCallback *)NULL,
                              (const void **)NULL,
                              &status);
        STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

        status = U_ZERO_ERROR;
        ucnv_setToUCallBack  ((UConverter*)m_ucnv,
                              (UConverterToUCallback)STRI__UCNV_TO_U_CALLBACK_SUBSTITUTE_WARN,
                              (const void *)m_warnings,
                              (UConverterToUCallback *)NULL,
                              (const void **)NULL,
                              &status);
        STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
    }
}


/** Returns a desired converted
 *
 * @return UConverter
 * @version 0.2-1 (Marek Gagolewski)
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-01)
 *    don't register callbacks by default
 */
UConverter* StriUcnv::getConverter()
{
    openConverter();
#ifndef NDEBUG
    if (!m_ucnv) throw StriException("!NDEBUG: StriUcnv::getConverter()");
#endif
    return m_ucnv;
}


/** Own fallback function for ucnv conversion: substitute & warn
 *
 *
 * @param context DeferredWarnings queue owned by the entry point
 * @param toUArgs Information about the conversion in progress
 * @param codeUnits Points to 'length' bytes of the concerned codepage sequence
 * @param length Size (in bytes) of the concerned codepage sequence
 * @param reason Defines the reason the callback was invoked
 * @param err Return value will be set to success if the callback was handled,
 *      otherwise this value will be set to a failure status.
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-10)
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-28)
 *          moved to StriUcnv
 */
void StriUcnv::STRI__UCNV_TO_U_CALLBACK_SUBSTITUTE_WARN (
    const void *context,
    UConverterToUnicodeArgs *toArgs,
    const char* codeUnits,
    int32_t length,
    UConverterCallbackReason reason,
    UErrorCode * err) noexcept
{
    bool wasSubstitute = (reason <= UCNV_IRREGULAR);

    // "DO NOT CALL THIS FUNCTION DIRECTLY!" :>
    UCNV_TO_U_CALLBACK_SUBSTITUTE(NULL, toArgs, codeUnits, length, reason, err);

    if (*err == U_ZERO_ERROR && wasSubstitute) {
        char message[StriException_BUFSIZE];
        switch (length) {
        case 1:
            snprintf(
                message, sizeof(message),
                MSG__UNCONVERTIBLE_BINARY_1, codeUnits[0]
            );
            break;
        case 2:
            snprintf(
                message, sizeof(message),
                MSG__UNCONVERTIBLE_BINARY_2, codeUnits[0], codeUnits[1]
            );
            break;
        case 3:
            snprintf(
                message, sizeof(message),
                MSG__UNCONVERTIBLE_BINARY_3,
                codeUnits[0], codeUnits[1], codeUnits[2]
            );
            break;
        case 4:
            snprintf(
                message, sizeof(message),
                MSG__UNCONVERTIBLE_BINARY_4,
                codeUnits[0], codeUnits[1], codeUnits[2], codeUnits[3]
            );
            break;
        default:
            snprintf(
                message, sizeof(message), "%s",
                MSG__UNCONVERTIBLE_BINARY_n
            );
            break;
        }
        ci__queue_converter_warning(
            (shared::DeferredWarnings*)context, err, message
        );
    }
}


/** Own fallback function for ucnv conversion: substitute & warn
 *
 *
 * @param context DeferredWarnings queue owned by the entry point
 * @param fromUArgs Information about the conversion in progress
 * @param codeUnits Points to 'length' UChars of the concerned Unicode sequence
 * @param length Size (in bytes) of the concerned codepage sequence
 * @param codePoint Single UChar32 (UTF-32) containing the concerend Unicode codepoint.
 * @param reason Defines the reason the callback was invoked
 * @param err Return value will be set to success if the callback was handled,
 *      otherwise this value will be set to a failure status.
 * @see ucnv_setSubstChars
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-10)
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-28)
 *          moved to StriUcnv
 */
void StriUcnv::STRI__UCNV_FROM_U_CALLBACK_SUBSTITUTE_WARN (
    const void *context,
    UConverterFromUnicodeArgs *fromArgs,
    const UChar* codeUnits,
    int32_t length,
    UChar32 codePoint,
    UConverterCallbackReason reason,
    UErrorCode * err) noexcept
{
    bool wasSubstitute = (reason <= UCNV_IRREGULAR);

    // "DO NOT CALL THIS FUNCTION DIRECTLY!" :>
    UCNV_FROM_U_CALLBACK_SUBSTITUTE(NULL, fromArgs, codeUnits, length, codePoint, reason, err);

    if (*err == U_ZERO_ERROR && wasSubstitute) {
        char message[StriException_BUFSIZE];
        snprintf(
            message, sizeof(message),
            MSG__UNCONVERTIBLE_CODE_POINT, codePoint
        );
        ci__queue_converter_warning(
            (shared::DeferredWarnings*)context, err, message
        );
    }
}



} } // namespace charr::altrep_backend
