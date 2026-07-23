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
#include "ci_container_utf8.h"
#include <unicode/strenum.h>
#include <string>
#include <utility>
#include <vector>


/** List available time zone IDs
 *
 * @param offset single numeric
 * @param region single string
 * @return character vector
 *
 * @version 0.5-1 (Marek Gagolewski, 2014-12-24)
 */
SEXP ci_timezone_list(SEXP region, SEXP offset)
{
    StringEnumeration* tz_enum = NULL;
    PROTECT(region = ci__prepare_arg_string_1(region, "region"));
    PROTECT(offset = ci__prepare_arg_double_1(offset, "offset"));

    STRI__ERROR_HANDLER_BEGIN(2)
    SEXP ret;
    {
        ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
        charport::charvec::Store output_store(0, 0);
        std::vector<char> region_c_string;
        const char* r = NULL;
        {
            StriContainerUTF8 region_cont(context, region, 1);
            if (!region_cont.isNA(0)) {
                const String8& region_value = region_cont.get(0);
                const R_len_t region_length = region_value.length();
                region_c_string.reserve(
                    static_cast<size_t>(region_length)+1
                );
                if (region_length > 0) {
                    region_c_string.insert(
                        region_c_string.end(), region_value.data(),
                        region_value.data()+region_length
                    );
                }
                region_c_string.push_back('\0');
                // Deviation from stringi: ICU's
                // createTimeZoneIDEnumeration requires a terminated region
                // code, so own one scalar adapter at this boundary.
                r = region_c_string.data();
            }
        }

        double offset_value = NA_REAL;
        charport::unwind_protect([&]() -> SEXP {
            offset_value = REAL_RO(offset)[0];
            return R_NilValue;
        });
        int32_t offset_hours = 0;
        const int32_t* o = NULL;
        if (!ISNA(offset_value)) {
            // 0.5 and 0.75 are represented exactly within the double type
            offset_hours = (int32_t)(offset_value*1000.0*3600.0);
            o = &offset_hours;
        }

        UErrorCode status = U_ZERO_ERROR;
        tz_enum = TimeZone::createTimeZoneIDEnumeration(
            UCAL_ZONE_TYPE_ANY, r, o, status
        );
        STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

        status = U_ZERO_ERROR;
        tz_enum->reset(status);
        STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

        status = U_ZERO_ERROR;
        R_len_t n = static_cast<R_len_t>(tz_enum->count(status));
        STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

        charport::charvec::Builder output(n);

//   SEXP nam;
//   STRI__PROTECT(nam = Rf_allocVector(STRSXP, n));

        // MG: I reckon that IDs are more readable than DisplayNames (which are moreover localized)
        for (R_len_t i=0; i<n; ++i) {
            int32_t len;
            status = U_ZERO_ERROR;
            const char* cur = tz_enum->next(&len, status);
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
            ci::builder_set(
                output, i, cur, len, cetype_ext_t::CE_ASCII
            );

//      TimeZone* curtz = TimeZone::createTimeZone(UnicodeString::fromUTF8(cur));
//      UnicodeString curdn;
//      curtz->getDisplayName(locale, curdn);
//      delete curtz;
//      string out;
//      curdn.toUTF8String(out);
//      SET_STRING_ELT(nam, i, Rf_mkCharCE(out.c_str(), CE_UTF8));
        }

//   Rf_setAttrib(ret, R_NamesSymbol, nam);

        output_store = output.release_store();

        if (tz_enum) {
            delete tz_enum;
            tz_enum = NULL;
        }
        STRI__PROTECT(ret = charport::charvec::wrap(
            std::move(output_store)
        ));
    }

    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END({
        if (tz_enum) {
            delete tz_enum;
            tz_enum = NULL;
        }
    })
}


///** Get default time zone
// *
// * @return single string
// *
// * @version 0.5-1 (Marek Gagolewski, 2014-12-24)
// */
//SEXP ci_timezone_get() {
//   TimeZone* curtz = ci__prepare_arg_timezone(R_NilValue, "tz", /*allowdefault*/true);
//
//   UnicodeString id;
//   curtz->getID(id);
//   delete curtz;
//
//   std::string id2;
//   id.toUTF8String(id2);
//
//   return Rf_mkString(id2.c_str());
//}


/** Set default time zone
 *
 * @param tz single string
 * @return nothing
 *
 * @version 0.5-1 (Marek Gagolewski, 2014-12-24)
 */
SEXP ci_timezone_set(SEXP tz) {
    if (!Rf_isNull(tz))
        PROTECT(tz = ci__prepare_arg_string_1(tz, "tz"));
    else
        PROTECT(tz);
    TimeZone* curtz = NULL;

    STRI__ERROR_HANDLER_BEGIN(1)
    {
        curtz = ci__prepare_arg_timezone(
            STRI__DEFERRED_WARNINGS, tz, "tz",
            false/*allowdefault*/
        );

        /* This call adopts the TimeZone object passed in;
           the client is no longer responsible for deleting it. */
        TimeZone::adoptDefault(curtz);
        curtz = NULL;
    }

    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return R_NilValue;
    STRI__ERROR_HANDLER_END({
        if (curtz) {
            delete curtz;
            curtz = NULL;
        }
    })
}


/** Get localised time zone info
 *
 * @param tz single string or NULL
 * @param locale single string or NULL
 * @param display_type single string
 * @return list
 *
 * @version 0.5-1 (Marek Gagolewski, 2014-12-24)
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-03-01)
 *    new out: WindowsID, NameDaylight, new in: display_type
 */
SEXP ci_timezone_info(SEXP tz, SEXP locale, SEXP display_type)
{
    if (!Rf_isNull(tz))
        PROTECT(tz = ci__prepare_arg_string_1(tz, "tz"));
    else
        PROTECT(tz);
    TimeZone* curtz = NULL;
    const R_len_t infosize = 6;
    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP vals;
    {
        curtz = ci__prepare_arg_timezone(
            STRI__DEFERRED_WARNINGS, tz, "tz",
            true/*allowdefault*/
        );

        const char* qloc = NULL;
        charport::unwind_protect([&]() -> SEXP {
            qloc = ci__prepare_arg_locale(
                locale, "locale", true, true,
                &STRI__DEFERRED_WARNINGS
            ); /* this is R_alloc'ed */
            return R_NilValue;
        });

        SEXP dtype_arg = R_NilValue;
        STRI__PROTECT(dtype_arg = charport::unwind_protect([&]() -> SEXP {
            return ci__prepare_arg_string_1(
                display_type, "display_type",
                &STRI__DEFERRED_WARNINGS
            );
        }));
        const char* dtype_opts[] = {
            "short", "long", "generic_short", "generic_long",
            "gmt_short", "gmt_long", "common", "generic_location",
            NULL
        };
        int dtype_cur = -1;
        {
            ci::ReaderContext reader_context(STRI__DEFERRED_WARNINGS);
            std::shared_ptr<ci::ReaderBorrow> borrow =
                reader_context.acquire(dtype_arg);
            const charport::StrView value = borrow->view(0);
            if (value.is_na())
                throw StriException(
                    MSG__ARG_EXPECTED_NOT_NA, "display_type"
                );
            dtype_cur = ci__match_arg(
                value.ptr, value.len, dtype_opts
            );
        }

        TimeZone::EDisplayType dtype;
        switch (dtype_cur) {
        case 0:
            dtype = TimeZone::SHORT;
            break;
        case 1:
            dtype = TimeZone::LONG;
            break;
        case 2:
            dtype = TimeZone::SHORT_GENERIC;
            break;
        case 3:
            dtype = TimeZone::LONG_GENERIC;
            break;
        case 4:
            dtype = TimeZone::SHORT_GMT;
            break;
        case 5:
            dtype = TimeZone::LONG_GMT;
            break;
        case 6:
            dtype = TimeZone::SHORT_COMMONLY_USED;
            break;
        case 7:
            dtype = TimeZone::GENERIC_LOCATION;
            break;
        default:
            throw StriException(
                MSG__INCORRECT_MATCH_OPTION, "display_type"
            );
        }

        std::vector<charport::charvec::Store> stores;
        stores.reserve(4);
        double raw_offset = NA_REAL;
        int uses_daylight = NA_LOGICAL;
        {
            std::vector<char> utf8_buffer;

            UnicodeString val_ID;
            curtz->getID(val_ID);
            {
                int32_t utf8_length = 0;
                const char* utf8 = ci::unicode_to_utf8(
                    val_ID, utf8_buffer, utf8_length
                );
                stores.push_back(ci::scalar_store(
                    utf8, utf8_length,
                    cetype_ext_t::CE_ASCII
                ));
            }

            UnicodeString val_name;
            curtz->getDisplayName(
                false, dtype, Locale::createFromName(qloc), val_name
            );
            stores.push_back(ci::scalar_store(val_name, utf8_buffer));

            // TODO: U_USING_DEFAULT_WARNING when qloc!=0
            // TODO: If the display name is not available for the locale,
            // then getDisplayName returns a string in the localised GMT offset format
            // such as GMT[+-]HH:mm. -- we can't check+warn if it is a valid locale
            // otherwise other than by comparing the output to this pattern

            {
                if ((bool)curtz->useDaylightTime()) {
                    UnicodeString val_name2;
                    curtz->getDisplayName(
                        true, dtype, Locale::createFromName(qloc), val_name2
                    );
                    stores.push_back(ci::scalar_store(
                        val_name2, utf8_buffer
                    ));
                }
                else {
                    stores.push_back(charport::charvec::Store::scalar(
                        NULL, 0, cetype_ext_t::CE_NA
                    ));
                }
            }

            UnicodeString val_windows;
            UErrorCode status = U_ZERO_ERROR;
#if U_ICU_VERSION_MAJOR_NUM>=52
            TimeZone::getWindowsID(
                val_ID, val_windows, status
            ); // Stable since ICU 52
#endif
            {
                if (U_SUCCESS(status) && val_windows.length() > 0) {
                    int32_t utf8_length = 0;
                    const char* utf8 = ci::unicode_to_utf8(
                        val_windows, utf8_buffer, utf8_length
                    );
                    stores.push_back(ci::scalar_store(
                        utf8, utf8_length,
                        cetype_ext_t::CE_ASCII
                    ));
                }
                else
                    stores.push_back(charport::charvec::Store::scalar(
                        NULL, 0, cetype_ext_t::CE_NA
                    ));
            }

            raw_offset = curtz->getRawOffset()/1000.0/3600.0;
            uses_daylight = (bool)curtz->useDaylightTime();
        }

        delete curtz;
        curtz = NULL;

        STRI__PROTECT(vals = charport::unwind_protect([&]() -> SEXP {
            return Rf_allocVector(VECSXP, infosize);
        }));
        for (R_len_t i=0; i<4; ++i) {
            SEXP value;
            STRI__PROTECT(value = charport::charvec::wrap(
                std::move(stores[static_cast<size_t>(i)])
            ));
            SET_VECTOR_ELT(vals, i, value);
            STRI__UNPROTECT(1);
        }
        charport::unwind_protect([&]() -> SEXP {
            SET_VECTOR_ELT(vals, 4, Rf_ScalarReal(raw_offset));
            SET_VECTOR_ELT(
                vals, 5, Rf_ScalarLogical(uses_daylight)
            );
            ci__set_names(
                vals, infosize,
                "ID", "Name", "Name.Daylight", "Name.Windows",
                "RawOffset", "UsesDaylightTime"
            );
            return R_NilValue;
        });
    }

    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return vals;
    STRI__ERROR_HANDLER_END({
        if (curtz) {
            delete curtz;
            curtz = NULL;
        }
    })
}
