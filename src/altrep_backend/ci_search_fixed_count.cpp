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
#include "ci_container_base.h"
#include "ci_utf8.h"
#include "ci_container_bytesearch.h"


namespace {

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


bool direct_ascii_encoding(cetype_ext_t encoding) noexcept
{
    return encoding == cetype_ext_t::CE_ASCII ||
        encoding == cetype_ext_t::CE_UTF8 ||
        encoding == cetype_ext_t::CE_ASCII_OR_UTF8;
}


bool count_ascii_scalar_direct(
    ci::ReaderContext& context, SEXP str, SEXP pattern,
    uint32_t pattern_flags, R_len_t vectorize_length, int* result,
    R_len_t& general_start,
    std::shared_ptr<ci::ReaderBorrow>& str_borrow,
    std::shared_ptr<ci::ReaderBorrow>& pattern_borrow
)
{
    // A scalar ASCII byte can be counted in borrowed records. Broader
    // encodings and search options retain the general UTF-8 matcher path.
    if (vectorize_length == 0)
        return true;
    if (pattern_flags != 0 || context.size(pattern) != 1)
        return false;

    str_borrow = context.acquire(str);
    pattern_borrow = context.acquire(pattern);
    const charport::StrView pattern_view = pattern_borrow->views()[0];
    if (pattern_view.is_na() || !direct_ascii_encoding(pattern_view.enc) ||
            pattern_view.len != 1 ||
            static_cast<unsigned char>(pattern_view.ptr[0]) > 0x7f)
        return false;

    const unsigned char pattern_byte =
        static_cast<unsigned char>(pattern_view.ptr[0]);
    const charport::StrViews& values = str_borrow->views();

    for (R_len_t i = 0; i < vectorize_length; ++i) {
        const charport::StrView value = values[i];
        if (value.is_na()) {
            result[i] = NA_INTEGER;
            continue;
        }
        if (!direct_ascii_encoding(value.enc)) {
            general_start = i;
            return false;
        }
        result[i] = count_ascii_byte(value.ptr, value.len, pattern_byte);
    }

    return true;
}

}


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
 *          use Utf8Input
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          corrected behavior on empty str/pattern
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-23)
 *          make StriException-friendly,
 *          use StriContainerByteSearch
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
 *    use StriByteSearchMatcher
 */
SEXP ci_count_fixed(SEXP str, SEXP pattern, SEXP opts_fixed)
{
    uint32_t pattern_flags = StriContainerByteSearch::getByteSearchFlags(opts_fixed, /*allow_overlap*/true);
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));

    STRI__ERROR_HANDLER_BEGIN(2)
    SEXP ret;
    {
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    R_len_t str_n = ci::checked_r_len(
        context.size(str), "character vectors"
    );
    R_len_t pattern_n = ci::checked_r_len(
        context.size(pattern), "character vectors"
    );
    R_len_t vectorize_length = 0;
    charport::unwind_protect([&]() -> SEXP {
        vectorize_length = ci__recycling_rule(
            STRI__DEFERRED_WARNINGS, 2, str_n, pattern_n
        );
        return R_NilValue;
    });

    STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
        return Rf_allocVector(INTSXP, vectorize_length);
    }));
    int* ret_tab = INTEGER(ret);
    R_len_t general_start = 0;
    std::shared_ptr<ci::ReaderBorrow> str_borrow;
    std::shared_ptr<ci::ReaderBorrow> pattern_borrow;

    if (!count_ascii_scalar_direct(
            context, str, pattern, pattern_flags,
            vectorize_length, ret_tab, general_start,
            str_borrow, pattern_borrow
    )) {
        Utf8Input str_cont(context, str, vectorize_length);
        StriContainerByteSearch pattern_cont(
            context, pattern, vectorize_length, pattern_flags
        );

        for (R_len_t i = general_start > 0 ?
                    general_start : pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            STRI__CONTINUE_ON_EMPTY_OR_NA_STR_PATTERN(str_cont, pattern_cont,
                    ret_tab[i] = NA_INTEGER, ret_tab[i] = 0)

            StriByteSearchMatcher* matcher = pattern_cont.getMatcher(i);
            matcher->reset(str_cont.get(i).data(), str_cont.get(i).length());
            R_len_t found = 0;
            while (USEARCH_DONE != matcher->findNext())
                ++found;
            ret_tab[i] = found;
        }
    }

    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END( ;/* do nothing special on error */ )
}
