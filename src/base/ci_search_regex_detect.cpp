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
#include "ci_container_regex.h"
#include "utf8_input.h"

#include <exception>
#include <unicode/ustring.h>

namespace charr { namespace base {

namespace {

class ReusableUtf16Text {
private:
    UnicodeString text_;

public:
    UnicodeString& set(const Utf8Record& value)
    {
        if (value.len <= 0) {
            text_.remove();
            return text_;
        }

        UChar* destination = text_.getBuffer(value.len);
        if (!destination)
            throw StriException(MSG__MEM_ALLOC_ERROR);

        int32_t length = 0;
        if (value.state == Utf8RecordState::ascii) {
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

bool direct_plain_record(SEXP value, Utf8Record& record)
{
    if (value == NA_STRING) {
        record = Utf8Record{
            nullptr, NA_INTEGER, Utf8RecordState::missing
        };
        return true;
    }
    if (IS_BYTES(value))
        throw StriException(MSG__BYTESENC);
    if (IS_ASCII(value)) {
        record = Utf8Record{
            CHAR(value), LENGTH(value), Utf8RecordState::ascii
        };
        return true;
    }
    if (IS_UTF8(value)) {
        record = Utf8Record{
            CHAR(value), LENGTH(value), Utf8RecordState::utf8
        };
        return true;
    }
    return false;
}

bool can_borrow_plain_utf8(const SEXP* values, R_xlen_t size)
{
    for (R_xlen_t i = 0; i < size; ++i) {
        const SEXP value = values[i];
        if (value == NA_STRING)
            continue;
        if (IS_BYTES(value))
            throw StriException(MSG__BYTESENC);
        if (!IS_ASCII(value) && !IS_UTF8(value))
            return false;
    }
    return true;
}

}

/**
 * Detect if a pattern occurs in a string
 *
 * @param str R character vector
 * @param pattern R character vector containing regular expressions
 * @param negate single bool
 * @param max_count single int
 * @param opts_regex list
 *
 * @version 0.1-?? (Marcin Bujarski)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          use StriContainerUTF16
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          use StriContainerUTF16's vectorization
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-18)
 *          use StriContainerRegexPattern + opts_regex
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.0-2 (Marek Gagolewski, 2016-01-29)
 *    Issue #214: allow a regex pattern like `.*`  to match an empty string
 *
 * @version 1.0-3 (Marek Gagolewski, 2016-02-03)
 *    FR #216: `negate` arg added
 *
 * @version 1.3.1 (Marek Gagolewski, 2019-02-08)
 *    #232: `max_count` arg added
 *
 * @version 1.4.7 (Marek Gagolewski, 2020-08-24)
 *    Use StriContainerRegexPattern::getRegexOptions
 */
SEXP ci_detect_regex(SEXP str, SEXP pattern, SEXP negate,
                       SEXP max_count, SEXP opts_regex)
{
    bool negate_1 = ci__prepare_arg_logical_1_notNA(negate, "negate");
    int max_count_1 = ci__prepare_arg_integer_1_notNA(max_count, "max_count");
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));
    R_len_t vectorize_length =
        ci__recycling_rule(true, 2, LENGTH(str), LENGTH(pattern));

    StriRegexMatcherOptions pattern_opts =
        StriContainerRegexPattern::getRegexOptions(opts_regex);

    STRI__ERROR_HANDLER_BEGIN(2)
    SEXP ret;
    try {
        STRI__PROTECT(ret = Rf_allocVector(LGLSXP, vectorize_length));
        int* ret_tab = LOGICAL(ret);

        if (vectorize_length > 0) {
            ReusableUtf16Text str_text;
            // The pattern container owns its UTF-16 data. Build it before the
            // final subject borrow so an exact str/pattern alias cannot make
            // a retained subject view depend on another vector access.
            StriContainerRegexPattern pattern_cont(
                pattern, vectorize_length, pattern_opts
            );

            const SEXP* values = STRING_PTR_RO(str);
            if (LENGTH(pattern) == 1 &&
                    can_borrow_plain_utf8(values, XLENGTH(str))) {
                const bool pattern_unusable = pattern_cont.isNA(0) ||
                    pattern_cont.get(0).length() <= 0;
                RegexMatcher* matcher = nullptr;
                for (R_len_t i = 0; i < vectorize_length; ++i) {
                    if (max_count_1 == 0) {
                        ret_tab[i] = NA_LOGICAL;
                        continue;
                    }

                    Utf8Record subject;
                    direct_plain_record(
                        values[i % LENGTH(str)], subject
                    );
                    if (subject.is_na() || pattern_unusable) {
                        ret_tab[i] = NA_LOGICAL;
                        continue;
                    }

                    if (!matcher)
                        matcher = pattern_cont.getMatcher(0);
                    matcher->reset(str_text.set(subject));
                    UErrorCode status = U_ZERO_ERROR;
                    ret_tab[i] = static_cast<int>(matcher->find(status));
                    STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})

                    if (negate_1) ret_tab[i] = !ret_tab[i];
                    if (max_count_1 > 0 && ret_tab[i]) --max_count_1;
                }
            }
            else {
                Utf8Input str_input(
                    str, vectorize_length, Utf8BomPolicy::preserve
                );

                for (R_len_t i = pattern_cont.vectorize_init();
                        i != pattern_cont.vectorize_end();
                        i = pattern_cont.vectorize_next(i)) {
                    if (max_count_1 == 0) {
                        ret_tab[i] = NA_LOGICAL;
                        continue;
                    }

                    if (str_input.is_na(i) || pattern_cont.isNA(i) ||
                            pattern_cont.get(i).length() <= 0) {
                        ret_tab[i] = NA_LOGICAL;
                        continue;
                    }

                    RegexMatcher* matcher = pattern_cont.getMatcher(i);
                    matcher->reset(str_text.set(str_input.text(i)));
                    UErrorCode status = U_ZERO_ERROR;
                    ret_tab[i] = static_cast<int>(matcher->find(status));
                    STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})

                    if (negate_1) ret_tab[i] = !ret_tab[i];
                    if (max_count_1 > 0 && ret_tab[i]) --max_count_1;
                }
            }
        }
    }
    catch (const StriException&) {
        throw;
    }
    catch (const std::exception& error) {
        throw StriException("%s", error.what());
    }

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}

} } // namespace charr::base
