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
#include <cstdio>
#include <cstring>
#include <unicode/uloc.h>

namespace charr { namespace altrep_backend {


namespace prepare_arg {

void ci__prepare_arg_list_warning(ci::DeferredWarnings* warnings)
{
    // Deviation from stringi: lazy coercions can run while an operation owns
    // output staging, so charr can queue its own list-coercion diagnostic.
    if (warnings)
        warnings->push(MSG__WARN_LIST_COERCION);
    else
        Rf_warning(MSG__WARN_LIST_COERCION);
}


void ci__prepare_arg_error(
    ci::DeferredWarnings* warnings, const char* message, const char* argname
)
{
    // Deviation from stringi: a warning-aware lazy coercion reports charr's
    // own argument failures through the outer C++ boundary instead of
    // signalling to R while the caller still owns output staging.
    if (warnings)
        throw StriException(message, argname);
    Rf_error(message, argname);
}


void ci__prepare_arg_scalar_warning(
    ci::DeferredWarnings* warnings, const char* message, const char* argname
)
{
    if (!warnings) {
        Rf_warning(message, argname);
        return;
    }

    char formatted[StriException_BUFSIZE];
    std::snprintf(
        formatted, StriException_BUFSIZE, message, argname
    );
    warnings->push(formatted);
}

} // namespace prepare_arg

using namespace prepare_arg;




// for R_tryCatchError -------------------------------------------------------

SEXP ci__call_as_character(void* data)
{
    SEXP call;
    SEXP x = (SEXP)data;
    PROTECT(call = Rf_lang2(Rf_install("as.character"), x));
    PROTECT(x = Rf_eval(call, R_BaseEnv));  // Q: BaseEnv has the generic as.*
    UNPROTECT(2);
    return x;
}


SEXP ci__call_as_integer(void* data)
{
    SEXP call;
    SEXP x = (SEXP)data;
    PROTECT(call = Rf_lang2(Rf_install("as.integer"), x));
    PROTECT(x = Rf_eval(call, R_BaseEnv));  // Q: BaseEnv has the generic as.*
    UNPROTECT(2);
    return x;
}


SEXP ci__call_as_double(void* data)
{
    SEXP call;
    SEXP x = (SEXP)data;
    PROTECT(call = Rf_lang2(Rf_install("as.double"), x));
    PROTECT(x = Rf_eval(call, R_BaseEnv));  // Q: BaseEnv has the generic as.*
    UNPROTECT(2);
    return x;
}


SEXP ci__call_as_logical(void* data)
{
    SEXP call;
    SEXP x = (SEXP)data;
    PROTECT(call = Rf_lang2(Rf_install("as.logical"), x));
    PROTECT(x = Rf_eval(call, R_BaseEnv));  // Q: BaseEnv has the generic as.*
    UNPROTECT(2);
    return x;
}


SEXP ci__call_as_raw(void* data)
{
    SEXP call;
    SEXP x = (SEXP)data;
    PROTECT(call = Rf_lang2(Rf_install("as.raw"), x));
    PROTECT(x = Rf_eval(call, R_BaseEnv));  // Q: BaseEnv has the generic as.*
    UNPROTECT(2);
    return x;
}


SEXP ci__call_as_POSIXct(void* data)
{
    SEXP call;
    SEXP x = (SEXP)data;
    PROTECT(call = Rf_lang2(Rf_install("as.POSIXct"), x));
    PROTECT(x = Rf_eval(call, R_BaseEnv));  // Q: BaseEnv has the generic as.*
    UNPROTECT(2);
    return x;
}



SEXP ci__handler_null(SEXP /*cond*/, void* /*data*/)
{
    return R_NilValue;
}


// ---------------------------------------------------------------------------

/** check if a list is empty or is a list of atomic vectors each of length 1 */
bool ci__check_list_of_scalars(SEXP x)
{
    STRI_ASSERT(Rf_isVectorList(x));
    R_len_t nv = LENGTH(x);
    for (R_len_t i=0; i<nv; ++i) {
        SEXP cur = VECTOR_ELT(x, i);
        if (!(Rf_isVectorAtomic(cur) && LENGTH(cur) == 1)) {
            return false;
        }
    }
    return true;
}


// ---------------------------------------------------------------------------


/**
 * Prepare list argument
 *
 * @param x a list or NULL
 * @param argname argument name (message formatting)
 * @return a list
 *
 * @version 1.3.2 (Marek Gagolewski, 2019-02-21)
 */
SEXP ci__prepare_arg_list(
    SEXP x, const char* argname, ci::DeferredWarnings* warnings
)
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    if (!Rf_isNull(x) && !Rf_isVectorList(x))
        ci__prepare_arg_error(warnings, MSG__ARG_EXPECTED_LIST, argname);

    return x;
}


/**
 * Prepare list of raw vectors argument, single raw vector,
 * or character vector argument
 *
 * Useful when dealing with raw data, like in string encoding
 * conversion or detection. For use, e.g., with io::RawListInput.
 *
 * If the object cannot be coerced, then an error will be generated
 *
 * WARNING: this function is allowed to call the error() function.
 * Use before STRI__ERROR_HANDLER_BEGIN (with other prepareargs).
 *
 * @param x a list /  raw vector / character vector
 * @param argname argument name (message formatting)
 * @return coercion is only done on character vector input. otherwise
 * check only is performed
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-08)
 */
SEXP ci__prepare_arg_list_raw(SEXP x, const char* argname)
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    if (Rf_isNull(x) || isRaw(x)) {
        return x; // single character string (byte data)
    }
    else if (Rf_isVectorList(x)) {
        R_len_t nv = LENGTH(x);
        for (R_len_t i=0; i<nv; ++i) {
            SEXP cur = VECTOR_ELT(x, i);
            if ((bool)Rf_isNull(cur))
                continue; // NA
            if (!isRaw(cur))
                Rf_error(MSG__ARG_EXPECTED_RAW_IN_LIST_NO_COERCION, argname);  // error() allowed here
        }
        return x;
    }
    else
        return ci__prepare_arg_string(x, argname);
}


/**
 * Prepare list of integer vectors or an integer vector argument
 *
 * If the object cannot be coerced, then an error will be generated
 *
 * WARNING: this function is allowed to call the error() function.
 * Use before STRI__ERROR_HANDLER_BEGIN (with other prepareargs).
 *
 * @param x a list of integer vectors / integer vector
 * @param argname argument name (message formatting)
 * @return a list vector
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-25)
 */
SEXP ci__prepare_arg_list_integer(SEXP x, const char* argname)
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    if ((bool)Rf_isNull(x)) {
        return x;
    }
    else if (Rf_isVectorList(x)) {
        R_len_t narg = LENGTH(x);
        if (narg <= 0) return x;

        if (MAYBE_REFERENCED(x)) {
            // the object should be copied
            SEXP xold = x;
            PROTECT(x = Rf_allocVector(VECSXP, narg));
            for (R_len_t i=0; i<narg; ++i) {
                if ((bool)Rf_isNull(VECTOR_ELT(xold, i)))
                    SET_VECTOR_ELT(x, i, R_NilValue);
                // @TODO: ci__prepare_arg_integer may call Rf_error, no UNPROTECT
                else
                    SET_VECTOR_ELT(x, i, ci__prepare_arg_integer(VECTOR_ELT(xold, i), argname));
            }
            UNPROTECT(1);
            return x;
        }
        else {
            // the object may be modified in place
            for (R_len_t i=0; i<narg; ++i)
                if (!Rf_isNull(VECTOR_ELT(x, i)))
                    SET_VECTOR_ELT(x, i, ci__prepare_arg_integer(VECTOR_ELT(x, i), argname));
            return x;
        }
    }
    else
        return ci__prepare_arg_integer(x, argname);
}


/**
 * Prepare list of character vectors argument
 *
 * This is used by functions like ci_join, which allows
 * for passing strings as "...".
 *
 * If the object cannot be coerced, then an error will be generated
 *
 * WARNING: this function is allowed to call the error() function.
 * Use before STRI__ERROR_HANDLER_BEGIN (with other prepareargs).
 *
 * @param x a list of strings
 * @param argname argument name (message formatting)
 * @return a list vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 */
SEXP ci__prepare_arg_list_string(SEXP x, const char* argname)
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    if (!Rf_isVectorList(x))
        Rf_error(MSG__ARG_EXPECTED_LIST_STRING, argname); // error() allowed here

    R_len_t narg = LENGTH(x);
    if (narg <= 0) return x;

    if (MAYBE_REFERENCED(x)) {
        // the object should be copied
        SEXP xold = x;
        PROTECT(x = Rf_allocVector(VECSXP, narg));
        for (R_len_t i=0; i<narg; ++i) {
            // @TODO: ci__prepare_arg_string may call Rf_error, no UNPROTECT
            SET_VECTOR_ELT(x, i, ci__prepare_arg_string(VECTOR_ELT(xold, i), argname));
        }
        UNPROTECT(1);
        return x;
    }
    else {
        // the object may be modified in place
        for (R_len_t i=0; i<narg; ++i)
            SET_VECTOR_ELT(x, i, ci__prepare_arg_string(VECTOR_ELT(x, i), argname));
        return x;
    }
}


/**
 * Prepare character vector argument
 *
 * If the object cannot be coerced, then an error will be generated
 *
 * WARNING: By default (allow_error=true), this function is allowed to call the error() function.
 * Use before STRI__ERROR_HANDLER_BEGIN (with other prepareargs).
 *
 *
 * @param x a character vector or an object that can be coerced to a character vector
 * @param argname argument name (message formatting)
 * @param allow_error if false, protect a call to as.* and return NilValue on fail.
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          argname added
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-19)
 *    BUGFIX: PROTECT mem from GC in factor object given
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-27)
 *        treat NULLs as empty vectors
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-05-01)
 *        #154 - the class attribute set fires up an as.xxxx call
 *
 * @version 1.2.1 (Marek Gagolewski, 2018-04-21)
 *    #285: warn if coercing from a non-trivial list
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-05-20)
 *    allow_error
 */
SEXP ci__prepare_arg_string(
    SEXP x, const char* argname, bool allow_error,
    ci::DeferredWarnings* warnings
)
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    if (Rf_isVectorList(x) || Rf_isObject(x))  // factor is an object too
    {
        if (Rf_isVectorList(x) && !ci__check_list_of_scalars(x))
            ci__prepare_arg_list_warning(warnings);

#if defined(R_VERSION) && R_VERSION >= R_Version(3, 5, 0)
        if (allow_error)
            return ci__call_as_character((void*)x);
        else
            return R_tryCatchError(ci__call_as_character, (void*)x, ci__handler_null, NULL);
#else
        return ci__call_as_character((void*)x);
#endif
    }
    else if ((bool)Rf_isString(x))
        return x; // return as-is
    else if (Rf_isVectorAtomic(x) || Rf_isNull(x))
        return Rf_coerceVector(x, STRSXP);
    else if ((bool)Rf_isSymbol(x))
        return Rf_ScalarString(PRINTNAME(x));

    ci__prepare_arg_error(
        warnings, MSG__ARG_EXPECTED_STRING, argname
    );
    return x; // avoid compiler warning
}


/**
 * Prepare numeric vector argument
 *
 * If the object cannot be coerced, then an error will be generated
 *
 * WARNING: By default (allow_error=true), this function is allowed to call the error() function.
 * Use before STRI__ERROR_HANDLER_BEGIN (with other prepareargs).
 *
 *
 * @param x a numeric vector or an object that can be coerced to a numeric vector
 * @param argname argument name (message formatting)
 * @param factors_as_strings treat factors with as.character or as.double?
 * @param allow_error if false, protect a call to as.* and return NilValue on fail.
 * @return numeric vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          argname added
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-19)
 *    BUGFIX: PROTECT mem from GC in factor object given
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-27)
 *        treat NULLs as empty vectors
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-05-01)
 *        #154 - the class attribute set fires up an as.xxxx call
 *
 * @version 1.2.1 (Marek Gagolewski, 2018-04-21)
 *    #285: warn if coercing from a non-trivial list
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-05-19)
 *    factors_as_strings, allow_error
 */
SEXP ci__prepare_arg_double(
    SEXP x, const char* argname, bool factors_as_strings,
    bool allow_error, ci::DeferredWarnings* warnings
)
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    if (factors_as_strings && Rf_isFactor(x))
    {
//         SEXP call;
//         PROTECT(call = Rf_lang2(Rf_install("as.character"), x));
//         PROTECT(x = Rf_eval(call, R_GlobalEnv)); // this will mark its encoding manually
//         PROTECT(x = Rf_coerceVector(x, REALSXP));
//         UNPROTECT(3);
//         return x;

        // as.double(as.character(x))
#if defined(R_VERSION) && R_VERSION >= R_Version(3, 5, 0)
        if (allow_error)
            PROTECT(x = ci__call_as_character((void*)x));
        else {
            PROTECT(x = R_tryCatchError(ci__call_as_character, (void*)x, ci__handler_null, NULL));
            if (Rf_isNull(x)) {
                UNPROTECT(1);
                return x;
            }
        }
#else
        PROTECT(x = ci__call_as_character((void*)x));
#endif
        PROTECT(x = Rf_coerceVector(x, REALSXP));
        UNPROTECT(2);
        return x;
    }
    else if (Rf_isVectorList(x) || Rf_isObject(x))  // factor is an object too
    {
        if (Rf_isVectorList(x) && !ci__check_list_of_scalars(x))
            ci__prepare_arg_list_warning(warnings);

#if defined(R_VERSION) && R_VERSION >= R_Version(3, 5, 0)
        if (allow_error)
            return ci__call_as_double((void*)x);
        else
            return R_tryCatchError(ci__call_as_double, (void*)x, ci__handler_null, NULL);
#else
        return ci__call_as_double((void*)x);
#endif
    }
    else if ((bool)Rf_isReal(x))
        return x; //return as-is
    else if (Rf_isVectorAtomic(x) || Rf_isNull(x))
        return Rf_coerceVector(x, REALSXP);

    ci__prepare_arg_error(
        warnings, MSG__ARG_EXPECTED_NUMERIC, argname
    );
    return x; // avoid compiler warning
}



/**
 * Prepare integer vector argument
 *
 * If the object cannot be coerced, then an error will be generated
 *
 * WARNING: By default (allow_error=true), this function is allowed to call the error() function.
 * Use before STRI__ERROR_HANDLER_BEGIN (with other prepareargs).
 *
 *
 * @param x an integer vector or an object that can be coerced to an integer vector
 * @param argname argument name (message formatting)
 * @param factors_as_strings treat factors with as.character or as.integer?
 * @param allow_error if false, protect a call to as.* and return NilValue on fail.
 * @return integer vector
 *
 * @version 0.1?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          argname added
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-19)
 *    BUGFIX: PROTECT mem from GC in factor object given
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-27)
 *        treat NULLs as empty vectors
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-05-01)
 *        #154 - the class attribute set fires up an as.xxxx call
 *
 * @version 1.2.1 (Marek Gagolewski, 2018-04-21)
 *    #285: warn if coercing from a non-trivial list
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-05-19)
 *    factors_as_strings, allow_error
 */
SEXP ci__prepare_arg_integer(
    SEXP x, const char* argname, bool factors_as_strings,
    bool allow_error, ci::DeferredWarnings* warnings
)
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    if (factors_as_strings && Rf_isFactor(x)) // factors must be checked first (as they are currently represented as integer vectors)
    {
//         SEXP call;
//         PROTECT(call = Rf_lang2(Rf_install("as.character"), x));
//         PROTECT(x = Rf_eval(call, R_GlobalEnv)); // this will mark its encoding manually
//         PROTECT(x = Rf_coerceVector(x, INTSXP));
//         UNPROTECT(3);
//         return x;

        // as.integer(as.character(x))
#if defined(R_VERSION) && R_VERSION >= R_Version(3, 5, 0)
        if (allow_error)
            PROTECT(x = ci__call_as_character((void*)x));
        else {
            PROTECT(x = R_tryCatchError(ci__call_as_character, (void*)x, ci__handler_null, NULL));
            if (Rf_isNull(x)) {
                UNPROTECT(1);
                return x;
            }
        }
#else
        PROTECT(x = ci__call_as_character((void*)x));
#endif
        PROTECT(x = Rf_coerceVector(x, INTSXP));
        UNPROTECT(2);
        return x;
    }
    else if (Rf_isVectorList(x) || Rf_isObject(x))  // factor is an object too
    {
        if (Rf_isVectorList(x) && !ci__check_list_of_scalars(x))
            ci__prepare_arg_list_warning(warnings);

#if defined(R_VERSION) && R_VERSION >= R_Version(3, 5, 0)
        if (allow_error)
            return ci__call_as_integer((void*)x);
        else
            return R_tryCatchError(ci__call_as_integer, (void*)x, ci__handler_null, NULL);
#else
        return ci__call_as_integer((void*)x);
#endif
    }
    else if (Rf_isInteger(x))
        return x; // return as-is
    else if (Rf_isVectorAtomic(x) || Rf_isNull(x))
        return Rf_coerceVector(x, INTSXP);

    ci__prepare_arg_error(
        warnings, MSG__ARG_EXPECTED_INTEGER, argname
    );
    return x; // avoid compiler warning
}





/**
 * Prepare logical vector argument
 *
 * If the object cannot be coerced, then an error will be generated
 *
 * WARNING: this function is allowed to call the error() function.
 * Use before STRI__ERROR_HANDLER_BEGIN (with other prepareargs).
 *
 *
 * @param x a logical vector or an object that can be coerced to a logical vector
 * @param argname argument name (message formatting)
 * @return logical vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          argname added
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-19)
 *    BUGFIX: PROTECT mem from GC in factor object given
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-27)
 *        treat NULLs as empty vectors
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-05-01)
 *        #154 - the class attribute set fires up an as.xxxx call
 *
 * @version 1.2.1 (Marek Gagolewski, 2018-04-21)
 *    #285: warn if coercing from a non-trivial list
 *
 * @version 1.6.2 (Marek Gagolewski, 2021-05-19)
 *    call as.logical on factors (not as.character+coerce to LGLSXP)
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-05-20)
 *    allow_error
 */
SEXP ci__prepare_arg_logical(
    SEXP x, const char* argname, bool allow_error,
    ci::DeferredWarnings* warnings
)
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    if (Rf_isFactor(x))
    {
//         SEXP call;
//         PROTECT(call = Rf_lang2(Rf_install("as.character"), x));
//         PROTECT(x = Rf_eval(call, R_GlobalEnv)); // this will mark its encoding manually
//         PROTECT(x = Rf_coerceVector(x, LGLSXP));
//         UNPROTECT(3);
#if defined(R_VERSION) && R_VERSION >= R_Version(3, 5, 0)
        if (allow_error)
            return ci__call_as_logical((void*)x);
        else
            return R_tryCatchError(ci__call_as_logical, (void*)x, ci__handler_null, NULL);
#else
        return ci__call_as_logical((void*)x);
#endif
    }
    else if (Rf_isVectorList(x) || Rf_isObject(x))
    {
        if (Rf_isVectorList(x) && !ci__check_list_of_scalars(x))
            ci__prepare_arg_list_warning(warnings);

#if defined(R_VERSION) && R_VERSION >= R_Version(3, 5, 0)
        if (allow_error)
            return ci__call_as_logical((void*)x);
        else
            return R_tryCatchError(ci__call_as_logical, (void*)x, ci__handler_null, NULL);
#else
        return ci__call_as_logical((void*)x);
#endif
    }
    else if ((bool)Rf_isLogical(x))
        return x; // return as-is
    else if (Rf_isVectorAtomic(x) || Rf_isNull(x))
        return Rf_coerceVector(x, LGLSXP);

    ci__prepare_arg_error(
        warnings, MSG__ARG_EXPECTED_LOGICAL, argname
    );
    return x; // avoid compiler warning
}


/**
 * Prepare raw vector argument
 *
 * If the object cannot be coerced, then an error will be generated
 *
 * WARNING: this function is allowed to call the error() function.
 * Use before STRI__ERROR_HANDLER_BEGIN (with other prepareargs).
 *
 *
 * @param x a raw vector or an object that can be coerced to a raw vector
 * @param argname argument name (message formatting)
 * @return raw vector
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-19)
 *    BUGFIX: PROTECT mem from GC in factor object given
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-27)
 *        treat NULLs as empty vectors
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-05-01)
 *        #154 - the class attribute set fires up an as.xxxx call
 *
 * @version 1.2.1 (Marek Gagolewski, 2018-04-21)
 *    #285: warn if coercing from a non-trivial list
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-05-19)
 *    factors_as_strings, allow_error
 */
SEXP ci__prepare_arg_raw(
    SEXP x, const char* argname, bool factors_as_strings,
    bool allow_error, ci::DeferredWarnings* warnings
)
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    if (factors_as_strings && Rf_isFactor(x))
    {
//         SEXP call;
//         PROTECT(call = Rf_lang2(Rf_install("as.character"), x));
//         PROTECT(x = Rf_eval(call, R_GlobalEnv)); // this will mark its encoding manually
//         PROTECT(x = Rf_coerceVector(x, RAWSXP));
//         UNPROTECT(3);
//         return x;
        // as.raw(as.character(x))
#if defined(R_VERSION) && R_VERSION >= R_Version(3, 5, 0)
        if (allow_error)
            PROTECT(x = ci__call_as_character((void*)x));
        else {
            PROTECT(x = R_tryCatchError(ci__call_as_character, (void*)x, ci__handler_null, NULL));
            if (Rf_isNull(x)) {
                UNPROTECT(1);
                return x;
            }
        }
#else
        PROTECT(x = ci__call_as_character((void*)x));
#endif
        PROTECT(x = Rf_coerceVector(x, RAWSXP));
        UNPROTECT(2);
        return x;
    }
    else if (Rf_isVectorList(x) || Rf_isObject(x))
    {
        if (Rf_isVectorList(x) && !ci__check_list_of_scalars(x))
            ci__prepare_arg_list_warning(warnings);

#if defined(R_VERSION) && R_VERSION >= R_Version(3, 5, 0)
        if (allow_error)
            return ci__call_as_raw((void*)x);
        else
            return R_tryCatchError(ci__call_as_raw, (void*)x, ci__handler_null, NULL);
#else
        return ci__call_as_raw((void*)x);
#endif
    }
    else if (TYPEOF(x) == RAWSXP)
        return x; // return as-is
    else if (Rf_isVectorAtomic(x) || Rf_isNull(x))
        return Rf_coerceVector(x, RAWSXP);

    ci__prepare_arg_error(
        warnings, MSG__ARG_EXPECTED_RAW, argname
    );
    return x; // avoid compiler warning
}



/**
 * POSIXt
 *
 * If the object cannot be coerced, then an error will be generated
 *
 * WARNING: this function is allowed to call the error() function.
 * Use before STRI__ERROR_HANDLER_BEGIN (with other prepareargs).
 *
 *
 * @param x a numeric vector with class POSIXct or something coercible to
 * @param argname argument name (message formatting)
 * @return numeric vector
 *
 * @version 0.5-1 (Marek Gagolewski, 2014-12-30)
 * @version 1.1.6 (Marek Gagolewski, 2020-02-17) bugfix #370
 * @version 1.6.3 (Marek Gagolewski, 2021-05-26) call as.POSIXct more eagerly
 */
SEXP ci__prepare_arg_POSIXct(SEXP x, const char* argname)
{
    int num_protect = 0;
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    if (/*factors_as_strings &&*/ Rf_isFactor(x)) {
        PROTECT(x = ci__call_as_character((void*)x));
        num_protect += 1;
        // will convert from character below
    }

    if (Rf_inherits(x, "POSIXlt") || Rf_inherits(x, "Date") || (TYPEOF(x) == STRSXP)) {
        PROTECT(x = ci__call_as_POSIXct((void*)x));
        num_protect += 1;
    }

    if (!Rf_inherits(x, "POSIXct")) {
        Rf_error(MSG__ARG_EXPECTED_POSIXct, argname);
    }

    SEXP attrib_class, attrib_tzone, robj_class, robj_tzone;
    PROTECT(robj_class = Rf_ScalarString(Rf_mkChar("class")));
    PROTECT(robj_tzone = Rf_ScalarString(Rf_mkChar("tzone")));
    PROTECT(attrib_class = Rf_getAttrib(x, robj_class));
    PROTECT(attrib_tzone = Rf_getAttrib(x, robj_tzone));
    PROTECT(x = ci__prepare_arg_double(x, argname));
    Rf_setAttrib(x, robj_class, attrib_class);
    Rf_setAttrib(x, robj_tzone, attrib_tzone);
    UNPROTECT(num_protect+5);
    return x;
}



/** Prepare string argument - one string
 *
 * If there are 0 elements -> error
 * If there are >1 elements -> warning
 *
 * WARNING: this function is allowed to call the error() function.
 * Use before STRI__ERROR_HANDLER_BEGIN (with other prepareargs).
 *
 *
 * @param x R object to be checked/coerced
 * @param argname argument name (message formatting)
 * @return always an R character vector with >=1 element
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.2.1 (Marek Gagolewski, 2018-04-21)
 *    #285: warn if coercing from a non-trivial list
 *    refactor: use ci__prepare_arg_xxx (again, as in pre-64651ed-commits)
 */
SEXP ci__prepare_arg_string_1(
    SEXP x, const char* argname, ci::DeferredWarnings* warnings
)
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    PROTECT(x = ci__prepare_arg_string(x, argname, true, warnings));
    int nprotect = 1;

//     if ((SEXP*)argname == (SEXP*)R_NilValue)
//         argname = "<noname>";
//      int nprotect = 0;
//     if (Rf_isFactor(x))
//     {
//         SEXP call;
//         PROTECT(call = Rf_lang2(Rf_install("as.character"), x));
//         PROTECT(x = Rf_eval(call, R_GlobalEnv)); // this will mark its encoding manually
//         nprotect = 2;
//     }
//     else if (Rf_isVectorList(x) || Rf_isObject(x))
//     {
//         if (Rf_isVectorList(x)) {
//             R_len_t nv = LENGTH(x);
//             for (R_len_t i=0; i<nv; ++i) {
//                 SEXP cur = VECTOR_ELT(x, i);
//                 if (!(Rf_isVectorAtomic(cur) && LENGTH(cur) == 1)) {
//                     Rf_warning(MSG__WARN_LIST_COERCION);
//                     break;
//                 }
//             }
//         }
//         SEXP call;
//         PROTECT(call = Rf_lang2(Rf_install("as.character"), x));
//         PROTECT(x = Rf_eval(call, R_GlobalEnv)); // this will mark its encoding manually
//         nprotect = 2;
//     }
//     else if ((bool)Rf_isString(x))
//         nprotect = 0;
//     else if (Rf_isVectorAtomic(x) || Rf_isNull(x)) {
//         PROTECT(x = Rf_coerceVector(x, STRSXP));
//         nprotect = 1;
//     }
//     else if ((bool)Rf_isSymbol(x)) {
//         PROTECT(x = Rf_ScalarString(PRINTNAME(x)));
//         nprotect = 1;
//     }
//     else {
//         Rf_error(MSG__ARG_EXPECTED_STRING, argname); // allowed here
//         return x; // avoid compiler warning
//     }


    R_len_t nx = LENGTH(x);

    if (nx <= 0) {
        UNPROTECT(nprotect);
        ci__prepare_arg_error(
            warnings, MSG__ARG_EXPECTED_NOT_EMPTY, argname
        );
        // won't come here anyway
        return x; // avoid compiler warning
    }
    else if (nx > 1) {
        if (!warnings)
            ci__prepare_arg_scalar_warning(
                NULL, MSG__ARG_EXPECTED_1_STRING, argname
            );
        SEXP xold = x;
        PROTECT(x = Rf_allocVector(STRSXP, 1));
        nprotect++;
        SET_STRING_ELT(x, 0, STRING_ELT(xold, 0));
        UNPROTECT(nprotect);
        // Deviation from stringi: in the deferred path, release the local R
        // protection before the warning queue performs a C++ allocation.
        if (warnings)
            ci__prepare_arg_scalar_warning(
                warnings, MSG__ARG_EXPECTED_1_STRING, argname
            );
        return x;
    }
    else { // if (nx == 1)
        UNPROTECT(nprotect);
        return x;
    }
}


/** Prepare double argument - one value
 *
 * If there are 0 elements -> error
 * If there are >1 elements -> warning
 *
 * WARNING: this function is allowed to call the error() function.
 * Use before STRI__ERROR_HANDLER_BEGIN (with other prepareargs).
 *
 *
 * @param x R object to be checked/coerced
 * @param argname argument name (message formatting)
 * @return always an R double vector with >=1 element
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.2.1 (Marek Gagolewski, 2018-04-21)
 *    #285: warn if coercing from a non-trivial list
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-05-19)
 *    factors_as_strings
 *    refactor: use ci__prepare_arg_xxx (again, as in pre-64651ed-commits)
 */
SEXP ci__prepare_arg_double_1(
    SEXP x, const char* argname, bool factors_as_strings,
    ci::DeferredWarnings* warnings
)
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    PROTECT(x = ci__prepare_arg_double(
        x, argname, factors_as_strings, true, warnings
    ));
    int nprotect = 1;

//     if ((SEXP*)argname == (SEXP*)R_NilValue)
//         argname = "<noname>";
//
//     int nprotect = 0;
//     if (factors_as_strings && Rf_isFactor(x))
//     {
//         SEXP call;
//         PROTECT(call = Rf_lang2(Rf_install("as.character"), x));
//         PROTECT(x = Rf_eval(call, R_GlobalEnv)); // this will mark its encoding manually
//         PROTECT(x = Rf_coerceVector(x, REALSXP));
//         nprotect = 3;
//     }
//     else if (Rf_isVectorList(x) || Rf_isObject(x))
//     {
//         if (Rf_isVectorList(x)) {
//             R_len_t nv = LENGTH(x);
//             for (R_len_t i=0; i<nv; ++i) {
//                 SEXP cur = VECTOR_ELT(x, i);
//                 if (!(Rf_isVectorAtomic(cur) && LENGTH(cur) == 1)) {
//                     Rf_warning(MSG__WARN_LIST_COERCION);
//                     break;
//                 }
//             }
//         }
//         SEXP call;
//         PROTECT(call = Rf_lang2(Rf_install("as.double"), x));
//         PROTECT(x = Rf_eval(call, R_GlobalEnv)); // this will mark its encoding manually
//         nprotect = 2;
//     }
//     else if ((bool)Rf_isReal(x))
//         nprotect = 0;
//     else if (Rf_isVectorAtomic(x) || Rf_isNull(x)) {
//         PROTECT(x = Rf_coerceVector(x, REALSXP));
//         nprotect = 1;
//     }
//     else {
//         Rf_error(MSG__ARG_EXPECTED_NUMERIC, argname); // allowed here
//         return x; // avoid compiler warning
//     }

    R_len_t nx = LENGTH(x);

    if (nx <= 0) {
        UNPROTECT(nprotect);
        ci__prepare_arg_error(
            warnings, MSG__ARG_EXPECTED_NOT_EMPTY, argname
        );
        // won't come here anyway
        return x; // avoid compiler warning
    }
    else if (nx > 1) {
        if (!warnings)
            ci__prepare_arg_scalar_warning(
                NULL, MSG__ARG_EXPECTED_1_NUMERIC, argname
            );
        double x0 = REAL_RO(x)[0];
        PROTECT(x = Rf_allocVector(REALSXP, 1));
        nprotect++;
        REAL(x)[0] = x0;
        UNPROTECT(nprotect);
        if (warnings)
            ci__prepare_arg_scalar_warning(
                warnings, MSG__ARG_EXPECTED_1_NUMERIC, argname
            );
        return x;
    }
    else {// if (nx == 1)
        UNPROTECT(nprotect);
        return x;
    }
}


/** Prepare integer argument - one value
 *
 * If there are 0 elements -> error
 * If there are >1 elements -> warning
 *
 * WARNING: this function is allowed to call the error() function.
 * Use before STRI__ERROR_HANDLER_BEGIN (with other prepareargs).
 *
 *
 * @param x R object to be checked/coerced
 * @param argname argument name (message formatting)
 * @return always an R integer vector with >=1 element
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.2.1 (Marek Gagolewski, 2018-04-21)
 *    #285: warn if coercing from a non-trivial list
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-05-19)
 *    factors_as_strings
 *    refactor: use ci__prepare_arg_xxx (again, as in pre-64651ed-commits)
 */
SEXP ci__prepare_arg_integer_1(
    SEXP x, const char* argname, bool factors_as_strings,
    ci::DeferredWarnings* warnings
)
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    PROTECT(x = ci__prepare_arg_integer(
        x, argname, factors_as_strings, true, warnings
    ));
    int nprotect = 1;

//     if ((SEXP*)argname == (SEXP*)R_NilValue)
//         argname = "<noname>";
//
//     int nprotect = 0;
//     if (factors_as_strings && Rf_isFactor(x)) // factors must be checked first (as they are currently represented as integer vectors)
//     {
//         SEXP call;
//         PROTECT(call = Rf_lang2(Rf_install("as.character"), x));
//         PROTECT(x = Rf_eval(call, R_GlobalEnv)); // this will mark its encoding manually
//         PROTECT(x = Rf_coerceVector(x, INTSXP));
//         nprotect = 3;
//     }
//     else if (Rf_isVectorList(x) || Rf_isObject(x))
//     {
//         if (Rf_isVectorList(x)) {
//             R_len_t nv = LENGTH(x);
//             for (R_len_t i=0; i<nv; ++i) {
//                 SEXP cur = VECTOR_ELT(x, i);
//                 if (!(Rf_isVectorAtomic(cur) && LENGTH(cur) == 1)) {
//                     Rf_warning(MSG__WARN_LIST_COERCION);
//                     break;
//                 }
//             }
//         }
//         SEXP call;
//         PROTECT(call = Rf_lang2(Rf_install("as.integer"), x));
//         PROTECT(x = Rf_eval(call, R_GlobalEnv)); // this will mark its encoding manually
//         nprotect = 2;
//     }
//     else if (Rf_isInteger(x))
//         nprotect = 0;
//     else if (Rf_isVectorAtomic(x) || Rf_isNull(x)) {
//         PROTECT(x = Rf_coerceVector(x, INTSXP));
//         nprotect = 1;
//     }
//     else {
//         Rf_error(MSG__ARG_EXPECTED_INTEGER, argname); //allowed here
//         return x; // avoid compiler warning
//     }

    R_len_t nx = LENGTH(x);

    if (nx <= 0) {
        UNPROTECT(nprotect);
        ci__prepare_arg_error(
            warnings, MSG__ARG_EXPECTED_NOT_EMPTY, argname
        );
        // won't come here anyway
        return x; // avoid compiler warning
    }
    else if (nx > 1) {
        if (!warnings)
            ci__prepare_arg_scalar_warning(
                NULL, MSG__ARG_EXPECTED_1_INTEGER, argname
            );
        int x0 = INTEGER_RO(x)[0];
        PROTECT(x = Rf_allocVector(INTSXP, 1));
        nprotect++;
        INTEGER(x)[0] = x0;
        UNPROTECT(nprotect);
        if (warnings)
            ci__prepare_arg_scalar_warning(
                warnings, MSG__ARG_EXPECTED_1_INTEGER, argname
            );
        return x;
    }
    else { // if (nx == 1)
        UNPROTECT(nprotect);
        return x;
    }
}


/** Prepare logical argument - one value
 *
 * If there are 0 elements -> error
 * If there are >1 elements -> warning
 *
 * WARNING: this function is allowed to call the error() function.
 * Use before STRI__ERROR_HANDLER_BEGIN (with other prepareargs).
 *
 *
 * @param x R object to be checked/coerced
 * @param argname argument name (message formatting)
 * @return always an R logical vector with >=1 element
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.2.1 (Marek Gagolewski, 2018-04-21)
 *    #285: warn if coercing from a non-trivial list
 *
 * @version 1.6.2 (Marek Gagolewski, 2021-05-19)
 *    call as.logical on factors (not as.character+coerce to LGLSXP)
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-05-20)
 *    refactor: use ci__prepare_arg_xxx (again, as in pre-64651ed-commits)
 */
SEXP ci__prepare_arg_logical_1(
    SEXP x, const char* argname, ci::DeferredWarnings* warnings
)
{
    if ((SEXP*)argname == (SEXP*)R_NilValue)
        argname = "<noname>";

    PROTECT(x = ci__prepare_arg_logical(
        x, argname, true, warnings
    ));
    int nprotect = 1;

//     int nprotect = 0;
//
//     if ((SEXP*)argname == (SEXP*)R_NilValue)
//         argname = "<noname>";
//
//     if (Rf_isFactor(x))
//     {
//         SEXP call;
// //         PROTECT(call = Rf_lang2(Rf_install("as.character"), x));
// //         PROTECT(x = Rf_eval(call, R_GlobalEnv)); // this will mark its encoding manually
// //         PROTECT(x = Rf_coerceVector(x, LGLSXP));
// //         nprotect = 3;
//         PROTECT(call = Rf_lang2(Rf_install("as.logical"), x));
//         PROTECT(x = Rf_eval(call, R_GlobalEnv)); // this will mark its encoding manually
//         nprotect = 2;
//     }
//     else if (Rf_isVectorList(x) || Rf_isObject(x))
//     {
//         if (Rf_isVectorList(x)) {
//             R_len_t nv = LENGTH(x);
//             for (R_len_t i=0; i<nv; ++i) {
//                 SEXP cur = VECTOR_ELT(x, i);
//                 if (!(Rf_isVectorAtomic(cur) && LENGTH(cur) == 1)) {
//                     Rf_warning(MSG__WARN_LIST_COERCION);
//                     break;
//                 }
//             }
//         }
//         SEXP call;
//         PROTECT(call = Rf_lang2(Rf_install("as.logical"), x));
//         PROTECT(x = Rf_eval(call, R_GlobalEnv)); // this will mark its encoding manually
//         nprotect = 2;
//     }
//     else if ((bool)Rf_isLogical(x)) {
//         nprotect = 0;
//         // do nothing
//     }
//     else if (Rf_isVectorAtomic(x) || Rf_isNull(x)) {
//         PROTECT(x = Rf_coerceVector(x, LGLSXP));
//         nprotect = 1;
//     }
//     else {
//         Rf_error(MSG__ARG_EXPECTED_LOGICAL, argname); // allowed here
//         return x; // avoid compiler warning
//     }

    R_len_t nx = LENGTH(x);

    if (nx <= 0) {
        UNPROTECT(nprotect);
        ci__prepare_arg_error(
            warnings, MSG__ARG_EXPECTED_NOT_EMPTY, argname
        );
        // won't come here anyway
        return x; // avoid compiler warning
    }
    else if (nx > 1) {
        if (!warnings)
            ci__prepare_arg_scalar_warning(
                NULL, MSG__ARG_EXPECTED_1_LOGICAL, argname
            );
        int x0 = LOGICAL_RO(x)[0];
        PROTECT(x = Rf_allocVector(LGLSXP, 1));
        nprotect++;
        LOGICAL(x)[0] = x0;
        UNPROTECT(nprotect);
        if (warnings)
            ci__prepare_arg_scalar_warning(
                warnings, MSG__ARG_EXPECTED_1_LOGICAL, argname
            );
        return x;
    }
    else { // if (nx == 1)
        UNPROTECT(nprotect);
        return x;
    }
}


/** Prepare logical argument - one value, not NA
 *
 * If there are 0 elements -> error
 * If there are >1 elements -> warning
 *
 * WARNING: this function is allowed to call the error() function.
 * Use before STRI__ERROR_HANDLER_BEGIN (with other prepareargs).
 *
 *
 * @param x R object to be checked/coerced
 * @param argname argument name (message formatting)
 * @return a boolean value
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
bool ci__prepare_arg_logical_1_notNA(
    SEXP x, const char* argname, ci::DeferredWarnings* warnings
)
{
    PROTECT(x = ci__prepare_arg_logical_1(x, argname, warnings));
    int xval = LOGICAL_RO(x)[0];
    UNPROTECT(1);
    if (xval == NA_LOGICAL)
        ci__prepare_arg_error(
            warnings, MSG__ARG_EXPECTED_NOT_NA, argname
        );
    return (bool)xval;
}



/** Prepare logical argument - one value, can be NA
 *
 * If there are 0 elements -> error
 * If there are >1 elements -> warning
 *
 * WARNING: this function is allowed to call the error() function.
 * Use before STRI__ERROR_HANDLER_BEGIN (with other prepareargs).
 *
 *
 * @param x R object to be checked/coerced
 * @param argname argument name (message formatting)
 * @return int value, test (xval == NA_LOGICAL)
 *
 * @version 1.6.2 (Marek Gagolewski, 2021-05-10)
 */
int ci__prepare_arg_logical_1_NA(SEXP x, const char* argname)
{
    PROTECT(x = ci__prepare_arg_logical_1(x, argname));
    int xval = LOGICAL_RO(x)[0];
    UNPROTECT(1);
    return xval;
}


/** Prepare integer argument - one value, not NA
 *
 * If there are 0 elements -> error
 * If there are >1 elements -> warning
 *
 * WARNING: this function is allowed to call the error() function.
 * Use before STRI__ERROR_HANDLER_BEGIN (with other prepareargs).
 *
 *
 * @param x R object to be checked/coerced
 * @param argname argument name (message formatting)
 * @return an integer value
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
int ci__prepare_arg_integer_1_notNA(
    SEXP x, const char* argname, ci::DeferredWarnings* warnings
)
{
    PROTECT(x = ci__prepare_arg_integer_1(
        x, argname, true, warnings
    ));
    int xval = INTEGER_RO(x)[0];
    UNPROTECT(1);
    if (xval == NA_INTEGER)
        ci__prepare_arg_error(
            warnings, MSG__ARG_EXPECTED_NOT_NA, argname
        );
    return (int)xval;
}


/** Prepare integer argument - one value, can be NA
 *
 * If there are 0 elements -> error
 * If there are >1 elements -> warning
 *
 * WARNING: this function is allowed to call the error() function.
 * Use before STRI__ERROR_HANDLER_BEGIN (with other prepareargs).
 *
 *
 * @param x R object to be checked/coerced
 * @param argname argument name (message formatting)
 * @return an integer value, test (xval == NA_INTEGER)
 *
 * @version 1.6.2 (Marek Gagolewski, 2021-05-10)
 */
int ci__prepare_arg_integer_1_NA(SEXP x, const char* argname)
{
    PROTECT(x = ci__prepare_arg_integer_1(x, argname));
    int xval = INTEGER_RO(x)[0];
    UNPROTECT(1);
    return (int)xval;
}


/** Prepare double argument - one value, not NA
 *
 * If there are 0 elements -> error
 * If there are >1 elements -> warning
 *
 * WARNING: this function is allowed to call the error() function.
 * Use before STRI__ERROR_HANDLER_BEGIN (with other prepareargs).
 *
 *
 * @param x R object to be checked/coerced
 * @param argname argument name (message formatting)
 * @return a double value
 *
 * @version 0.2-2 (Marek Gagolewski, 2014-04-26)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
double ci__prepare_arg_double_1_notNA(
    SEXP x, const char* argname, ci::DeferredWarnings* warnings
)
{
    PROTECT(x = ci__prepare_arg_double_1(
        x, argname, true, warnings
    ));
    double xval = REAL_RO(x)[0];
    UNPROTECT(1);
    if (ISNA(xval))
        ci__prepare_arg_error(
            warnings, MSG__ARG_EXPECTED_NOT_NA, argname
        );
    return (double)xval;
}


/** Prepare double argument - one value, can be NA
 *
 * If there are 0 elements -> error
 * If there are >1 elements -> warning
 *
 * WARNING: this function is allowed to call the error() function.
 * Use before STRI__ERROR_HANDLER_BEGIN (with other prepareargs).
 *
 *
 * @param x R object to be checked/coerced
 * @param argname argument name (message formatting)
 * @return a double value, test (ISNA(xval))
 *
 * @version 1.6.2 (Marek Gagolewski, 2021-05-10)
 */
double ci__prepare_arg_double_1_NA(SEXP x, const char* argname)
{
    PROTECT(x = ci__prepare_arg_double_1(x, argname));
    double xval = REAL_RO(x)[0];
    UNPROTECT(1);
    return (double)xval;
}


/**
 * This is a helper function to avoid UNPROTECTED var names warning
 * when playing with CHARSXP directly
 *
 * @param x an R STRING object (from STRING_ELT(charactervector, index))
 * @return an Ralloc'd character string
 *
 * @version 1.1.6 (Marek Gagolewski, 2017-11-10)
 */
const char* ci__copy_string_Ralloc(SEXP x, const char* argname)
{
    PROTECT(x);
    if (x == NA_STRING) {
        UNPROTECT(1);
        Rf_error(MSG__ARG_EXPECTED_NOT_NA, argname); // allowed here
    }

    const char* ret_tmp = (const char*)CHAR(x); // ret may be gc'ed
    size_t ret_n = strlen(ret_tmp);
    /* R_alloc ==  Here R will reclaim the memory at the end of the call to .Call */
    char* ret = R_alloc(ret_n+1, (int)sizeof(char));
    STRI_ASSERT(ret);
    if (!ret) {
        UNPROTECT(1);
        Rf_error(MSG__MEM_ALLOC_ERROR);
    }
    memcpy(ret, ret_tmp, ret_n+1);
    UNPROTECT(1);
    return ret;
}



/**
 * Check if we are dealing with the 'C' locale (it should be resolved to
 * en_US_POSIX)
 *
 * "C", "c", "C.UTF-8", "c.UTF-8", "C.any_other_encoding", etc.
 *
 * @version 1.8.1 (Marek Gagolewski, 2023-11-09)
 *
 * @param str string
 * @return bool
 */
bool ci__is_C_locale(const char* str)
{
    return str && ((str[0] == 'C' || str[0] == 'c')
        && (str[1] == '\0' || str[1] == '.'));
}


/**
 * Prepare character vector argument that will be used to choose a locale
 *
 * If the \code{loc} argument is incorrect, the an error is generated.
 * If something goes wrong, a warning is given.
 *
 * WARNING: this function is allowed to call the error() function.
 * Use before STRI__ERROR_HANDLER_BEGIN (with other prepareargs).
 *
 *
 * @param loc generally, a single character string
 * @param allowdefault do we allow \code{R_NilValue} or a single empty string
 *    to work as a default locale selector?
 * @param allowna do we allow \code{NA} in \code{loc}?
 *    This will return \code{NULL} as result [DEPRECATED, only used in ci_enc_detect2]
 * @param argname argument name (message formatting)
 * @return string a \code{C} string with extracted locale name;
 *    can be NULL if allownull is TRUE
 *
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          argname added
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-06)
 *    Use R_alloc for the string returned
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-01-01)
 *    "@keyword=value" may use default locale from now; also, loc is trimmed
 *
 * @version 1.5.4 (Marek Gagolewski, 2021-04-07)
 *    BUGFIX: locale='' is the default
 *
 * @version 1.8.1 (Marek Gagolewski, 2023-11-07)
 *    C is an alias of en_US_POSIX; allowna argument dropped,
 *    new argument: allownull
 */
const char* ci__prepare_arg_locale(
    SEXP loc,
    const char* argname,
    bool allowdefault,
    bool allownull,
    ci::DeferredWarnings* warnings
) {
    const char* default_locale = (allownull)?NULL:uloc_getDefault();

    if (default_locale && ci__is_C_locale(default_locale))
        default_locale = "en_US_POSIX";

    if (Rf_isNull(loc)) {
        if (allowdefault) return default_locale;
        else ci__prepare_arg_error(
            warnings, MSG__ARG_EXPECTED_NOT_NULL, argname
        );
    }

    PROTECT(loc = ci__prepare_arg_string_1(loc, argname, warnings));
    if (STRING_ELT(loc, 0) == NA_STRING) {
        UNPROTECT(1);
        ci__prepare_arg_error(
            warnings, MSG__ARG_EXPECTED_NOT_NA, argname
        );
    }

    const char* qloc = CHAR(STRING_ELT(loc, 0));
    if (qloc[0] == '\0') {  // empty string
        UNPROTECT(1);
        if (allowdefault) return default_locale;
        else ci__prepare_arg_error(
            warnings, MSG__LOCALE_INCORRECT_ID, argname
        );
    }

    UErrorCode err = U_ZERO_ERROR;
    char buf[ULOC_FULLNAME_CAPACITY];
    uloc_canonicalize(qloc, buf, ULOC_FULLNAME_CAPACITY, &err);
    UNPROTECT(1); // qloc, loc no longer used
    if (U_FAILURE(err)) {
        if (warnings)
            throw StriException(err);
        STRI__CHECKICUSTATUS_RFERROR(err, {;})
    }

    R_len_t ret_n = strlen(buf);
    char* ret = R_alloc(ret_n+1, (int)sizeof(char));
    memcpy(ret, buf, ret_n+1);

    // right-trim
    while (ret_n > 0 && (ret[ret_n-1] == ' ' || ret[ret_n-1] == '\t' || ret[ret_n-1] == '\n' || ret[ret_n-1] == '\r'))
        ret[--ret_n] = '\0';

    // left-trim
    while (ret[0] == ' ' || ret[0] == '\t' || ret[0] == '\n' || ret[0] == '\r') {
        ++ret;
        --ret_n;
    }

    if (ret_n == 0) {
        if (allowdefault) return default_locale;
        else ci__prepare_arg_error(
            warnings, MSG__LOCALE_INCORRECT_ID, argname
        );
    }

    if (ci__is_C_locale(ret))
        return "en_US_POSIX";

    if (ret[0] == ULOC_KEYWORD_SEPARATOR) {  // length is > 0
        // no locale specifier, just keywords
        if (!allowdefault)
            ci__prepare_arg_error(
                warnings, MSG__LOCALE_INCORRECT_ID, argname
            );
        const char* ret_default;
        if (default_locale) ret_default = default_locale;
        else {
            ret_default = uloc_getDefault();
            if (ci__is_C_locale(ret_default))
                ret_default = "en_US_POSIX";
        }
        R_len_t ret_detault_n = strlen(ret_default);
        const char* ret_tmp2 = ret;
        ret = R_alloc(ret_detault_n+ret_n+1, (int)sizeof(char));
        memcpy(ret, ret_default, ret_detault_n);
        memcpy(ret+ret_detault_n, ret_tmp2, ret_n+1);
    }

    return ret;
}


/**
 * Prepare character vector argument that will be used to choose a time zone
 *
 * If the \code{tz} argument is incorrect, then an error is generated.
 * If something goes wrong, a warning is given.
 *
 * The caller prepares and protects tz before entering the shared error
 * boundary. This function reports validation failures with C++ exceptions.
 *
 * @param warnings operation-level deferred warning queue
 * @param tz a prepared, protected scalar character vector or NULL
 * @param allowdefault whether NULL or an empty ID selects the default zone
 * @return TimeZone object - owned by the caller
 *
 *
 * @version 0.5-1 (Marek Gagolewski, 2014-12-24)
 */
TimeZone* ci__prepare_arg_timezone(
    ci::DeferredWarnings& warnings,
    SEXP tz, const char* argname, bool allowdefault
)
{
    UnicodeString tz_val("");

    if (!Rf_isNull(tz)) {
        // Deviation from stringi: read the prepared scalar through the common
        // UTF-8 adapter and pass ICU an explicit byte length.
        ci::ReaderContext context(warnings);
        io::Utf8Input tz_cont(context, tz, 1);
        if (tz_cont.isNA(0)) {
            throw StriException(MSG__ARG_EXPECTED_NOT_NA, argname);
        }
        const io::Utf8Record& value = tz_cont.get(0);
        tz_val.setTo(UnicodeString::fromUTF8(
            StringPiece(value.data(), value.length())
        ));
    }

//   if (tz_val.length() == 0 && !Rf_isNull(defaulttz)) {
//      PROTECT(defaulttz = ci__prepare_arg_string_1(defaulttz, argname));
//      if (STRING_ELT(defaulttz, 0) == NA_STRING) {
//         UNPROTECT(1);
//         Rf_error(MSG__ARG_EXPECTED_NOT_NA, argname); // Rf_error allowed here
//      }
//      tz_val.setTo(UnicodeString((const char*)CHAR(STRING_ELT(defaulttz, 0))));
//      UNPROTECT(1);
//   }

    if (tz_val.length() == 0) {
        if (!allowdefault)
            throw StriException(MSG__TIMEZONE_INCORRECT_ID);
        return TimeZone::createDefault();
    }
    else {
        TimeZone* ret = TimeZone::createTimeZone(tz_val);
        if (*ret == TimeZone::getUnknown()) {
            delete ret;
            throw StriException(MSG__TIMEZONE_INCORRECT_ID);
        }
        else
            return ret;
    }

    // won't arrive here anyway
    return NULL; // avoid compiler warning
}


/**
 * Prepare character vector argument that will be used to choose a character encoding
 *
 * If the \code{enc} argument is incorrect, the an error is generated.
 * If something goes wrong, a warning is given.
 *
 * WARNING: this function is allowed to call the error() function.
 * Use before STRI__ERROR_HANDLER_BEGIN (with other prepareargs).
 *
 *
 * @param enc generally, a single character string
 * @param allowdefault do we allow \code{R_NilValue} or a single empty string
 *    to work as a default charset selector? (defaults \code{true})
 * @param argname argument name (message formatting)
 * @return a \code{C} string with extracted locale name
 * (NULL for default charset so that it can be passed to ICU's \code{ucnv_open()})
 * Do not delete.
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          argname added
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-06)
 *    Use R_alloc for the string returned
 */
const char* ci__prepare_arg_enc(SEXP enc, const char* argname, bool allowdefault)
{
    if (allowdefault && Rf_isNull(enc))
        return (const char*)NULL;
    else {
        PROTECT(enc = ci__prepare_arg_string_1(enc, argname));
        if (STRING_ELT(enc, 0) == NA_STRING) {
            UNPROTECT(1);
            Rf_error(MSG__ARG_EXPECTED_NOT_NA, argname); // allowed here
        }

        if (LENGTH(STRING_ELT(enc, 0)) == 0) {
            UNPROTECT(1);
            if (allowdefault)
                return (const char*)NULL;
            else
                Rf_error(MSG__ENC_INCORRECT_ID); // allowed here
        }
        else {
            const char* ret_tmp = (const char*)CHAR(STRING_ELT(enc, 0)); // ret may be gc'ed
            size_t ret_n = strlen(ret_tmp);
            /* R_alloc ==  Here R will reclaim the memory at the end of the call to .Call */
            char* ret = R_alloc(ret_n+1, (int)sizeof(char));
            STRI_ASSERT(ret);
            if (!ret) {
                UNPROTECT(1);
                Rf_error(MSG__MEM_ALLOC_ERROR);
            }
            memcpy(ret, ret_tmp, ret_n+1);
            UNPROTECT(1);
            return ret;
        }
    }

    // won't come here anyway
    return NULL; // avoid compiler warning
}

} } // namespace charr::altrep_backend
