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


#include "../ci_stringi.h"
#include "pattern_set.h"
#include <unicode/usearch.h>


namespace charr { namespace base_backend { namespace fixed {

/**
 * Construct String Container from R character vector
 * @param rstr R character vector
 * @param _nrecycle extend length [vectorization]
 */
PatternSet::PatternSet(SEXP rstr, R_len_t _nrecycle, uint32_t _flags)
    : patterns_(rstr, _nrecycle), matcher_()
{
    this->flags = _flags;

    R_len_t n = get_n();
    for (R_len_t i=0; i<n; ++i) {
        if (!isNA(i) && get(i).length() <= 0) {
            r_warning(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
        }
    }
}


/**
 * @version 0.5-1 (Marek Gagolewski, 2015-02-14)
 */
shared::ByteSearchMatcher* PatternSet::getMatcher(R_len_t i) {
    if (i >= get_n() && matcher_ &&
            matcher_->pattern_data() == get(i).data()) {
        // matcher reuse
    }
    else {
        matcher_.reset(new shared::ByteSearchMatcher(
            get(i).data(), get(i).length(), isOverlap(),
            isCaseInsensitive()
        ));
    }

    return matcher_.get();
}


uint32_t PatternSet::getByteSearchFlags(SEXP opts_fixed, bool allow_overlap)
{
    uint32_t flags = 0;
    if (!Rf_isNull(opts_fixed) && !Rf_isVectorList(opts_fixed))
        Rf_error(MSG__ARG_EXPECTED_LIST, "opts_fixed"); // error() call allowed here

    R_len_t narg = Rf_isNull(opts_fixed)?0:LENGTH(opts_fixed);

    if (narg > 0) {

        SEXP names = PROTECT(Rf_getAttrib(opts_fixed, R_NamesSymbol));
        if (names == R_NilValue || LENGTH(names) != narg)
            Rf_error(MSG__FIXED_CONFIG_FAILED); // error() call allowed here

        for (R_len_t i=0; i<narg; ++i) {
            if (STRING_ELT(names, i) == NA_STRING)
                Rf_error(MSG__FIXED_CONFIG_FAILED); // error() call allowed here

            SEXP tmp_arg;
            PROTECT(tmp_arg = STRING_ELT(names, i));
            const char* curname = ci__copy_string_Ralloc(tmp_arg, "curname");  /* this is R_alloc'ed */
            UNPROTECT(1);

            PROTECT(tmp_arg = VECTOR_ELT(opts_fixed, i));
            if  (!strcmp(curname, "case_insensitive")) {
                bool val = ci__prepare_arg_logical_1_notNA(tmp_arg, "case_insensitive");
                if (val) flags |= BYTESEARCH_CASE_INSENSITIVE;
            } else if  (!strcmp(curname, "overlap") && allow_overlap) {
                bool val = ci__prepare_arg_logical_1_notNA(tmp_arg, "overlap");
                if (val) flags |= BYTESEARCH_OVERLAP;
            } else {
                r_warning(MSG__INCORRECT_FIXED_OPTION, curname);
            }
            UNPROTECT(1);
        }
        UNPROTECT(1); /* names */
    }

    return flags;
}

} } } // namespace charr::base_backend::fixed
