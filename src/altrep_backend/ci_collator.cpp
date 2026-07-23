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
#include <unicode/ucol.h>
#include <unicode/usearch.h>


/**
 * Create & set up an ICU Collator
 *
 * WARNING: this function may call R error helpers. Call it through
 * charport::unwind_protect inside STRI__ERROR_HANDLER_BEGIN.
 *
 * @param warnings entry point's deferred warning queue
 * @param opts_collator named R list
 * @return a Collator object that should be closed with ucol_close() after use
 *
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-17)
 *          allow for NULL opts_collator (identical to list())
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-09)
 *          disallow NA as opts_collator
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc;
 *    + many other bugs in settings establishment
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-06)
 *    Fetch opts vals first to avoid memleaks (missing ucol_close calls on Rf_error)
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-08)
 *    #23: add `overlap` option
 *
 * @version 1.1.6 (Marek Gagolewski, 2017-11-10)
 *    PROTECT STRING_ELT(names, i)
 *
 * @version 1.8.1 (Marek Gagolewski, 2023-11-07)
 *    #476: Warn when falling back to the root locale, make C==en_US_POSIX
 */
UCollator* ci__ucol_open(
    ci::DeferredWarnings& warnings, SEXP opts_collator
)
{
    if (!Rf_isNull(opts_collator) && !Rf_isVectorList(opts_collator))
        throw StriException(MSG__INCORRECT_COLLATOR_OPTION_SPEC);

    R_len_t narg = Rf_isNull(opts_collator)?0:LENGTH(opts_collator);

    const char* default_locale = ci__prepare_arg_locale(
        R_NilValue, "locale", true, true, &warnings
    );

    if (narg <= 0) { // no custom settings - use default Collator
        UErrorCode status = U_ZERO_ERROR;
        UCollator* col = ucol_open(default_locale, &status);
        STRI__CHECKICUSTATUS_THROW(status, {
            if (col) ucol_close(col);
        })
        return col;
    }

    SEXP names = PROTECT(Rf_getAttrib(opts_collator, R_NamesSymbol));
    int nprotect = 1;

    /* Fetch every option before opening the collator. */
    UColAttributeValue  opt_FRENCH_COLLATION = UCOL_DEFAULT;
    UColAttributeValue  opt_ALTERNATE_HANDLING = UCOL_DEFAULT;
    UColAttributeValue  opt_CASE_FIRST = UCOL_DEFAULT;
    UColAttributeValue  opt_CASE_LEVEL = UCOL_DEFAULT;
    UColAttributeValue  opt_NORMALIZATION_MODE = UCOL_DEFAULT;
    UColAttributeValue  opt_STRENGTH =  UCOL_DEFAULT_STRENGTH;
    UColAttributeValue  opt_NUMERIC_COLLATION = UCOL_DEFAULT;
//   USearchAttributeValue  opt_OVERLAP = USEARCH_OFF;
    const char*         opt_LOCALE = default_locale;

    // Deviation from stringi: controlled validation and warning-aware scalar
    // preparation throw C++, so release every local protection before this
    // parser propagates an error to the operation boundary.
    try {
        if (names == R_NilValue || LENGTH(names) != narg)
            throw StriException(MSG__INCORRECT_COLLATOR_OPTION_SPEC);

        for (R_len_t i=0; i<narg; ++i) {
            if (STRING_ELT(names, i) == NA_STRING)
                throw StriException(MSG__INCORRECT_COLLATOR_OPTION_SPEC);

            SEXP tmp_arg;
            PROTECT(tmp_arg = STRING_ELT(names, i));
            ++nprotect;
            const char* curname = ci__copy_string_Ralloc(
                tmp_arg, "curname"
            ); /* this is R_alloc'ed */
            UNPROTECT(1);
            --nprotect;

            PROTECT(tmp_arg = VECTOR_ELT(opts_collator, i));
            ++nprotect;
            if (!strcmp(curname, "locale")) {
                opt_LOCALE = ci__prepare_arg_locale(
                    tmp_arg, "locale", true, true, &warnings
                ); /* this is R_alloc'ed */
            } else if  (!strcmp(curname, "strength")) {
                int val = ci__prepare_arg_integer_1_notNA(
                    tmp_arg, "strength", &warnings
                );
                if (val < (int)UCOL_PRIMARY + 1) val = (int)UCOL_PRIMARY + 1;
                else if (val > (int)UCOL_STRENGTH_LIMIT + 1) val = (int)UCOL_STRENGTH_LIMIT + 1;
                opt_STRENGTH = (UColAttributeValue)(val-1);
//      } else if  (!strcmp(curname, "overlap") && allow_overlap) {
//         bool val_bool = ci__prepare_arg_logical_1_notNA(tmp_arg, "overlap");
//         opt_OVERLAP = (val_bool?USEARCH_ON:USEARCH_OFF);
            } else if  (!strcmp(curname, "alternate_shifted")) {
                bool val_bool = ci__prepare_arg_logical_1_notNA(
                    tmp_arg, "alternate_shifted", &warnings
                );
                opt_ALTERNATE_HANDLING = (val_bool?UCOL_SHIFTED:UCOL_NON_IGNORABLE);
            } else if  (!strcmp(curname, "uppercase_first")) {
                SEXP val;
                PROTECT(val = ci__prepare_arg_logical_1(
                    tmp_arg, "uppercase_first", &warnings
                ));
                opt_CASE_FIRST = (LOGICAL(val)[0]==NA_LOGICAL?UCOL_OFF:
                                  (LOGICAL(val)[0]?UCOL_UPPER_FIRST:UCOL_LOWER_FIRST));
                UNPROTECT(1);
            } else if  (!strcmp(curname, "french")) {
                bool val_bool = ci__prepare_arg_logical_1_notNA(
                    tmp_arg, "french", &warnings
                );
                opt_FRENCH_COLLATION = (val_bool?UCOL_ON:UCOL_OFF);
            } else if  (!strcmp(curname, "case_level")) {
                bool val_bool = ci__prepare_arg_logical_1_notNA(
                    tmp_arg, "case_level", &warnings
                );
                opt_CASE_LEVEL = (val_bool?UCOL_ON:UCOL_OFF);
            } else if  (!strcmp(curname, "normalization")) {
                bool val_bool = ci__prepare_arg_logical_1_notNA(
                    tmp_arg, "normalization", &warnings
                );
                opt_NORMALIZATION_MODE = (val_bool?UCOL_ON:UCOL_OFF);
            } else if  (!strcmp(curname, "numeric")) {
                bool val_bool = ci__prepare_arg_logical_1_notNA(
                    tmp_arg, "numeric", &warnings
                );
                opt_NUMERIC_COLLATION = (val_bool?UCOL_ON:UCOL_OFF);
            } else {
                // Deviation from stringi: own collator warning text until the
                // entry point has released its collator and borrowed inputs.
                std::string warning("incorrect opts_collator setting: '");
                warning += curname;
                warning += "'; ignoring";
                warnings.push(warning.c_str());
            }
            UNPROTECT(1);
            --nprotect;
        }
    }
    catch (...) {
        UNPROTECT(nprotect);
        throw;
    }
    UNPROTECT(nprotect); /* names */

    // create collator
    UErrorCode status = U_ZERO_ERROR;
    UCollator* col = ucol_open(opt_LOCALE, &status);
    STRI__CHECKICUSTATUS_THROW(status, {
        if (col) ucol_close(col);
    })

    if (status == U_USING_DEFAULT_WARNING && opt_LOCALE) {
        UErrorCode status2 = U_ZERO_ERROR;
        const char* valid_locale = ucol_getLocaleByType(col, ULOC_VALID_LOCALE, &status2);
        if (valid_locale && !strcmp(valid_locale, "root")) {
            // Deviation from stringi: queue this warning while the collator is
            // live, then emit it after the caller closes the collator.
            try {
                warnings.push(ICUError::getICUerrorName(status));
            }
            catch (...) {
                ucol_close(col);
                throw;
            }
        }
    }
    // else if (status == U_USING_FALLBACK_WARNING)  // warning on this would be too invasive
    //    Rf_warning("%s", ICUError::getICUerrorName(status));


    // set other opts
//   if (opt_OVERLAP != UCOL_OFF) {
//      status = U_ZERO_ERROR;
//      ucol_setAttribute(col, UCOL_OVERLAP, opt_OVERLAP, &status);
//      STRI__CHECKICUSTATUS_RFERROR(status, { ucol_close(col); }) // error() allowed here
//   }

    if (opt_STRENGTH != UCOL_DEFAULT_STRENGTH) {
        status = U_ZERO_ERROR;
        ucol_setAttribute(col, UCOL_STRENGTH, opt_STRENGTH, &status);
        STRI__CHECKICUSTATUS_THROW(status, { ucol_close(col); })
    }

    if (opt_FRENCH_COLLATION != UCOL_DEFAULT) {
        status = U_ZERO_ERROR;
        ucol_setAttribute(col, UCOL_FRENCH_COLLATION, opt_FRENCH_COLLATION, &status);
        STRI__CHECKICUSTATUS_THROW(status, { ucol_close(col); })
    }

    if (opt_ALTERNATE_HANDLING != UCOL_DEFAULT) {
        status = U_ZERO_ERROR;
        ucol_setAttribute(col, UCOL_ALTERNATE_HANDLING, opt_ALTERNATE_HANDLING, &status);
        STRI__CHECKICUSTATUS_THROW(status, { ucol_close(col); })
    }

    if (opt_CASE_FIRST != UCOL_DEFAULT) {
        status = U_ZERO_ERROR;
        ucol_setAttribute(col, UCOL_CASE_FIRST, opt_CASE_FIRST, &status);
        STRI__CHECKICUSTATUS_THROW(status, { ucol_close(col); })
    }

    if (opt_CASE_LEVEL != UCOL_DEFAULT) {
        status = U_ZERO_ERROR;
        ucol_setAttribute(col, UCOL_CASE_LEVEL, opt_CASE_LEVEL, &status);
        STRI__CHECKICUSTATUS_THROW(status, { ucol_close(col); })
    }

    if (opt_NORMALIZATION_MODE != UCOL_DEFAULT) {
        status = U_ZERO_ERROR;
        ucol_setAttribute(col, UCOL_NORMALIZATION_MODE, opt_NORMALIZATION_MODE, &status);
        STRI__CHECKICUSTATUS_THROW(status, { ucol_close(col); })
    }

    if (opt_NUMERIC_COLLATION != UCOL_DEFAULT) {
        status = U_ZERO_ERROR;
        ucol_setAttribute(col, UCOL_NUMERIC_COLLATION, opt_NUMERIC_COLLATION, &status);
        STRI__CHECKICUSTATUS_THROW(status, { ucol_close(col); })
    }

    return col;
}
