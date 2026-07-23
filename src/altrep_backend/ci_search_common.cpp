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
#include "ci_stringi.h"
#include "ci_builder.h"
#include "ci_container_utf8.h"
#include "ci_container_usearch.h"
#include <unicode/uregex.h>
#include "ci_string8buf.h"
#include <deque>
using namespace std;


/**
 * Set colnames for matrix returned by ci_locate_first_* or ci_locate_last_*
 * @param matrix R matrix with two columns
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29) name_col1, name_col2
 */
void ci__locate_set_dimnames_matrix(
    SEXP matrix, bool get_length
) {
    SEXP dimnames;
    SEXP colnames;
    PROTECT(dimnames = Rf_allocVector(VECSXP, 2));
    PROTECT(colnames = Rf_allocVector(STRSXP, 2));
    SET_STRING_ELT(colnames, 0, Rf_mkChar(MSG__LOCATE_DIM_START));
    SET_STRING_ELT(colnames, 1, Rf_mkChar(
        get_length?MSG__LOCATE_DIM_LENGTH:MSG__LOCATE_DIM_END
    ));
    SET_VECTOR_ELT(dimnames, 0, R_NilValue);
    SET_VECTOR_ELT(dimnames, 1, colnames);
    Rf_setAttrib(matrix, R_DimNamesSymbol, dimnames);
    UNPROTECT(2);
}


/**
 * Set colnames for matrices stored in a list returned by ci_locate_all_* or ci_locate_all_*
 * @param matrix R matrix with two columns
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29) name_col1, name_col2
 */
void ci__locate_set_dimnames_list(
    SEXP list, bool get_length
) {
    R_len_t n = LENGTH(list);
    if (n <= 0) return;

    SEXP dimnames;
    SEXP colnames;
    PROTECT(dimnames = Rf_allocVector(VECSXP, 2));
    PROTECT(colnames = Rf_allocVector(STRSXP, 2));
    SET_STRING_ELT(colnames, 0, Rf_mkChar(MSG__LOCATE_DIM_START));
    SET_STRING_ELT(colnames, 1, Rf_mkChar(
        get_length?MSG__LOCATE_DIM_LENGTH:MSG__LOCATE_DIM_END
    ));
    SET_VECTOR_ELT(dimnames, 1, colnames);

    for (R_len_t i = 0; i < n; ++i)
        Rf_setAttrib(VECTOR_ELT(list, i), R_DimNamesSymbol, dimnames);
    UNPROTECT(2);
}


/**
 * Copy a logical subset of str_cont to an exact-size charvec Store
 *
 * @param str_cont
 * @param which logical
 * @param result_counter
 * @return output Store
 *
 * @version 0.3-1 (Bartlomiej Tartanus, 2014-07-25)
 * @version 0.3-1 (Marek Gagolewski, 2014-10-17)
 *                using std::vector<int> to avoid mem-leaks, and
 *                const StriContainer& for increased performance
 */
charport::charvec::Store ci__subset_by_logical(
    const StriContainerUTF8& str_cont,
    const std::vector<int>& which, int result_counter
)
{
    if (result_counter <= 0)
        return charport::charvec::Store(0, 0);

    if (result_counter == 1) {
        for (R_len_t j=0; j<static_cast<R_len_t>(which.size()); ++j) {
            if (which[j] == NA_LOGICAL)
                return charport::charvec::Store::scalar(
                    NULL, 0, cetype_ext_t::CE_NA
                );
            if (which[j]) {
                const String8& value = str_cont.get(j);
                if (value.isNA())
                    return charport::charvec::Store::scalar(
                        NULL, 0, cetype_ext_t::CE_NA
                    );
                return ci::scalar_store(value);
            }
        }
        throw std::logic_error("subset result count mismatch");
    }

    charport::charvec::Builder output(result_counter);
    for (R_len_t j=0, i=0; i<result_counter; ++j) {
        if (which[j] == NA_LOGICAL) {
            output.set_na(i);
            i++;
        }
        else if (which[j]) {
            ci::builder_set(output, i, str_cont.get(j));
            i++;
        }
    }
    return output.release_store();
}


/**
 * Copy a logical subset of str_cont to an exact-size charvec Store
 *
 * @param str_cont
 * @param which logical
 * @param result_counter
 * @return output Store
 *
 * @version 0.3-1 (Bartlomiej Tartanus, 2014-07-25)
 * @version 0.3-1 (Marek Gagolewski, 2014-10-17)
 *                using std::vector<int> to avoid mem-leaks, and
 *                const StriContainer& for increased performance
 */
charport::charvec::Store ci__subset_by_logical(
    const StriContainerUTF16& str_cont,
    const std::vector<int>& which, int result_counter
)
{
    if (result_counter <= 0)
        return charport::charvec::Store(0, 0);

    std::vector<char> utf8_buffer;
    if (result_counter == 1) {
        for (R_len_t j=0; j<static_cast<R_len_t>(which.size()); ++j) {
            if (which[j] == NA_LOGICAL)
                return charport::charvec::Store::scalar(
                    NULL, 0, cetype_ext_t::CE_NA
                );
            if (which[j]) {
                const UnicodeString& value = str_cont.get(j);
                if (value.isBogus())
                    return charport::charvec::Store::scalar(
                        NULL, 0, cetype_ext_t::CE_NA
                    );
                return ci::scalar_store(value, utf8_buffer);
            }
        }
        throw std::logic_error("subset result count mismatch");
    }

    charport::charvec::Builder output(result_counter);
    for (R_len_t j=0, i=0; i<result_counter; ++j) {
        if (which[j] == NA_LOGICAL) {
            output.set_na(i);
            i++;
        }
        else if (which[j]) {
            ci::builder_set(output, i, str_cont.get(j), utf8_buffer);
            i++;
        }
    }
    return output.release_store();
}
