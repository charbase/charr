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
#include "ci_string8buf.h"
#include "ci_container_utf8.h"
#include <unicode/strenum.h>
#include <unicode/dtfmtsym.h>
#include <string>
#include <utility>
#include <vector>


/** List Localizable Date-Time Formatting Data
 *
 * @param locale single string or NULL
 * @param context single string
 * @param width single string
 * @return list
 *
 * @version 0.5-1 (Marek Gagolewski, 2014-12-25)
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-01-01)
 *    use calendar keyword in locale
 */
SEXP ci_datetime_symbols(SEXP locale, SEXP context, SEXP width)
{
    const R_len_t infosize = 5;
    STRI__ERROR_HANDLER_BEGIN(0)
    SEXP vals;
    {
        ci::ReaderContext reader_context(STRI__DEFERRED_WARNINGS);
        const char* qloc = NULL;
        charport::unwind_protect([&]() -> SEXP {
            qloc = ci__prepare_arg_locale(
                locale, "locale", true, true,
                &STRI__DEFERRED_WARNINGS
            );
            return R_NilValue;
        });

        const char* context_opts[] = {"format", "standalone", NULL};
        SEXP context_arg = R_NilValue;
        STRI__PROTECT(context_arg = charport::unwind_protect([&]() -> SEXP {
            return ci__prepare_arg_string_1(
                context, "context", &STRI__DEFERRED_WARNINGS
            );
        }));
        int context_cur = -1;
        {
            std::shared_ptr<ci::ReaderBorrow> borrow =
                reader_context.acquire(context_arg);
            const charport::StrView value = borrow->view(0);
            if (value.is_na())
                throw StriException(MSG__ARG_EXPECTED_NOT_NA, "context");
            context_cur = ci__match_arg(
                value.ptr, value.len, context_opts
            );
        }

        const char* width_opts[] = {"abbreviated", "wide", "narrow", NULL};
        SEXP width_arg = R_NilValue;
        STRI__PROTECT(width_arg = charport::unwind_protect([&]() -> SEXP {
            return ci__prepare_arg_string_1(
                width, "width", &STRI__DEFERRED_WARNINGS
            );
        }));
        int width_cur = -1;
        {
            std::shared_ptr<ci::ReaderBorrow> borrow =
                reader_context.acquire(width_arg);
            const charport::StrView value = borrow->view(0);
            if (value.is_na())
                throw StriException(MSG__ARG_EXPECTED_NOT_NA, "width");
            width_cur = ci__match_arg(value.ptr, value.len, width_opts);
        }

        DateFormatSymbols::DtContextType context_val;
        if (context_cur == 0)
            context_val = DateFormatSymbols::FORMAT;
        else if (context_cur == 1)
            context_val = DateFormatSymbols::STANDALONE;
        else
            throw StriException(MSG__INCORRECT_MATCH_OPTION, "context");

        DateFormatSymbols::DtWidthType width_val;
        if (width_cur == 0)
            width_val = DateFormatSymbols::ABBREVIATED;
        else if (width_cur == 1)
            width_val = DateFormatSymbols::WIDE;
        else if (width_cur == 2)
            width_val = DateFormatSymbols::NARROW;
        else
            throw StriException(MSG__INCORRECT_MATCH_OPTION, "width");

        std::vector<charport::charvec::Store> stores;
        stores.reserve(static_cast<size_t>(infosize));
        {
            UErrorCode status = U_ZERO_ERROR;
            String8buf calendar_type(128);
            Locale loc = Locale::createFromName(qloc);
            int32_t kvlen = loc.getKeywordValue(
                "calendar", calendar_type.data(), calendar_type.size(), status
            );
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

            status = U_ZERO_ERROR;
            DateFormatSymbols sym(status);
            status = U_ZERO_ERROR;
            if (kvlen == 0)
                sym = DateFormatSymbols(loc, status);
            else
                sym = DateFormatSymbols(loc, calendar_type.data(), status);
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

            if (status == U_USING_DEFAULT_WARNING && qloc) {
                //UErrorCode status2 = U_ZERO_ERROR;
                //const char* valid_locale = sym.getLocale(ULOC_VALID_LOCALE, status2).getBaseName();
                // NOTE! It does not fall back to the "root" locale!
                //if (valid_locale && !strcmp(valid_locale, "root"))
                STRI__DEFERRED_WARNINGS.push(
                    ICUError::getICUerrorName(status)
                );
            }

            int32_t count;
            const UnicodeString* ret;
            std::vector<char> utf8_buffer;
            charport::charvec::Builder output(0);


            // getMonths
            ret = sym.getMonths(count, context_val, width_val); //  (DateFormatSymbols retains ownership.)
            {
                output.reset(count);
                for (int32_t i=0; i<count; ++i)
                    ci::builder_set(output, i, ret[i], utf8_buffer);
                stores.push_back(output.release_store());
            }

            // getWeekdays
            ret = sym.getWeekdays(count, context_val, width_val); //  (DateFormatSymbols retains ownership.)
            if (count > 0 && ret[0].length() == 0) { // this always(?) returns an emty string at the beginning
                --count;
                ++ret;
            }
            {
                output.reset(count);
                for (int32_t i=0; i<count; ++i)
                    ci::builder_set(output, i, ret[i], utf8_buffer);
                stores.push_back(output.release_store());
            }

            // getQuarters
            ret = sym.getQuarters(count, context_val, width_val); //  (DateFormatSymbols retains ownership.)
            {
                output.reset(count);
                for (int32_t i=0; i<count; ++i)
                    ci::builder_set(output, i, ret[i], utf8_buffer);
                stores.push_back(output.release_store());
            }

            // getAmPmStrings
            ret = sym.getAmPmStrings(count); //  (DateFormatSymbols retains ownership.)
            {
                output.reset(count);
                for (int32_t i=0; i<count; ++i)
                    ci::builder_set(output, i, ret[i], utf8_buffer);
                stores.push_back(output.release_store());
            }

            // getEra
            if (width_val == DateFormatSymbols::WIDE)
                ret = sym.getEraNames(count);
            else if (width_val == DateFormatSymbols::ABBREVIATED)
                ret = sym.getEras(count);
            else
                ret = sym.getNarrowEras(count);
            {
                output.reset(count);
                for (int32_t i=0; i<count; ++i)
                    ci::builder_set(output, i, ret[i], utf8_buffer);
                stores.push_back(output.release_store());
            }
        }

//   // getYearNames -- @TODO ICU54 draft
//   ++j;
//   ret = sym.getYearNames(count, context_val, width_val); //  (DateFormatSymbols retains ownership.)
//   SET_VECTOR_ELT(vals, j, Rf_allocVector(STRSXP, count));
//   for (int32_t i=0; i<count; ++i) {
//      std::string out;
//      ret[i].toUTF8String(out);
//      SET_STRING_ELT(VECTOR_ELT(vals, j), i, Rf_mkCharCE(out.c_str(), CE_UTF8));
//   }

//   // getZodiacNames -- @TODO ICU54 draft
//   ++j;
//   ret = sym.getZodiacNames(count, context_val, width_val); //  (DateFormatSymbols retains ownership.)
//   SET_VECTOR_ELT(vals, j, Rf_allocVector(STRSXP, count));
//   for (int32_t i=0; i<count; ++i) {
//      std::string out;
//      ret[i].toUTF8String(out);
//      SET_STRING_ELT(VECTOR_ELT(vals, j), i, Rf_mkCharCE(out.c_str(), CE_UTF8));
//   }


        STRI__PROTECT(vals = charport::unwind_protect([&]() -> SEXP {
            return Rf_allocVector(VECSXP, infosize);
        }));
        for (R_len_t i=0; i<infosize; ++i) {
            SEXP value;
            STRI__PROTECT(value = charport::charvec::wrap(
                std::move(stores[static_cast<size_t>(i)])
            ));
            SET_VECTOR_ELT(vals, i, value);
            STRI__UNPROTECT(1);
        }
        charport::unwind_protect([&]() -> SEXP {
            ci__set_names(
                vals, infosize,
                "Month", "Weekday", "Quarter", "AmPm", "Era"
            );
            return R_NilValue;
        });
    }

    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return vals;
    STRI__ERROR_HANDLER_END({ /* do nothing on error */ })
}
