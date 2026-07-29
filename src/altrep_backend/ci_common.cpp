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
#include "ci_reader.h"
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace charr { namespace altrep_backend {



/**
 * Create a character vector with given C strings
 *
 * @param numnames number of strings
 * @param ... variable number of C strings
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-03-01)
 *     assume UTF-8
*/
SEXP ci__make_character_vector_char_ptr(R_len_t numnames, ...)
{
    va_list arguments;
    charport::charvec::Builder output(numnames);

    va_start(arguments, numnames);
    try {
        for (R_len_t i = 0; i < numnames; ++i) {
            const char* value = va_arg(arguments, char*);
            // Deviation from stringi: these fixed or ICU-owned ASCII metadata
            // strings enter through a terminated API, but Builder keeps the
            // character result lazy.
            ci::builder_set(
                output, i, value, std::strlen(value),
                cetype_ext_t::CE_ASCII
            );
        }
    }
    catch (...) {
        va_end(arguments);
        throw;
    }
    va_end(arguments);

    return ci::unwind_protect([&]() -> SEXP {
        return output.to_sexp();
    });
}


/**
 * Create a character vector with given UnicodeStrings
 *
 * @param numnames number of strings
 * @param ... variable number of pointers to UnicodeString
 * @return character vector
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-03-01)
*/
SEXP ci__make_character_vector_UnicodeString_ptr(R_len_t numnames, ...)
{
    va_list arguments;
    charport::charvec::Builder output(numnames);
    std::vector<char> utf8_buffer;

    va_start(arguments, numnames);
    try {
        for (R_len_t i = 0; i < numnames; ++i) {
            UnicodeString* cur_str16 =
                (UnicodeString*)va_arg(arguments, UnicodeString*);
            // Deviation from stringi: convert with an explicit length and
            // keep the character result lazy.
            ci::builder_set(output, i, *cur_str16, utf8_buffer);
        }
    }
    catch (...) {
        va_end(arguments);
        throw;
    }
    va_end(arguments);

    return ci::unwind_protect([&]() -> SEXP {
        return output.to_sexp();
    });
}


/**
 *  Calculate the length of the output vector when applying a vectorized
 *  operation on >= 2  vectors
 *
 *  For nonconforming lengths, a warning is given
 *
 *  @param enableWarning enable warning in case of multiple calls to this function
 *  @param n number of vectors to recycle
 *  @param ... vector lengths
 *  @return max of the given lengths or 0 iff any ns* is <= 0
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          variable args length
*/
R_len_t ci__recycling_rule(bool enableWarning, int n, ...)
{
    R_len_t nsm = 0;
    va_list arguments;

    va_start(arguments, n);
    for (R_len_t i = 0; i < n; ++i) {
        R_len_t curlen = va_arg(arguments, R_len_t);
        if (curlen <= 0) {
            va_end(arguments);
            return 0;
        }
        if (curlen > nsm)
            nsm = curlen;
    }
    va_end(arguments);

    if (enableWarning) {
        bool warn = false;
        va_start(arguments, n);
        for (R_len_t i = 0; i < n; ++i) {
            R_len_t curlen = va_arg(arguments, R_len_t);
            if (nsm % curlen != 0) {
                warn = true;
                break;
            }
        }
        va_end(arguments);
        if (warn)
            Rf_warning(MSG__WARN_RECYCLING_RULE);
    }

    return nsm;
}


/** Apply R's recycling rule and queue a nonconforming-length warning.
 *
 * Deviation from stringi: operations with live C++ owners or helper-held
 * protections use this overload so R warning handlers run after cleanup.
 *
 * @param warnings operation-level deferred warning queue
 * @param n number of vectors to recycle
 * @param ... vector lengths
 * @return max of the given lengths or 0 if any length is <= 0
 */
R_len_t ci__recycling_rule(ci::DeferredWarnings& warnings, int n, ...)
{
    R_len_t nsm = 0;
    va_list arguments;

    va_start(arguments, n);
    for (R_len_t i=0; i<n; ++i) {
        R_len_t curlen = va_arg(arguments, R_len_t);
        if (curlen <= 0) {
            va_end(arguments);
            return 0;
        }
        if (curlen > nsm)
            nsm = curlen;
    }
    va_end(arguments);

    bool warn = false;
    va_start(arguments, n);
    for (R_len_t i=0; i<n; ++i) {
        R_len_t curlen = va_arg(arguments, R_len_t);
        if (nsm % curlen != 0) {
            warn = true;
            break;
        }
    }
    va_end(arguments);

    if (warn)
        warnings.push(MSG__WARN_RECYCLING_RULE);

    return nsm;
}


/**
 *  Creates a character vector filled with NA_character_
 *
 * @param howmany length of the vector, howmany >= 0
 * @return a character vector of length howmany
 *
 * @version 0.1-?? (Marek Gagolewski)
*/
SEXP ci__vector_NA_strings(R_len_t howmany)
{
    if (howmany < 0) {
        Rf_warning(MSG__EXPECTED_NONNEGATIVE);
        howmany = 0;
    }

    charport::charvec::Builder output(howmany);
    for (R_len_t i=0; i<howmany; ++i)
        output.set_na(i);

    return ci::unwind_protect([&]() -> SEXP {
        return output.to_sexp();
    });
}


/**
 *  Creates a character vector filled with NA_integer_
 *
 *  @param howmany length of the vector, howmany >= 0
 *  @return a character vector of length howmany
 *
 * @version 0.1-?? (Marek Gagolewski)
*/
SEXP ci__vector_NA_integers(R_len_t howmany)
{
    if (howmany < 0) {
        Rf_warning(MSG__EXPECTED_NONNEGATIVE);
        howmany = 0;
    }

    SEXP ret;
    PROTECT(ret = Rf_allocVector(INTSXP, howmany));
    for (R_len_t i=0; i<howmany; ++i)
        INTEGER(ret)[i] = NA_INTEGER;
    UNPROTECT(1);

    return ret;
}


/**
 *  Creates a character vector filled with empty strings
 *
 *  @param howmany length of the vector, howmany >= 0
 *  @return a character vector of length howmany
 *
 * @version 0.1-?? (Marek Gagolewski)
*/
SEXP ci__vector_empty_strings(R_len_t howmany)
{
    if (howmany < 0) {
        Rf_warning(MSG__EXPECTED_NONNEGATIVE);
        howmany = 0;
    }

    charport::charvec::Builder output(howmany);
    for (R_len_t i=0; i<howmany; ++i)
        ci::builder_set(
            output, i, "", 0, cetype_ext_t::CE_ASCII
        );

    return ci::unwind_protect([&]() -> SEXP {
        return output.to_sexp();
    });
}


/** Creates an empty R list
 *
 * @return the same as a call to list() in R
 *
 * @version 0.1-?? (Marek Gagolewski)
 */
SEXP ci__emptyList()
{
    return Rf_allocVector(VECSXP, 0);
}


/** Creates an integer matrix filled with NA_INTEGER (or something else)
 *
 * @param nrow number of rows
 * @param ncol number of columns
 *
 * @version 0.1-?? (Marek Gagolewski)
 */
SEXP ci__matrix_NA_INTEGER(R_len_t nrow, R_len_t ncol, int filler)
{
    SEXP x;
    PROTECT(x = Rf_allocMatrix(INTSXP, nrow, ncol));
    int* ians = INTEGER(x);
    const R_xlen_t size = XLENGTH(x);
    for (R_xlen_t i=0; i<size; ++i)
        ians[i] = filler;
    UNPROTECT(1);
    return x;
}


/** Creates a character matrix filled with NA_STRING
 *
 * @param nrow number of rows
 * @param ncol number of columns
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-22)
 */
SEXP ci__matrix_NA_STRING(R_len_t nrow, R_len_t ncol)
{
    if (nrow < 0 || ncol < 0)
        Rf_error("negative extents to matrix");

    const R_xlen_t size =
        static_cast<R_xlen_t>(nrow)*static_cast<R_xlen_t>(ncol);
    SEXP x = R_NilValue;
    {
        charport::charvec::Builder output(size);
        for (R_xlen_t i=0; i<size; ++i)
            output.set_na(i);
        x = ci::unwind_protect([&]() -> SEXP {
            return output.to_sexp();
        });
    }

    PROTECT(x);
    ci::unwind_protect([&]() -> SEXP {
        SEXP dim;
        PROTECT(dim = Rf_allocVector(INTSXP, 2));
        INTEGER(dim)[0] = nrow;
        INTEGER(dim)[1] = ncol;
        Rf_setAttrib(x, R_DimSymbol, dim);
        UNPROTECT(1);
        return R_NilValue;
    });
    UNPROTECT(1);
    return x;
}


/** Match an explicit-length option from a set of C string literals.
 *
 * @param option option bytes
 * @param option_length option length in bytes
 * @param set a set of options to match
 * @return index in set, negative value for no match
 *
 * @version 0.2-2 (Marek Gagolewski, 2014-04-20)
 *
 * @version 0.2-2 (Marek Gagolewski, 2014-04-24)
 *          proper handling of "word" in {"word", "word-second"}
 */
int ci__match_arg(
    const char* option, R_len_t option_length,
    const char* const* set
) {
    // Deviation from stringi: option bytes are borrowed from Reader and have
    // no trailing terminator. Exact matches still take precedence over a
    // longer option with the same prefix.
    int set_length = 0;
    while (set[set_length] != NULL) ++set_length;
    if (set_length <= 0) return -1;
    // this could be substituted for a linked list:
    std::vector<bool> excluded((size_t)set_length, false);

    for (R_len_t k=0; k<option_length; ++k) {
        for (int i=0; i<set_length; ++i) {
            if (excluded[static_cast<size_t>(i)]) continue;
            if (set[i][k] == '\0' || set[i][k] != option[k])
                excluded[static_cast<size_t>(i)] = true;
            else if (set[i][k+1] == '\0' && k+1 == option_length)
                return i; // exact match
        }
    }

    int which = -1;
    for (int i=0; i<set_length; ++i) {
        if (excluded[static_cast<size_t>(i)]) continue;
        if (which < 0) which = i;
        else return -1; // more than one match
    }
    return which;
}

} } // namespace charr::altrep_backend
