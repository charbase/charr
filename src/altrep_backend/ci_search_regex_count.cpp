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
#include "regex/pattern_set.h"
#include "altrep_backend/io/utf8_input.h"

#include <unicode/ustring.h>

namespace charr { namespace altrep_backend {

namespace search_regex_count {

class ReusableUtf16Text {
private:
    UnicodeString text_;

public:
    UnicodeString& set(const charport::StrView& value)
    {
        if (value.len <= 0) {
            text_.remove();
            return text_;
        }

        UChar* destination = text_.getBuffer(value.len);
        if (!destination)
            throw StriException(MSG__MEM_ALLOC_ERROR);

        int32_t length = 0;
        if (value.enc == cetype_ext_t::CE_ASCII) {
            for (int32_t i = 0; i < value.len; ++i)
                destination[i] = static_cast<unsigned char>(value.ptr[i]);
            length = value.len;
        }
        else {
            UErrorCode status = U_ZERO_ERROR;
            u_strFromUTF8WithSub(
                destination, value.len, &length,
                value.ptr, value.len, 0xfffd, nullptr, &status
            );
            text_.releaseBuffer(length);
            STRI__CHECKICUSTATUS_THROW(status, {/* nothing to release */})
            return text_;
        }

        text_.releaseBuffer(length);
        return text_;
    }
};

int count_matches(RegexMatcher* matcher, UnicodeString& subject)
{
    matcher->reset(subject);
    UErrorCode status = U_ZERO_ERROR;
    int count = 0;
    while (matcher->find(status)) {
        STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
        ++count;
    }
    STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
    return count;
}

} // namespace search_regex_count

using namespace search_regex_count;


/**
 * Count the number of recurrences of \code{pattern} in \code{s}
 *
 * @param str strings to search in
 * @param pattern regex patterns to search for
 * @param opts_regex list
 *
 * @return integer vector
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          use io::Utf16Input's vectorization
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException-friendly
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-17)
 *          use regex::PatternSet + opts_regex
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.0-2 (Marek Gagolewski, 2016-01-29)
 *    Issue #214: allow a regex pattern like `.*`  to match an empty string
 *
 * @version 1.4.7 (Marek Gagolewski, 2020-08-24)
 *    Use regex::PatternSet::getRegexOptions
 */
SEXP ci_count_regex(SEXP str, SEXP pattern, SEXP opts_regex)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));
    STRI__ERROR_HANDLER_BEGIN(2)
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    R_len_t str_n = ci::checked_r_len(
        context.size(str), "character vectors"
    );
    R_len_t pattern_n = ci::checked_r_len(
        context.size(pattern), "character vectors"
    );
    R_len_t vectorize_length = 0;
    ci::unwind_protect([&]() -> SEXP {
        vectorize_length = ci__recycling_rule(
            false, 2, str_n, pattern_n
        );
        return R_NilValue;
    });
    // Deviation from stringi: queue the recycling warning until regex and
    // Reader owners have been released.
    if (vectorize_length > 0 &&
            (vectorize_length % str_n != 0 ||
             vectorize_length % pattern_n != 0))
        context.warn(MSG__WARN_RECYCLING_RULE);

    regex::Options pattern_opts;
    // Deviation from stringi: preserve recycling-before-options order while
    // routing option-parser R unwinds through the common error boundary.
    ci::unwind_protect([&]() -> SEXP {
        pattern_opts = regex::PatternSet::getRegexOptions(
            STRI__DEFERRED_WARNINGS, opts_regex
        );
        return R_NilValue;
    });

    SEXP ret;
    STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
        return Rf_allocVector(INTSXP, vectorize_length);
    }));
    int* ret_tab = INTEGER(ret);

    if (vectorize_length > 0) {
        regex::PatternSet pattern_cont(
            context, pattern, vectorize_length, pattern_opts
        );
        ReusableUtf16Text subject_text;
        charr::altrep_backend::io::Utf8Input subjects(
            context, str, vectorize_length, true,
            charr::altrep_backend::io::Utf8BomPolicy::preserve
        );
        if (pattern_n == 1) {
            const bool pattern_unusable = pattern_cont.isNA(0) ||
                pattern_cont.get(0).length() <= 0;
            RegexMatcher* matcher = nullptr;
            for (R_len_t i = 0; i < vectorize_length; ++i) {
                if (subjects.is_na(i) || pattern_unusable) {
                    ret_tab[i] = NA_INTEGER;
                    continue;
                }
                if (!matcher)
                    matcher = pattern_cont.getMatcher(0);
                ret_tab[i] = count_matches(
                    matcher, subject_text.set(subjects.text(i))
                );
            }
        }
        else {
            for (R_len_t i = pattern_cont.vectorize_init();
                    i != pattern_cont.vectorize_end();
                    i = pattern_cont.vectorize_next(i)) {
                if (subjects.is_na(i) || pattern_cont.isNA(i) ||
                        pattern_cont.get(i).length() <= 0) {
                    ret_tab[i] = NA_INTEGER;
                    continue;
                }
                ret_tab[i] = count_matches(
                    pattern_cont.getMatcher(i),
                    subject_text.set(subjects.text(i))
                );
            }
        }
    }

    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}

} } // namespace charr::altrep_backend
