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
#include "ci_container_charclass.h"
#include "native_to_utf8.h"
#include "utf8_views.h"

#include <array>
#include <exception>


namespace charr { namespace base {

namespace {

struct TrimSlice {
    const char* data;
    R_len_t length;
};


struct ScalarTrimPattern {
    const UnicodeSet* retained;
    std::array<unsigned char, 128> ascii_retained;
    bool missing;
};


bool retained_contains(
    const UnicodeSet& retained, const unsigned char* ascii_retained,
    UChar32 code_point
)
{
    if (ascii_retained != nullptr && code_point <= 0x7f)
        return ascii_retained[code_point] != 0;
    return retained.contains(code_point);
}


// Compile-time direction flags keep the hot edge scan branch-free.
template<bool Left, bool Right>
TrimSlice trim_slice(
    const char* data, R_len_t length,
    const UnicodeSet& retained, const unsigned char* ascii_retained
)
{
    R_len_t begin = 0;
    R_len_t end = length;

    if (Left) {
        UChar32 code_point;
        for (R_len_t cursor = 0; cursor < length; ) {
            U8_NEXT(data, cursor, length, code_point);
            if (code_point < 0)
                throw StriException(MSG__INVALID_UTF8);
            if (retained_contains(
                    retained, ascii_retained, code_point
                ))
                break;
            begin = cursor;
        }
    }

    if (Right && begin < length) {
        UChar32 code_point;
        for (R_len_t cursor = length; cursor > 0; ) {
            U8_PREV(data, 0, cursor, code_point);
            if (code_point < 0)
                throw StriException(MSG__INVALID_UTF8);
            if (retained_contains(
                    retained, ascii_retained, code_point
                ))
                break;
            end = cursor;
        }
    }

    return TrimSlice{data + begin, end - begin};
}


ScalarTrimPattern make_scalar_pattern(
    const StriContainerCharClass& patterns
)
{
    ScalarTrimPattern scalar{nullptr, {}, patterns.isNA(0)};
    if (scalar.missing)
        return scalar;

    scalar.retained = &patterns.get(0);
    for (std::size_t i = 0; i < scalar.ascii_retained.size(); ++i) {
        scalar.ascii_retained[i] = static_cast<unsigned char>(
            scalar.retained->contains(static_cast<UChar32>(i))
        );
    }
    return scalar;
}


template<bool Left, bool Right, bool ScalarPattern>
void trim_records(
    SEXP output, const Utf8Input& input, R_len_t output_length,
    const StriContainerCharClass& patterns,
    const ScalarTrimPattern& scalar
)
{
    for (R_len_t i = 0; i < output_length; ++i) {
        const Utf8Record value = input.record(i);

        const bool pattern_missing = ScalarPattern
            ? scalar.missing
            : patterns.isNA(i);
        if (value.is_na() || pattern_missing) {
            SET_STRING_ELT(output, i, NA_STRING);
            continue;
        }

        const UnicodeSet* retained;
        const unsigned char* ascii_retained;
        if constexpr (ScalarPattern) {
            retained = scalar.retained;
            ascii_retained = scalar.ascii_retained.data();
        }
        else {
            retained = &patterns.get(i);
            ascii_retained = nullptr;
        }
        const TrimSlice trimmed = trim_slice<Left, Right>(
            value.ptr, value.len, *retained, ascii_retained
        );
        SET_STRING_ELT(
            output, i,
            Rf_mkCharLenCE(trimmed.data, trimmed.length, CE_UTF8)
        );
    }
}

} // namespace

/**
 * Trim characters from a charclass from left AND/OR right side of the string
 *
 * @param str character vector
 * @param pattern character vector
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-04)
 *          Use UTF-8 input and CharClass
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException-friendly & Use StrContainerCharClass
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-03)
 *          detects invalid UTF-8 byte stream
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-05)
 *          StriContainerCharClass now relies on UnicodeSet
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-06-10) negate
*/
template<bool Left, bool Right>
SEXP ci__trim_leftright(SEXP str, SEXP pattern, bool negate)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));
    R_len_t vectorize_length =
        ci__recycling_rule(true, 2, LENGTH(str), LENGTH(pattern));

    STRI__ERROR_HANDLER_BEGIN(2)
    SEXP ret;
    try {
        StriContainerCharClass pattern_cont(
            pattern, vectorize_length, negate
        );
        STRI__PROTECT(ret = Rf_allocVector(STRSXP, vectorize_length));
        if (vectorize_length > 0) {
            Utf8Input input(str, vectorize_length);
            const bool scalar_pattern = LENGTH(pattern) == 1;
            const ScalarTrimPattern scalar = scalar_pattern
                ? make_scalar_pattern(pattern_cont)
                : ScalarTrimPattern{nullptr, {}, false};

            if (scalar_pattern) {
                trim_records<Left, Right, true>(
                    ret, input, vectorize_length, pattern_cont, scalar
                );
            }
            else {
                trim_records<Left, Right, false>(
                    ret, input, vectorize_length, pattern_cont, scalar
                );
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


/**
 * Trim characters from a charclass from both sides of the string
 *
 * @param str character vector
 * @param pattern character vector
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-04)
 *          Use ci__trim_leftright
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException-friendly
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-06-10) negate
*/
SEXP ci_trim_both(SEXP str, SEXP pattern, SEXP negate)
{
    bool negate_val = ci__prepare_arg_logical_1_notNA(negate, "negate");
    return ci__trim_leftright<true, true>(str, pattern, negate_val);
}


/**
 * Trim characters from a charclass from the left of the string
 *
 * @param str character vector
 * @param pattern character vector
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-04)
 *          Use ci__trim_leftright
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-06-10) negate
*/
SEXP ci_trim_left(SEXP str, SEXP pattern, SEXP negate)
{
    bool negate_val = ci__prepare_arg_logical_1_notNA(negate, "negate");
    return ci__trim_leftright<true, false>(str, pattern, negate_val);
}


/**
 * Trim characters from a charclass from the right of the string
 *
 * @param str character vector
 * @param pattern character vector
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-04)
 *          Use ci__trim_leftright
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-06-10) negate
*/
SEXP ci_trim_right(SEXP str, SEXP pattern, SEXP negate)
{
    bool negate_val = ci__prepare_arg_logical_1_notNA(negate, "negate");
    return ci__trim_leftright<false, true>(str, pattern, negate_val);
}

} } // namespace charr::base
