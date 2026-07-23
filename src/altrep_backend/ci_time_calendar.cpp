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
#include "ci_container_utf8.h"
#include "ci_container_double.h"
#include "ci_container_integer.h"
#include <unicode/calendar.h>
#include <unicode/gregocal.h>
#include <memory>


/** Set POSIXct class on a given object
 *
 * @param x R object
 *
 * @version 0.5-1 (Marek Gagolewski, 2014-12-29)
 */
void ci__set_class_POSIXct(SEXP x) {
    SEXP cl;
    PROTECT(cl = Rf_allocVector(STRSXP, 2));
    // SET_STRING_ELT(cl, 0, Rf_mkChar("POSIXst"));
    SET_STRING_ELT(cl, 0, Rf_mkChar("POSIXct"));
    SET_STRING_ELT(cl, 1, Rf_mkChar("POSIXt"));
    Rf_setAttrib(x, R_ClassSymbol, cl);
    UNPROTECT(1);
}


/** Get current date-time
 *
 * @return POSIXct
 *
 * @version 0.5-1 (Marek Gagolewski, 2014-12-29)
 */
SEXP ci_datetime_now()
{
    UDate now = Calendar::getNow();
    SEXP ret;
    PROTECT(ret = Rf_ScalarReal(((double)now)/1000.0)); // msec.->sec.
    ci__set_class_POSIXct(ret);
    UNPROTECT(1);
    return ret;
}


/** Get calendar
 *
 * @return Calendar
 *
 * @version 1.8.1 (Marek Gagolewski, 2023-11-07)
 */
Calendar* ci__get_calendar(
    const char* locale_val, ci::DeferredWarnings& warnings
)
{
    UErrorCode status = U_ZERO_ERROR;
    // Deviation from stringi: retain partial Calendar ownership under RAII
    // through ICU status checks and warning staging, then transfer it only
    // after every local failure path is complete.
    std::unique_ptr<Calendar> cal(
        Calendar::createInstance(Locale::createFromName(locale_val), status)
    );
    STRI__CHECKICUSTATUS_THROW(status, {/* cal is released by RAII */})

    // NOTE: unfortunately, in ICU 74.1 U_USING_DEFAULT_WARNING is never emitted
    if (status == U_USING_DEFAULT_WARNING && cal && locale_val) {
        UErrorCode status2 = U_ZERO_ERROR;
        const char* valid_locale = cal->getLocaleID(ULOC_VALID_LOCALE, status2);
        if (valid_locale && !strcmp(valid_locale, "root")) {
            // Deviation from stringi: queue locale fallback while the Calendar
            // is live; the entry point emits it after releasing ICU state.
            warnings.push(ICUError::getICUerrorName(status));
        }
    }

    return cal.release();
}



/** Date-time arithmetic
 *
 * @param time
 * @param value
 * @param units
 * @param tz
 * @param locale
 *
 * @return POSIXct
 *
 * @version 0.5-1 (Marek Gagolewski, 2014-12-30)
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-03-06) tz arg added
 *
 * @version 1.8.1 (Marek Gagolewski, 2023-11-07)
 *     #476: Warn when falling back to the root locale, make C==en_US_POSIX
 */
SEXP ci_datetime_add(SEXP time, SEXP value, SEXP units, SEXP tz, SEXP locale)
{
    PROTECT(time = ci__prepare_arg_POSIXct(time, "time"));
    PROTECT(value = ci__prepare_arg_integer(value, "value"));
    if (!Rf_isNull(tz)) PROTECT(tz = ci__prepare_arg_string_1(tz, "tz"));
    else             PROTECT(tz); /* needed to set tzone attrib */

    R_len_t vectorize_length = ci__recycling_rule(true, 2, LENGTH(time), LENGTH(value));

    PROTECT(units = ci__prepare_arg_string_1(units, "units"));

    TimeZone* tz_val = NULL;
    Calendar* cal = NULL;
    STRI__ERROR_HANDLER_BEGIN(4)
    SEXP ret;
    {
        ci::ReaderContext reader_context(STRI__DEFERRED_WARNINGS);
        const char* units_opts[] = {
            "years", "months", "weeks", "days", "hours", "minutes",
            "seconds", "milliseconds", NULL
        };
        int units_cur = -1;
        {
            std::shared_ptr<ci::ReaderBorrow> borrow =
                reader_context.acquire(units);
            const charport::StrView units_value = borrow->view(0);
            if (units_value.is_na())
                throw StriException(MSG__ARG_EXPECTED_NOT_NA, "units");
            units_cur = ci__match_arg(
                units_value.ptr, units_value.len, units_opts
            );
        }

        const char* locale_val = NULL;
        charport::unwind_protect([&]() -> SEXP {
            locale_val = ci__prepare_arg_locale(
                locale, "locale", true, true,
                &STRI__DEFERRED_WARNINGS
            );
            return R_NilValue;
        });

        tz_val = ci__prepare_arg_timezone(
            STRI__DEFERRED_WARNINGS, tz, "tz", true/*allowdefault*/
        );
        StriContainerDouble time_cont(time, vectorize_length);
        StriContainerInteger value_cont(value, vectorize_length);

        UCalendarDateFields units_field;
        switch (units_cur) {
        case 0:
            units_field = UCAL_YEAR;
            break;
        case 1:
            units_field = UCAL_MONTH;
            break;
        case 2:
            units_field = UCAL_WEEK_OF_YEAR;
            break;
        case 3:
            units_field = UCAL_DAY_OF_MONTH;
            break;
        case 4:
            units_field = UCAL_HOUR_OF_DAY;
            break;
        case 5:
            units_field = UCAL_MINUTE;
            break;
        case 6:
            units_field = UCAL_SECOND;
            break;
        case 7:
            units_field = UCAL_MILLISECOND;
            break;
        default:
            throw StriException(MSG__INCORRECT_MATCH_OPTION, "units");
        }


        cal = ci__get_calendar(locale_val, STRI__DEFERRED_WARNINGS);

        cal->adoptTimeZone(tz_val);
        tz_val = NULL; /* The Calendar takes ownership of the TimeZone. */

        UErrorCode status = U_ZERO_ERROR;
        STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
            return Rf_allocVector(REALSXP, vectorize_length);
        }));
        double* ret_val = REAL(ret);
        for (R_len_t i=0; i<vectorize_length; ++i) {
            if (time_cont.isNA(i) || value_cont.isNA(i)) {
                ret_val[i] = NA_REAL;
                continue;
            }
            status = U_ZERO_ERROR;
            cal->setTime((UDate)(time_cont.get(i)*1000.0), status);
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

            status = U_ZERO_ERROR;
            cal->add(units_field, value_cont.get(i), status);
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

            status = U_ZERO_ERROR;
            ret_val[i] = ((double)cal->getTime(status))/1000.0;
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
        }

        if (tz_val) {
            delete tz_val;
            tz_val = NULL;
        }
        if (cal) {
            delete cal;
            cal = NULL;
        }
        // Deviation from stringi: release ICU state before attribute creation
        // can signal into R.
        charport::unwind_protect([&]() -> SEXP {
            if (!Rf_isNull(tz))
                Rf_setAttrib(ret, Rf_ScalarString(Rf_mkChar("tzone")), tz);
            ci__set_class_POSIXct(ret);
            return R_NilValue;
        });
    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END({
        if (tz_val) {
            delete tz_val;
            tz_val = NULL;
        }
        if (cal) {
            delete cal;
            cal = NULL;
        }
    })
}


/**
 * Get values of date-time fields
 *
 * @param time
 * @param locale
 * @param tz
 *
 * @return list
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-01-01)
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-03-03) tz arg added
 *
 * @version 1.8.1 (Marek Gagolewski, 2023-11-07)
 *     #476: Warn when falling back to the root locale, make C==en_US_POSIX
 */
SEXP ci_datetime_fields(SEXP time, SEXP tz, SEXP locale)
{
    PROTECT(time = ci__prepare_arg_POSIXct(time, "time"));
    const char* locale_val = ci__prepare_arg_locale(locale, "locale");
    if (!Rf_isNull(tz)) PROTECT(tz = ci__prepare_arg_string_1(tz, "tz"));
    else             PROTECT(tz); /* needed to set tzone attrib */

    TimeZone* tz_val = NULL;
    Calendar* cal = NULL;
    STRI__ERROR_HANDLER_BEGIN(2)
    R_len_t vectorize_length = LENGTH(time);
    SEXP ret;
#define STRI__FIELDS_NUM 14
    {
        tz_val = ci__prepare_arg_timezone(
            STRI__DEFERRED_WARNINGS, tz, "tz", true/*allowdefault*/
        );
        StriContainerDouble time_cont(time, vectorize_length);

        cal = ci__get_calendar(locale_val, STRI__DEFERRED_WARNINGS);

        cal->adoptTimeZone(tz_val);
        tz_val = NULL; /* The Calendar takes ownership of the TimeZone. */

        STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
            SEXP output = PROTECT(
                Rf_allocVector(VECSXP, STRI__FIELDS_NUM)
            );
            for (R_len_t j=0; j<STRI__FIELDS_NUM; ++j)
                SET_VECTOR_ELT(
                    output, j, Rf_allocVector(INTSXP, vectorize_length)
                );
            UNPROTECT(1);
            return output;
        }));

        UErrorCode status = U_ZERO_ERROR;
        for (R_len_t i=0; i<vectorize_length; ++i) {
            if (time_cont.isNA(i)) {
                for (R_len_t j=0; j<STRI__FIELDS_NUM; ++j)
                    INTEGER(VECTOR_ELT(ret, j))[i] = NA_INTEGER;
                continue;
            }

            status = U_ZERO_ERROR;
            cal->setTime((UDate)(time_cont.get(i)*1000.0), status);
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

            for (R_len_t j=0; j<STRI__FIELDS_NUM; ++j) {
                UCalendarDateFields units_field;
                switch (j) {
                case 0:
                    units_field = UCAL_EXTENDED_YEAR;
                    break;
                case 1:
                    units_field = UCAL_MONTH;
                    break;
                case 2:
                    units_field = UCAL_DAY_OF_MONTH;
                    break;
                case 3:
                    units_field = UCAL_HOUR_OF_DAY;
                    break;
                case 4:
                    units_field = UCAL_MINUTE;
                    break;
                case 5:
                    units_field = UCAL_SECOND;
                    break;
                case 6:
                    units_field = UCAL_MILLISECOND;
                    break;
                case 7:
                    units_field = UCAL_WEEK_OF_YEAR;
                    break;
                case 8:
                    units_field = UCAL_WEEK_OF_MONTH;
                    break;
                case 9:
                    units_field = UCAL_DAY_OF_YEAR;
                    break;
                case 10:
                    units_field = UCAL_DAY_OF_WEEK;
                    break;
                case 11:
                    units_field = UCAL_HOUR;
                    break;
                case 12:
                    units_field = UCAL_AM_PM;
                    break;
                case 13:
                    units_field = UCAL_ERA;
                    break;
                default:
                    throw StriException(
                        MSG__INCORRECT_MATCH_OPTION, "units"
                    );
                }
                //UCAL_IS_LEAP_MONTH
                //UCAL_MILLISECONDS_IN_DAY -> SecondsInDay

                // UCAL_AM_PM -> "AM" or "PM" (localized? or factor?+index in ci_datetime_symbols) add arg use_symbols????
                // UCAL_DAY_OF_WEEK -> (localized? or factor?) SUNDAY, MONDAY
                // UCAL_DAY_OF_YEAR '

                // isWekend

                status = U_ZERO_ERROR;
                INTEGER(VECTOR_ELT(ret, j))[i] = cal->get(units_field, status);
                STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

                if (units_field == UCAL_MONTH)      ++INTEGER(VECTOR_ELT(ret, j))[i]; // month + 1
                else if (units_field == UCAL_AM_PM) ++INTEGER(VECTOR_ELT(ret, j))[i]; // ampm + 1
                else if (units_field == UCAL_ERA)   ++INTEGER(VECTOR_ELT(ret, j))[i]; // era + 1
            }
        }

        if (tz_val) {
            delete tz_val;
            tz_val = NULL;
        }
        if (cal) {
            delete cal;
            cal = NULL;
        }
        // Deviation from stringi: release ICU state before name allocation
        // can signal into R.
        charport::unwind_protect([&]() -> SEXP {
            ci__set_names(ret, STRI__FIELDS_NUM,
                "Year", "Month", "Day", "Hour", "Minute", "Second", "Millisecond",
                "WeekOfYear", "WeekOfMonth", "DayOfYear", "DayOfWeek", "Hour12", "AmPm", "Era"
            );
            return R_NilValue;
        });
    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END({
        if (tz_val) {
            delete tz_val;
            tz_val = NULL;
        }
        if (cal) {
            delete cal;
            cal = NULL;
        }
    })
}


/**
 * Create a date-time object
 *
 * @param year
 * @param month
 * @param day
 * @param hours
 * @param minutes
 * @param seconds
 * @param tz
 * @param lenient
 * @param locale
 *
 * @return POSIXct
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-01-01)
 * @version 0.5-1 (Marek Gagolewski, 2015-01-11) lenient arg added
 * @version 0.5-1 (Marek Gagolewski, 2015-03-02) tz arg added
 * @version 1.1.2 (Marek Gagolewski, 2016-09-30) round() is not C++98
 *
 * @version 1.8.1 (Marek Gagolewski, 2023-11-07)
 *     #476: Warn when falling back to the root locale, make C==en_US_POSIX
 */
SEXP ci_datetime_create(
    SEXP year, SEXP month, SEXP day, SEXP hour,
    SEXP minute, SEXP second, SEXP lenient, SEXP tz, SEXP locale
) {
    PROTECT(year = ci__prepare_arg_integer(year, "year"));
    PROTECT(month = ci__prepare_arg_integer(month, "month"));
    PROTECT(day = ci__prepare_arg_integer(day, "day"));
    PROTECT(hour = ci__prepare_arg_integer(hour, "hour"));
    PROTECT(minute = ci__prepare_arg_integer(minute, "minute"));
    PROTECT(second = ci__prepare_arg_double(second, "second"));
    const char* locale_val = ci__prepare_arg_locale(locale, "locale");
    bool lenient_val = ci__prepare_arg_logical_1_notNA(lenient, "lenient");
    if (!Rf_isNull(tz)) PROTECT(tz = ci__prepare_arg_string_1(tz, "tz"));
    else             PROTECT(tz); /* needed to set tzone attrib */

    R_len_t vectorize_length = ci__recycling_rule(true, 6,
                               LENGTH(year), LENGTH(month), LENGTH(day),
                               LENGTH(hour), LENGTH(minute), LENGTH(second));

    TimeZone* tz_val = NULL;
    Calendar* cal = NULL;
    STRI__ERROR_HANDLER_BEGIN(7)
    SEXP ret;
    {
        tz_val = ci__prepare_arg_timezone(
            STRI__DEFERRED_WARNINGS, tz, "tz", true/*allowdefault*/
        );
        StriContainerInteger year_cont(year, vectorize_length);
        StriContainerInteger month_cont(month, vectorize_length);
        StriContainerInteger day_cont(day, vectorize_length);
        StriContainerInteger hour_cont(hour, vectorize_length);
        StriContainerInteger minute_cont(minute, vectorize_length);
        StriContainerDouble second_cont(second, vectorize_length);

        cal = ci__get_calendar(locale_val, STRI__DEFERRED_WARNINGS);

        cal->setLenient(lenient_val);

        cal->adoptTimeZone(tz_val);
        tz_val = NULL; /* The Calendar takes ownership of the TimeZone. */

        UErrorCode status = U_ZERO_ERROR;
        STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
            return Rf_allocVector(REALSXP, vectorize_length);
        }));
        double* ret_val = REAL(ret);
        for (R_len_t i=0; i<vectorize_length; ++i) {
            if (year_cont.isNA(i) || month_cont.isNA(i)  || day_cont.isNA(i) ||
                    hour_cont.isNA(i) || minute_cont.isNA(i) || second_cont.isNA(i)) {
                ret_val[i] = NA_REAL;
                continue;
            }

            cal->set(UCAL_EXTENDED_YEAR, year_cont.get(i));
            cal->set(UCAL_MONTH, month_cont.get(i)-1);
            cal->set(UCAL_DATE, day_cont.get(i));
            cal->set(UCAL_HOUR_OF_DAY, hour_cont.get(i));
            cal->set(UCAL_MINUTE, minute_cont.get(i));
            cal->set(UCAL_SECOND, (int)floor(second_cont.get(i)));
            cal->set(UCAL_MILLISECOND, (int)fround((second_cont.get(i)-floor(second_cont.get(i)))*1000.0, 0));

            status = U_ZERO_ERROR;
            ret_val[i] = ((double)cal->getTime(status))/1000.0;
            if (U_FAILURE(status)) ret_val[i] = NA_REAL;
        }

        if (tz_val) {
            delete tz_val;
            tz_val = NULL;
        }
        if (cal) {
            delete cal;
            cal = NULL;
        }
        // Deviation from stringi: release ICU state before attribute creation
        // can signal into R.
        charport::unwind_protect([&]() -> SEXP {
            if (!Rf_isNull(tz))
                Rf_setAttrib(ret, Rf_ScalarString(Rf_mkChar("tzone")), tz);
            ci__set_class_POSIXct(ret);
            return R_NilValue;
        });
    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END({
        if (tz_val) {
            delete tz_val;
            tz_val = NULL;
        }
        if (cal) {
            delete cal;
            cal = NULL;
        }
    })
}


// /**
//  * @param x list
//  * @return POSIXct
//  *
//  * @version 0.5-1 (Marek Gagolewski, 2015-03-07)
//  */
// SEXP ci_c_posixst(SEXP x) {
//     if (!Rf_isVectorList(x)) Rf_error(MSG__INTERNAL_ERROR);
//     R_len_t n = LENGTH(x);
//     R_len_t m = 0;
//     for (R_len_t i=0; i<n; ++i) {
//         SET_VECTOR_ELT(x, i, ci__prepare_arg_POSIXct(VECTOR_ELT(x, i), "..."));
//         m += LENGTH(VECTOR_ELT(x, i));
//     }
//     SEXP ret;
//     PROTECT(ret = Rf_allocVector(REALSXP, m));
//     double* ret_val = REAL(ret);
//     R_len_t k = 0;
//     for (R_len_t i=0; i<n; ++i) {
//         R_len_t ni = LENGTH(VECTOR_ELT(x, i));
//         double* xi_val = REAL(VECTOR_ELT(x, i));
//         for (R_len_t j=0; j<ni; ++j)
//             ret_val[k++] = xi_val[j];
//     }
//
//     // @TODO: tz?
//     ci__set_class_POSIXct(ret);
//     UNPROTECT(1);
//     return ret;
// }
