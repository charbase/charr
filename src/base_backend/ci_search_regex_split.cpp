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
#include "io/integer_input.h"
#include "io/logical_input.h"
#include "regex/pattern_set.h"
#include <utility>
#include <vector>
#include <unicode/utf8.h>
namespace charr { namespace base_backend {

using namespace std;


namespace search_regex_split {

typedef pair<R_len_t, R_len_t> CiRegexSplitField;


class CiRegexSplitSubject {
private:
    UnicodeString text_;
    vector<int32_t> byte_offsets_;
    bool ascii_;

public:
    CiRegexSplitSubject() : text_(), byte_offsets_(), ascii_(true)
    {
    }

    UnicodeString& set(const char* data, int32_t length, bool ascii)
    {
        ascii_ = ascii;
        if (length <= 0) {
            text_.remove();
            byte_offsets_.clear();
            return text_;
        }

        UChar* output = text_.getBuffer(length);
        if (!output)
            throw StriException(MSG__MEM_ALLOC_ERROR);

        int32_t source_i = 0;
        int32_t output_i = 0;
        if (ascii_) {
            for (; source_i < length; ++source_i)
                output[output_i++] = static_cast<unsigned char>(data[source_i]);
        }
        else {
            byte_offsets_.resize(static_cast<size_t>(length) + 1);
            byte_offsets_[0] = 0;
            while (source_i < length) {
                const int32_t source_begin = source_i;
                UChar32 code_point;
                U8_NEXT_OR_FFFD(data, source_i, length, code_point);
                if (code_point <= 0xffff) {
                    output[output_i++] = static_cast<UChar>(code_point);
                    byte_offsets_[static_cast<size_t>(output_i)] = source_i;
                }
                else {
                    output[output_i++] = U16_LEAD(code_point);
                    byte_offsets_[static_cast<size_t>(output_i)] = source_begin;
                    output[output_i++] = U16_TRAIL(code_point);
                    byte_offsets_[static_cast<size_t>(output_i)] = source_i;
                }
            }
            byte_offsets_.resize(static_cast<size_t>(output_i) + 1);
        }

        text_.releaseBuffer(output_i);
        return text_;
    }

    R_len_t byte_offset(int32_t utf16_offset) const
    {
        return ascii_
            ? static_cast<R_len_t>(utf16_offset)
            : static_cast<R_len_t>(
                byte_offsets_[static_cast<size_t>(utf16_offset)]
            );
    }
};


inline void ci__collect_scalar_default_fields(
    RegexMatcher* matcher, const CiRegexSplitSubject& subject,
    R_len_t string_length, UErrorCode& status,
    vector<CiRegexSplitField>& fields
)
{
    fields.clear();
    R_len_t field_start = 0;
    while (matcher->find(status)) {
        STRI__CHECKICUSTATUS_THROW(status, {})
        const int32_t match_start_utf16 = matcher->start(status);
        STRI__CHECKICUSTATUS_THROW(status, {})
        const int32_t match_end_utf16 = matcher->end(status);
        STRI__CHECKICUSTATUS_THROW(status, {})
        const R_len_t match_start = subject.byte_offset(match_start_utf16);
        const R_len_t match_end = subject.byte_offset(match_end_utf16);
        fields.emplace_back(field_start, match_start);
        field_start = match_end;
    }
    STRI__CHECKICUSTATUS_THROW(status, {})
    fields.emplace_back(field_start, string_length);
}


void ci__split_regex_scalar_default(
    const io::Utf8Input& input, SEXP pattern, R_len_t vectorize_length,
    const regex::Options& pattern_opts, SEXP ret
)
{
    regex::PatternSet pattern_cont(
        pattern, vectorize_length, pattern_opts
    );
    if (vectorize_length <= 0)
        return;

    const bool pattern_unusable = pattern_cont.isNA(0) ||
        pattern_cont.get(0).length() <= 0;
    RegexMatcher* matcher = NULL;
    CiRegexSplitSubject subject;
    vector<CiRegexSplitField> fields;
    fields.reserve(16);
    for (R_len_t i = 0; i < vectorize_length; ++i) {
        const io::Utf8Record value = input.record(i);
        if (value.is_na() || pattern_unusable) {
            SET_VECTOR_ELT(ret, i, ci__vector_NA_strings(1));
            continue;
        }

        const bool ascii = value.isASCII();
        const char* data = value.ptr;
        const R_len_t length = value.len;
        if (length <= 0) {
            SET_VECTOR_ELT(ret, i, ci__vector_empty_strings(1));
            continue;
        }

        if (!matcher)
            matcher = pattern_cont.getMatcher(0);
        matcher->reset(subject.set(data, length, ascii));
        UErrorCode status = U_ZERO_ERROR;
        ci__collect_scalar_default_fields(
            matcher, subject, length, status, fields
        );

        SEXP ans;
        PROTECT(ans = Rf_allocVector(STRSXP, fields.size()));
        for (R_len_t j = 0; j < static_cast<R_len_t>(fields.size()); ++j) {
            const CiRegexSplitField& field = fields[static_cast<size_t>(j)];
            SET_STRING_ELT(
                ans, j,
                Rf_mkCharLenCE(
                    data + field.first, field.second - field.first, CE_UTF8
                )
            );
        }
        SET_VECTOR_ELT(ret, i, ans);
        UNPROTECT(1);
    }
}


inline void ci__collect_regex_split_fields(
    RegexMatcher* matcher, R_len_t string_length, int n_cur,
    bool omit_empty, bool tokens_only, UErrorCode& status,
    vector<CiRegexSplitField>& fields
)
{
    fields.clear();
    fields.emplace_back(0, 0);

    int field_count = 1;
    while (field_count < n_cur) {
        const int found = static_cast<int>(matcher->find(status));
        STRI__CHECKICUSTATUS_THROW(status, {})
        if (!found)
            break;

        const R_len_t match_start = static_cast<R_len_t>(
            matcher->start(status)
        );
        STRI__CHECKICUSTATUS_THROW(status, {})
        const R_len_t match_end = static_cast<R_len_t>(
            matcher->end(status)
        );
        STRI__CHECKICUSTATUS_THROW(status, {})

        if (omit_empty && fields.back().first == match_start) {
            fields.back().first = match_end;
        }
        else {
            fields.back().second = match_start;
            fields.emplace_back(match_end, match_end);
            ++field_count;
        }
    }

    fields.back().second = string_length;
    if (omit_empty && fields.back().first == fields.back().second)
        fields.pop_back();

    if (tokens_only && n_cur < INT_MAX) {
        --n_cur;
        if (fields.size() > static_cast<size_t>(n_cur))
            fields.resize(static_cast<size_t>(n_cur));
    }
}

} // namespace search_regex_split

using namespace search_regex_split;


/**
 * Split a string into parts.
 *
 * The pattern matches identify delimiters that separate the input into fields.
 * The input data between the matches becomes the fields themselves.
 *
 * @param str character vector
 * @param pattern character vector
 * @param n integer vector
 * @param opts_regex
 * @param tokens_only single logical value
 * @param simplify single logical value
 *
 * @return list of character vectors  or character matrix
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-21)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-07-10)
 *          BUGFIX: wrong behavior on empty str
 *
 * @version 0.1-24 (Marek Gagolewski, 2014-03-11)
 *          Added missing utext_close call to avoid memleaks
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-10-19)
 *          added tokens_only param
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-10-23)
 *          added split param
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-10-24)
 *          allow omit_empty=NA
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-04)
 *    allow `simplify=NA`; FR #126: pass n to ci_list2matrix
 *
 * @version 1.4.7 (Marek Gagolewski, 2020-08-24)
 *    Use regex::PatternSet::getRegexOptions
 */
SEXP ci_split_regex(SEXP str, SEXP pattern, SEXP n, SEXP omit_empty,
                      SEXP tokens_only, SEXP simplify, SEXP opts_regex)
{
    bool tokens_only1 = ci__prepare_arg_logical_1_notNA(tokens_only, "tokens_only");
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));
    PROTECT(n = ci__prepare_arg_integer(n, "n"));
    PROTECT(omit_empty = ci__prepare_arg_logical(omit_empty, "omit_empty"));
    PROTECT(simplify = ci__prepare_arg_logical_1(simplify, "simplify"));
    const int simplify_1 = LOGICAL_RO(simplify)[0];
    R_len_t vectorize_length = ci__recycling_rule(true, 4,
                               LENGTH(str), LENGTH(pattern), LENGTH(n), LENGTH(omit_empty));

    regex::Options pattern_opts =
        regex::PatternSet::getRegexOptions(opts_regex);

    UText* str_text = NULL; // may potentially be slower, but definitely is more convenient!
    STRI__ERROR_HANDLER_BEGIN(5)
    if (XLENGTH(pattern) == 1 && XLENGTH(n) == 1 &&
            XLENGTH(omit_empty) == 1 &&
            INTEGER_RO(n)[0] != NA_INTEGER && INTEGER_RO(n)[0] < 0 &&
            LOGICAL_RO(omit_empty)[0] == FALSE &&
            !tokens_only1 && simplify_1 == FALSE) {
        io::Utf8Input scalar_input(str, vectorize_length);
        SEXP ret;
        STRI__PROTECT(ret = Rf_allocVector(VECSXP, vectorize_length));
        ci__split_regex_scalar_default(
            scalar_input, pattern, vectorize_length, pattern_opts, ret
        );
        STRI__UNPROTECT_ALL
        return ret;
    }

    io::Utf8Input str_cont(str, vectorize_length);
    io::IntegerInput   n_cont(n, vectorize_length);
    io::LogicalInput   omit_empty_cont(omit_empty, vectorize_length);
    regex::PatternSet pattern_cont(pattern, vectorize_length, pattern_opts);

    SEXP ret;
    STRI__PROTECT(ret = Rf_allocVector(VECSXP, vectorize_length));
    vector<CiRegexSplitField> fields;
    fields.reserve(16);

    for (R_len_t i = pattern_cont.vectorize_init();
            i != pattern_cont.vectorize_end();
            i = pattern_cont.vectorize_next(i))
    {
        if (n_cont.isNA(i)) {
            SET_VECTOR_ELT(ret, i, ci__vector_NA_strings(1));
            continue;
        }

        int n_cur = n_cont.get(i);
        const bool omit_empty_isna = omit_empty_cont.isNA(i);
        const bool omit_empty_cur =
            !omit_empty_isna && omit_empty_cont.get(i);

        STRI__CONTINUE_ON_EMPTY_OR_NA_STR_PATTERN(str_cont, pattern_cont,
                SET_VECTOR_ELT(ret, i, ci__vector_NA_strings(1));,
                SET_VECTOR_ELT(ret, i,
                               (omit_empty_isna)?ci__vector_NA_strings(1):
                               ci__vector_empty_strings((omit_empty_cur || n_cur == 0)?0:1));)

        const io::Utf8Record& str_cur = str_cont.get(i);
        const R_len_t str_cur_n = str_cur.length();
        const char* str_cur_s = str_cur.data();

        if (n_cur >= INT_MAX-1)
            throw StriException(MSG__INCORRECT_NAMED_ARG "; " MSG__EXPECTED_SMALLER, "n");
        else if (n_cur < 0)
            n_cur = INT_MAX;
        else if (n_cur == 0) {
            SET_VECTOR_ELT(ret, i, Rf_allocVector(STRSXP, 0));
            continue;
        }
        else if (tokens_only1)
            n_cur++; // we need to do one split ahead here

        UErrorCode status = U_ZERO_ERROR;
        RegexMatcher *matcher = pattern_cont.getMatcher(i); // will be deleted automatically
        str_text = utext_openUTF8(
            str_text, str_cur_s, str_cur_n, &status
        );
        STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

        matcher->reset(str_text);
        ci__collect_regex_split_fields(
            matcher, str_cur_n, n_cur, omit_empty_cur, tokens_only1,
            status, fields
        );

        SEXP ans;
        STRI__PROTECT(ans = Rf_allocVector(STRSXP, fields.size()));

        for (R_len_t k = 0; k < static_cast<R_len_t>(fields.size()); ++k) {
            const CiRegexSplitField& curoccur = fields[
                static_cast<size_t>(k)
            ];
            if (curoccur.second == curoccur.first && omit_empty_isna)
                SET_STRING_ELT(ans, k, NA_STRING);
            else
                SET_STRING_ELT(ans, k,
                               Rf_mkCharLenCE(str_cur_s+curoccur.first, curoccur.second-curoccur.first, CE_UTF8));
        }

        SET_VECTOR_ELT(ret, i, ans);
        STRI__UNPROTECT(1);
    }

    if (str_text) {
        utext_close(str_text);
        str_text = NULL;
    }

    if (LOGICAL(simplify)[0] == NA_LOGICAL || LOGICAL(simplify)[0]) {
        R_len_t n_min = 0;
        R_len_t n_length = LENGTH(n);
        int* n_tab = INTEGER(n);
        for (R_len_t i=0; i<n_length; ++i) {
            if (n_tab[i] != NA_INTEGER && n_min < n_tab[i])
                n_min = n_tab[i];
        }
        SEXP robj_TRUE, robj_n_min, robj_na_strings, robj_empty_strings;
        STRI__PROTECT(robj_TRUE = Rf_ScalarLogical(TRUE));
        STRI__PROTECT(robj_n_min = Rf_ScalarInteger(n_min));
        STRI__PROTECT(robj_na_strings = ci__vector_NA_strings(1));
        STRI__PROTECT(robj_empty_strings = ci__vector_empty_strings(1));
        STRI__PROTECT(ret = ci_list2matrix(ret, robj_TRUE,
                                             (LOGICAL(simplify)[0] == NA_LOGICAL)?robj_na_strings
                                             :robj_empty_strings,
                                             robj_n_min))
    }

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END({
        if (str_text) {
            utext_close(str_text);
            str_text = NULL;
        }
    })
}

} } // namespace charr::base_backend
