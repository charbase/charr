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
#include "fixed/pattern_set.h"


namespace charr { namespace base_backend {

namespace search_fixed_count {

inline R_len_t count_ascii_byte(
    const char* data, R_len_t length, unsigned char pattern
) noexcept
{
    R_len_t count = 0;
    const unsigned char* current =
        reinterpret_cast<const unsigned char*>(data);
    const unsigned char* end = current + length;
    for (; current != end; ++current)
        count += (*current == pattern);
    return count;
}


bool count_ascii_scalar_direct(
    SEXP str, SEXP pattern, uint32_t pattern_flags,
    R_len_t vectorize_length, int* result, R_len_t& general_start
)
{
    // A scalar ASCII byte can be counted directly. Broader encodings and
    // search options retain the general UTF-8 matcher path.
    if (pattern_flags != 0 || LENGTH(pattern) != 1)
        return false;

    SEXP pattern_string = STRING_ELT(pattern, 0);
    if (pattern_string == NA_STRING || !IS_ASCII(pattern_string) ||
            LENGTH(pattern_string) != 1)
        return false;

    const unsigned char pattern_byte =
        static_cast<unsigned char>(CHAR(pattern_string)[0]);

    for (R_len_t i = 0; i < vectorize_length; ++i) {
        SEXP value = STRING_ELT(str, i);
        if (value == NA_STRING) {
            result[i] = NA_INTEGER;
            continue;
        }
        if (!IS_ASCII(value) && !IS_UTF8(value)) {
            general_start = i;
            return false;
        }
        result[i] = count_ascii_byte(
            CHAR(value), LENGTH(value), pattern_byte
        );
    }

    return true;
}

} // namespace search_fixed_count

using namespace search_fixed_count;

/**
 * Count the number of recurrences of \code{pattern} in \code{str}
 * [fast but dummy bitewise compare]
 *
 * @param str strings to search in
 * @param pattern patterns to search for
 * @return integer vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          use a UTF-8 input adapter
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          corrected behavior on empty str/pattern
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-23)
 *          make StriException-friendly,
 *          use fixed::PatternSet
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          ci_count_fixed now uses byte search only
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-07)
 *    FR #110, #23: opts_fixed arg added
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-02-14)
 *    use shared::ByteSearchMatcher
 */
SEXP ci_count_fixed(SEXP str, SEXP pattern, SEXP opts_fixed)
{
    uint32_t pattern_flags = fixed::PatternSet::getByteSearchFlags(opts_fixed, /*allow_overlap*/true);
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));

    STRI__ERROR_HANDLER_BEGIN(2)
    R_len_t vectorize_length = ci__recycling_rule(true, 2, LENGTH(str), LENGTH(pattern));

    SEXP ret;
    STRI__PROTECT(ret = Rf_allocVector(INTSXP, vectorize_length));
    int* ret_tab = INTEGER(ret);
    R_len_t general_start = 0;

    if (!count_ascii_scalar_direct(
            str, pattern, pattern_flags, vectorize_length, ret_tab,
            general_start
    )) {
        io::Utf8Input str_cont(str, vectorize_length);
        fixed::PatternSet pattern_cont(
            pattern, vectorize_length, pattern_flags
        );

        // general_start is set only by the scalar-pattern path, so the
        // pattern container advances with a unit stride from that index.
        for (R_len_t i = general_start > 0 ?
                    general_start : pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            STRI__CONTINUE_ON_EMPTY_OR_NA_STR_PATTERN(str_cont, pattern_cont,
                    ret_tab[i] = NA_INTEGER, ret_tab[i] = 0)

            shared::ByteSearchMatcher* matcher = pattern_cont.getMatcher(i);
            matcher->reset(str_cont.get(i).data(), str_cont.get(i).length());
            R_len_t found = 0;
            while (shared::ByteSearchMatcher::not_found != matcher->find_next())
                ++found;
            ret_tab[i] = found;
        }
    }

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END( ;/* do nothing special on error */ )
}

} } // namespace charr::base_backend
