
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

#include <cstddef>
#include <cstring>
#include <unicode/uloc.h>

namespace charr { namespace altrep_backend {

namespace prepare_arg {

// for R_tryCatchError -------------------------------------------------------

CHARR_R_HELPER SEXP call_as_character_r(void* data) noexcept
{
    SEXP call;
    SEXP x = static_cast<SEXP>(data);
    PROTECT(call = Rf_lang2(Rf_install("as.character"), x));
    PROTECT(x = Rf_eval(call, R_BaseEnv));  // Q: BaseEnv has the generic as.*
    UNPROTECT(2);
    return x;
}


CHARR_R_HELPER SEXP call_as_integer_r(void* data) noexcept
{
    SEXP call;
    SEXP x = static_cast<SEXP>(data);
    PROTECT(call = Rf_lang2(Rf_install("as.integer"), x));
    PROTECT(x = Rf_eval(call, R_BaseEnv));  // Q: BaseEnv has the generic as.*
    UNPROTECT(2);
    return x;
}


CHARR_R_HELPER SEXP call_as_double_r(void* data) noexcept
{
    SEXP call;
    SEXP x = static_cast<SEXP>(data);
    PROTECT(call = Rf_lang2(Rf_install("as.double"), x));
    PROTECT(x = Rf_eval(call, R_BaseEnv));  // Q: BaseEnv has the generic as.*
    UNPROTECT(2);
    return x;
}


CHARR_R_HELPER SEXP call_as_logical_r(void* data) noexcept
{
    SEXP call;
    SEXP x = static_cast<SEXP>(data);
    PROTECT(call = Rf_lang2(Rf_install("as.logical"), x));
    PROTECT(x = Rf_eval(call, R_BaseEnv));  // Q: BaseEnv has the generic as.*
    UNPROTECT(2);
    return x;
}


CHARR_NEUTRAL_HELPER SEXP handler_null(
    SEXP /*cond*/, void* /*data*/
) noexcept
{
    return R_NilValue;
}


/** check if a list is empty or is a list of atomic vectors each of length 1 */
CHARR_R_HELPER bool check_list_of_scalars(SEXP x) noexcept
{
    const R_len_t size = LENGTH(x);
    for (R_len_t i = 0; i < size; ++i) {
        SEXP current = VECTOR_ELT(x, i);
        if (!(Rf_isVectorAtomic(current) && LENGTH(current) == 1))
            return false;
    }
    return true;
}


CHARR_R_HELPER SEXP prepare_double_r(
    SEXP x, const char* argname, bool factors_as_strings,
    bool allow_error
) noexcept
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    if (factors_as_strings && Rf_isFactor(x)) {
        if (allow_error)
            PROTECT(x = call_as_character_r(static_cast<void*>(x)));
        else {
            PROTECT(x = R_tryCatchError(
                call_as_character_r, static_cast<void*>(x),
                handler_null, NULL
            ));
            if (Rf_isNull(x)) {
                UNPROTECT(1);
                return x;
            }
        }
        PROTECT(x = Rf_coerceVector(x, REALSXP));
        UNPROTECT(2);
        return x;
    }

    if (Rf_isVectorList(x) || Rf_isObject(x)) {
        if (Rf_isVectorList(x) && !check_list_of_scalars(x))
            Rf_warning(MSG__WARN_LIST_COERCION);

        if (allow_error)
            return call_as_double_r(static_cast<void*>(x));
        return R_tryCatchError(
            call_as_double_r, static_cast<void*>(x), handler_null, NULL
        );
    }

    if ((bool)Rf_isReal(x))
        return x;
    if (Rf_isVectorAtomic(x) || Rf_isNull(x))
        return Rf_coerceVector(x, REALSXP);

    Rf_error(MSG__ARG_EXPECTED_NUMERIC, argname);
    return x;
}


/**
 * Check if we are dealing with the 'C' locale (it should be resolved to
 * en_US_POSIX)
 *
 * "C", "c", "C.UTF-8", "c.UTF-8", "C.any_other_encoding", etc.
 */
CHARR_NEUTRAL_HELPER bool is_c_locale(const char* str) noexcept
{
    return str && ((str[0] == 'C' || str[0] == 'c')
        && (str[1] == '\0' || str[1] == '.'));
}


CHARR_R_HELPER const char* prepare_locale_value_r(
    SEXP loc,
    const char* argname,
    const char* default_locale,
    bool allowdefault
) noexcept
{
    if (STRING_ELT(loc, 0) == NA_STRING) {
        UNPROTECT(1);
        Rf_error(MSG__ARG_EXPECTED_NOT_NA, argname);
    }

    const char* requested = CHAR(STRING_ELT(loc, 0));
    if (requested[0] == '\0') {
        UNPROTECT(1);
        if (allowdefault)
            return default_locale;
        Rf_error(MSG__LOCALE_INCORRECT_ID);
    }

    UErrorCode status = U_ZERO_ERROR;
    char canonical[ULOC_FULLNAME_CAPACITY];
    uloc_canonicalize(
        requested, canonical, ULOC_FULLNAME_CAPACITY, &status
    );
    UNPROTECT(1);
    STRI__CHECKICUSTATUS_RFERROR(status, {;})

    R_len_t length = std::strlen(canonical);
    char* result = static_cast<char*>(R_alloc(length+1, sizeof(char)));
    std::memcpy(result, canonical, length+1);

    while (length > 0 &&
            (result[length-1] == ' ' || result[length-1] == '\t' ||
             result[length-1] == '\n' || result[length-1] == '\r')) {
        result[--length] = '\0';
    }

    while (result[0] == ' ' || result[0] == '\t' ||
            result[0] == '\n' || result[0] == '\r') {
        ++result;
        --length;
    }

    if (length == 0) {
        if (allowdefault)
            return default_locale;
        Rf_error(MSG__LOCALE_INCORRECT_ID);
    }

    if (is_c_locale(result))
        return "en_US_POSIX";

    if (result[0] == ULOC_KEYWORD_SEPARATOR) {
        if (!allowdefault)
            Rf_error(MSG__LOCALE_INCORRECT_ID);

        const char* prefix = default_locale;
        if (prefix == nullptr) {
            prefix = uloc_getDefault();
            if (is_c_locale(prefix))
                prefix = "en_US_POSIX";
        }
        const R_len_t prefix_length = std::strlen(prefix);
        const char* keywords = result;
        result = static_cast<char*>(R_alloc(
            prefix_length+length+1, sizeof(char)
        ));
        std::memcpy(result, prefix, prefix_length);
        std::memcpy(result+prefix_length, keywords, length+1);
    }

    return result;
}

} // namespace prepare_arg

using namespace prepare_arg;


/** Prepare list argument */
CHARR_R_HELPER SEXP ci__prepare_arg_list_r(
    SEXP x, const char* argname
) noexcept
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    if (!Rf_isNull(x) && !Rf_isVectorList(x))
        Rf_error(MSG__ARG_EXPECTED_LIST, argname);

    return x;
}


/**
 * Prepare list of raw vectors argument, single raw vector,
 * or character vector argument
 */
CHARR_R_HELPER SEXP ci__prepare_arg_list_raw_r(
    SEXP x, const char* argname
) noexcept
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    if (Rf_isNull(x) || isRaw(x))
        return x;

    if (Rf_isVectorList(x)) {
        const R_len_t size = LENGTH(x);
        for (R_len_t i = 0; i < size; ++i) {
            SEXP current = VECTOR_ELT(x, i);
            if ((bool)Rf_isNull(current))
                continue;
            if (!isRaw(current))
                Rf_error(
                    MSG__ARG_EXPECTED_RAW_IN_LIST_NO_COERCION, argname
                );
        }
        return x;
    }

    return ci__prepare_arg_string_r(x, argname);
}


/** Prepare list of character vectors argument */
CHARR_R_HELPER SEXP ci__prepare_arg_list_string_r(
    SEXP x, const char* argname
) noexcept
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    if (!Rf_isVectorList(x))
        Rf_error(MSG__ARG_EXPECTED_LIST_STRING, argname);

    const R_len_t size = LENGTH(x);
    if (size <= 0)
        return x;

    if (MAYBE_REFERENCED(x)) {
        SEXP source = x;
        PROTECT(x = Rf_allocVector(VECSXP, size));
        for (R_len_t i = 0; i < size; ++i) {
            SET_VECTOR_ELT(
                x, i,
                ci__prepare_arg_string_r(
                    VECTOR_ELT(source, i), argname
                )
            );
        }
        UNPROTECT(1);
        return x;
    }

    for (R_len_t i = 0; i < size; ++i) {
        SET_VECTOR_ELT(
            x, i,
            ci__prepare_arg_string_r(VECTOR_ELT(x, i), argname)
        );
    }
    return x;
}


/** Prepare character vector argument */
CHARR_R_HELPER SEXP ci__prepare_arg_string_r(
    SEXP x, const char* argname, bool allow_error
) noexcept
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    if (Rf_isVectorList(x) || Rf_isObject(x)) {
        if (Rf_isVectorList(x) && !check_list_of_scalars(x))
            Rf_warning(MSG__WARN_LIST_COERCION);

        if (allow_error)
            return call_as_character_r(static_cast<void*>(x));
        return R_tryCatchError(
            call_as_character_r, static_cast<void*>(x), handler_null, NULL
        );
    }

    if ((bool)Rf_isString(x))
        return x;
    if (Rf_isVectorAtomic(x) || Rf_isNull(x))
        return Rf_coerceVector(x, STRSXP);
    if ((bool)Rf_isSymbol(x))
        return Rf_ScalarString(PRINTNAME(x));

    Rf_error(MSG__ARG_EXPECTED_STRING, argname);
    return x;
}


/** Prepare integer vector argument */
CHARR_R_HELPER SEXP ci__prepare_arg_integer_r(
    SEXP x, const char* argname, bool factors_as_strings,
    bool allow_error
) noexcept
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    if (factors_as_strings && Rf_isFactor(x)) {
        if (allow_error)
            PROTECT(x = call_as_character_r(static_cast<void*>(x)));
        else {
            PROTECT(x = R_tryCatchError(
                call_as_character_r, static_cast<void*>(x),
                handler_null, NULL
            ));
            if (Rf_isNull(x)) {
                UNPROTECT(1);
                return x;
            }
        }
        PROTECT(x = Rf_coerceVector(x, INTSXP));
        UNPROTECT(2);
        return x;
    }

    if (Rf_isVectorList(x) || Rf_isObject(x)) {
        if (Rf_isVectorList(x) && !check_list_of_scalars(x))
            Rf_warning(MSG__WARN_LIST_COERCION);

        if (allow_error)
            return call_as_integer_r(static_cast<void*>(x));
        return R_tryCatchError(
            call_as_integer_r, static_cast<void*>(x), handler_null, NULL
        );
    }

    if (Rf_isInteger(x))
        return x;
    if (Rf_isVectorAtomic(x) || Rf_isNull(x))
        return Rf_coerceVector(x, INTSXP);

    Rf_error(MSG__ARG_EXPECTED_INTEGER, argname);
    return x;
}


/** Prepare logical vector argument */
CHARR_R_HELPER SEXP ci__prepare_arg_logical_r(
    SEXP x, const char* argname, bool allow_error
) noexcept
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    if (Rf_isFactor(x)) {
        if (allow_error)
            return call_as_logical_r(static_cast<void*>(x));
        return R_tryCatchError(
            call_as_logical_r, static_cast<void*>(x), handler_null, NULL
        );
    }

    if (Rf_isVectorList(x) || Rf_isObject(x)) {
        if (Rf_isVectorList(x) && !check_list_of_scalars(x))
            Rf_warning(MSG__WARN_LIST_COERCION);

        if (allow_error)
            return call_as_logical_r(static_cast<void*>(x));
        return R_tryCatchError(
            call_as_logical_r, static_cast<void*>(x), handler_null, NULL
        );
    }

    if ((bool)Rf_isLogical(x))
        return x;
    if (Rf_isVectorAtomic(x) || Rf_isNull(x))
        return Rf_coerceVector(x, LGLSXP);

    Rf_error(MSG__ARG_EXPECTED_LOGICAL, argname);
    return x;
}


/** Prepare string argument - one string */
CHARR_R_HELPER SEXP ci__prepare_arg_string_1_r(
    SEXP x, const char* argname
) noexcept
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    PROTECT(x = ci__prepare_arg_string_r(x, argname));
    int nprotect = 1;
    const R_len_t size = LENGTH(x);

    if (size <= 0) {
        UNPROTECT(nprotect);
        Rf_error(MSG__ARG_EXPECTED_NOT_EMPTY, argname);
    }

    if (size > 1) {
        Rf_warning(MSG__ARG_EXPECTED_1_STRING, argname);
        SEXP source = x;
        PROTECT(x = Rf_allocVector(STRSXP, 1));
        ++nprotect;
        SET_STRING_ELT(x, 0, STRING_ELT(source, 0));
    }

    UNPROTECT(nprotect);
    return x;
}


/** Prepare logical argument - one value */
CHARR_R_HELPER SEXP ci__prepare_arg_logical_1_r(
    SEXP x, const char* argname
) noexcept
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    PROTECT(x = ci__prepare_arg_logical_r(x, argname));
    int nprotect = 1;
    const R_len_t size = LENGTH(x);

    if (size <= 0) {
        UNPROTECT(nprotect);
        Rf_error(MSG__ARG_EXPECTED_NOT_EMPTY, argname);
    }

    if (size > 1) {
        Rf_warning(MSG__ARG_EXPECTED_1_LOGICAL, argname);
        const int value = LOGICAL_RO(x)[0];
        PROTECT(x = Rf_allocVector(LGLSXP, 1));
        ++nprotect;
        LOGICAL(x)[0] = value;
    }

    UNPROTECT(nprotect);
    return x;
}


/** Prepare logical argument - one value, not NA */
CHARR_R_HELPER bool ci__prepare_arg_logical_1_notNA_r(
    SEXP x, const char* argname
) noexcept
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    PROTECT(x = ci__prepare_arg_logical_1_r(x, argname));
    const int value = LOGICAL_RO(x)[0];
    UNPROTECT(1);
    if (value == NA_LOGICAL)
        Rf_error(MSG__ARG_EXPECTED_NOT_NA, argname);
    return (bool)value;
}


/** Prepare logical argument - one value, can be NA */
CHARR_R_HELPER int ci__prepare_arg_logical_1_NA_r(
    SEXP x, const char* argname
) noexcept
{
    PROTECT(x = ci__prepare_arg_logical_1_r(x, argname));
    const int value = LOGICAL_RO(x)[0];
    UNPROTECT(1);
    return value;
}


/** Prepare integer argument - one value, not NA */
CHARR_R_HELPER int ci__prepare_arg_integer_1_notNA_r(
    SEXP x, const char* argname
) noexcept
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    PROTECT(x = ci__prepare_arg_integer_r(x, argname));
    const R_len_t size = LENGTH(x);
    if (size <= 0) {
        UNPROTECT(1);
        Rf_error(MSG__ARG_EXPECTED_NOT_EMPTY, argname);
    }
    if (size > 1)
        Rf_warning(MSG__ARG_EXPECTED_1_INTEGER, argname);

    const int value = INTEGER_RO(x)[0];
    UNPROTECT(1);
    if (value == NA_INTEGER)
        Rf_error(MSG__ARG_EXPECTED_NOT_NA, argname);
    return value;
}


/** Prepare double argument - one value, not NA */
CHARR_R_HELPER double ci__prepare_arg_double_1_notNA_r(
    SEXP x, const char* argname
) noexcept
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    PROTECT(x = prepare_double_r(x, argname, true, true));
    const R_len_t size = LENGTH(x);
    if (size <= 0) {
        UNPROTECT(1);
        Rf_error(MSG__ARG_EXPECTED_NOT_EMPTY, argname);
    }
    if (size > 1)
        Rf_warning(MSG__ARG_EXPECTED_1_NUMERIC, argname);

    const double value = REAL_RO(x)[0];
    UNPROTECT(1);
    if (ISNA(value))
        Rf_error(MSG__ARG_EXPECTED_NOT_NA, argname);
    return value;
}


/** Prepare character vector argument that will be used to choose a locale */
CHARR_R_HELPER const char* ci__prepare_arg_locale_r(
    SEXP loc,
    const char* argname,
    bool allowdefault,
    bool allownull
) noexcept
{
    const char* default_locale = allownull ? nullptr : uloc_getDefault();
    if (default_locale != nullptr && is_c_locale(default_locale))
        default_locale = "en_US_POSIX";

    if (Rf_isNull(loc)) {
        if (allowdefault)
            return default_locale;
        Rf_error(MSG__ARG_EXPECTED_NOT_NULL, argname);
    }

    PROTECT(loc = ci__prepare_arg_string_1_r(loc, argname));
    return prepare_locale_value_r(
        loc, argname, default_locale, allowdefault
    );
}


/** Prepare character vector argument that will be used to choose a character encoding */
CHARR_R_HELPER const char* ci__prepare_arg_enc_r(
    SEXP enc, const char* argname, bool allowdefault
) noexcept
{
    if (allowdefault && Rf_isNull(enc))
        return nullptr;

    PROTECT(enc = ci__prepare_arg_string_1_r(enc, argname));
    SEXP value = STRING_ELT(enc, 0);
    if (value == NA_STRING) {
        UNPROTECT(1);
        Rf_error(MSG__ARG_EXPECTED_NOT_NA, argname);
    }

    if (LENGTH(value) == 0) {
        UNPROTECT(1);
        if (allowdefault)
            return nullptr;
        Rf_error(MSG__ENC_INCORRECT_ID);
    }

    const std::size_t length = std::strlen(CHAR(value));
    char* copy = static_cast<char*>(R_alloc(length+1, sizeof(char)));
    if (copy == nullptr) {
        UNPROTECT(1);
        Rf_error(MSG__MEM_ALLOC_ERROR);
    }

    value = STRING_ELT(enc, 0);
    std::memcpy(copy, CHAR(value), length+1);
    UNPROTECT(1);
    return copy;
}

} } // namespace charr::altrep_backend
