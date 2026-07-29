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


#ifndef CHARR_BASE_REGEX_PATTERN_SET_H
#define CHARR_BASE_REGEX_PATTERN_SET_H


#include <unicode/regex.h>
#include <vector>
#include "../io/utf16_input.h"



namespace charr { namespace base_backend { namespace regex {

/** Represents Regex Matcher's settings
 *
 * @version 1.4.7 (Marek Gagolewski, 2020-08-24)
 */
struct Options {
    uint32_t flags;
    int32_t stack_limit;
    int32_t time_limit;
};



/**
 * A class to handle regex searches
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-17)
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-18)
 *          BUGFIX: memleaks on StriException
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-05-27)
 *          BUGFIX: invalid matcher reuse on empty search string
 *
 * @version 1.3.1 (Marek Gagolewski, 2019-02-06)
 *          #337: warn on empty search pattern here
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-19)
 *          #153: extract capture group names
 */
class PatternSet {

private:

    io::Utf16Input patterns_;

    Options opts; ///< RegexMatcher options
    RegexMatcher* lastMatcher; ///< recently used RegexMatcher
    R_len_t lastMatcherIndex;  ///< used by vectorize_getMatcher

    std::vector<std::string> lastCaptureGroupNames;
    R_len_t lastCaptureGroupNamesIndex;

public:

    static Options getRegexOptions(SEXP opts_regex);

    PatternSet();
    PatternSet(SEXP rstr, R_len_t nrecycle, Options opts);
    PatternSet(PatternSet& container);
    ~PatternSet();
    PatternSet& operator=(PatternSet& container);
    RegexMatcher* getMatcher(R_len_t i);
    const std::vector<std::string>& getCaptureGroupNames(R_len_t i);

    R_len_t get_n() const noexcept { return patterns_.get_n(); }
    R_len_t get_nrecycle() const noexcept {
        return patterns_.get_nrecycle();
    }
    R_len_t vectorize_init() const noexcept {
        return patterns_.vectorize_init();
    }
    R_len_t vectorize_end() const noexcept {
        return patterns_.vectorize_end();
    }
    R_len_t vectorize_next(R_len_t index) const noexcept {
        return patterns_.vectorize_next(index);
    }
    bool isNA(R_len_t index) const { return patterns_.isNA(index); }
    const UnicodeString& get(R_len_t index) const {
        return patterns_.get(index);
    }

    SEXP getCaptureGroupRDimnames(R_len_t i, R_len_t last_i=-1, SEXP ret=R_NilValue);
    SEXP getCaptureGroupRNames(R_len_t i);  // TODO: allow reuse
};


} } } // namespace charr::base_backend::regex

#endif
