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
#include "../ci_reader.h"
#include <unicode/usearch.h>

namespace charr { namespace altrep_backend { namespace fixed {


/**
 * Default constructor
 *
 */
PatternSet::PatternSet()
    : patterns_(), matcher_()
{
    this->flags = 0;
}


/**
 * Construct String Container from R character vector
 * @param rstr R character vector
 * @param _nrecycle extend length [vectorization]
 */
PatternSet::PatternSet(
    ci::ReaderContext& context, SEXP rstr,
    R_len_t _nrecycle, uint32_t _flags
) : patterns_(context, rstr, _nrecycle, true), matcher_()
{
    this->flags = _flags;

    R_len_t n = get_n();
    for (R_len_t i=0; i<n; ++i) {
        if (!isNA(i) && get(i).length() <= 0) {
            // Deviation from stringi: defer warnings until the operation has
            // destroyed every Reader-owning container. R handlers may touch
            // any input alias and invalidate a live borrow.
            context.warn(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
        }
    }
}


/** Copying constructor
 *
 */
PatternSet::PatternSet(PatternSet& container)
    : patterns_(container.patterns_), matcher_()
{
    this->flags = container.flags;
}


/** Copy operator
 * @param container source
 * @return *this
 */
PatternSet& PatternSet::operator=(PatternSet& container)
{
    if (this == &container)
        return *this;

    // Deviation from stringi: replace owned state without explicitly ending
    // and then reusing this object's lifetime; assignment also copies flags.
    matcher_.reset();
    patterns_ = container.patterns_;
    flags = container.flags;
    return *this;
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


uint32_t PatternSet::getByteSearchFlags(
    SEXP opts_fixed, bool allow_overlap, ci::DeferredWarnings* warnings
)
{
    uint32_t flags = 0;
    if (!Rf_isNull(opts_fixed) && !Rf_isVectorList(opts_fixed)) {
        if (warnings)
            throw StriException(MSG__ARG_EXPECTED_LIST, "opts_fixed");
        Rf_error(MSG__ARG_EXPECTED_LIST, "opts_fixed");
    }

    R_len_t narg = Rf_isNull(opts_fixed)?0:LENGTH(opts_fixed);

    if (narg > 0) {

        SEXP names = PROTECT(Rf_getAttrib(opts_fixed, R_NamesSymbol));
        if (names == R_NilValue || LENGTH(names) != narg) {
            if (warnings) {
                UNPROTECT(1);
                throw StriException(MSG__FIXED_CONFIG_FAILED);
            }
            Rf_error(MSG__FIXED_CONFIG_FAILED);
        }

        for (R_len_t i=0; i<narg; ++i) {
            if (STRING_ELT(names, i) == NA_STRING) {
                if (warnings) {
                    UNPROTECT(1);
                    throw StriException(MSG__FIXED_CONFIG_FAILED);
                }
                Rf_error(MSG__FIXED_CONFIG_FAILED);
            }

            SEXP tmp_arg;
            PROTECT(tmp_arg = STRING_ELT(names, i));
            const char* curname = ci__copy_string_Ralloc(tmp_arg, "curname");  /* this is R_alloc'ed */
            UNPROTECT(1);

            PROTECT(tmp_arg = VECTOR_ELT(opts_fixed, i));
            try {
                if  (!strcmp(curname, "case_insensitive")) {
                    bool val = ci__prepare_arg_logical_1_notNA(
                        tmp_arg, "case_insensitive", warnings
                    );
                    if (val) flags |= BYTESEARCH_CASE_INSENSITIVE;
                }
                else if (!strcmp(curname, "overlap") && allow_overlap) {
                    bool val = ci__prepare_arg_logical_1_notNA(
                        tmp_arg, "overlap", warnings
                    );
                    if (val) flags |= BYTESEARCH_OVERLAP;
                }
                else if (warnings) {
                    // Deviation from stringi: retain this diagnostic until the
                    // caller has released its operation state.
                    std::string warning("incorrect opts_fixed setting: '");
                    warning += curname;
                    warning += "'; ignoring";
                    warnings->push(warning.c_str());
                }
                else {
                    Rf_warning(MSG__INCORRECT_FIXED_OPTION, curname);
                }
            }
            catch (...) {
                // Deferred validation throws through C++, so balance the
                // current option and names protections before propagating it.
                UNPROTECT(2);
                throw;
            }
            UNPROTECT(1);
        }
        UNPROTECT(1); /* names */
    }

    return flags;
}

} } } // namespace charr::altrep_backend::fixed
