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
#include "ci_container_regex.h"
#include "native_to_utf8.h"
#include "stable_slice_arena.h"
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <string>
#include <utility>
#include <vector>
#include <unicode/utf8.h>
namespace charr { namespace base {

using namespace std;


namespace {

struct CiRegexExtractInput {
    const char* data;
    int32_t length;
    bool is_na;
    bool is_ascii;
    bool is_borrowed;
};


class CiRegexExtractUtf16Subject {
private:
    UnicodeString text_;
    vector<int32_t> byte_offsets_;
    const char* data_;
    int32_t length_;
    bool borrowed_;

public:
    CiRegexExtractUtf16Subject() :
        text_(), byte_offsets_(), data_(NULL), length_(0), borrowed_(true)
    {
    }

    UnicodeString& set(const Utf8Record& input, bool borrowed)
    {
        data_ = input.ptr;
        length_ = input.len;
        borrowed_ = borrowed;
        if (length_ <= 0) {
            text_.remove();
            byte_offsets_.assign(1, 0);
            return text_;
        }

        UChar* output = text_.getBuffer(length_);
        if (!output)
            throw StriException(MSG__MEM_ALLOC_ERROR);
        byte_offsets_.resize(static_cast<size_t>(length_) + 1);

        int32_t source_i = 0;
        int32_t output_i = 0;
        byte_offsets_[0] = 0;
        if (input.isASCII()) {
            for (; source_i < length_; ++source_i) {
                output[output_i++] = static_cast<unsigned char>(data_[source_i]);
                byte_offsets_[static_cast<size_t>(output_i)] = source_i + 1;
            }
        }
        else {
            while (source_i < length_) {
                const int32_t source_begin = source_i;
                UChar32 code_point;
                U8_NEXT_OR_FFFD(data_, source_i, length_, code_point);
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
        }

        text_.releaseBuffer(output_i);
        byte_offsets_.resize(static_cast<size_t>(output_i) + 1);
        return text_;
    }

    CiRegexExtractInput slice(int32_t start, int32_t end) const
    {
        if (start < 0 || end < start || end > text_.length())
            throw StriException("invalid ICU regex match boundary");
        const int32_t byte_start = byte_offsets_[static_cast<size_t>(start)];
        const int32_t byte_end = byte_offsets_[static_cast<size_t>(end)];
        return CiRegexExtractInput{
            data_ + byte_start, byte_end - byte_start,
            false, false, borrowed_
        };
    }
};


struct CiRegexExtractSlice {
    const char* borrowed;
    size_t owned_offset;
    R_len_t length;
};


class CiRegexExtractArena {
private:
    string owned_;
    vector<CiRegexExtractSlice> slices_;

public:
    size_t size() const noexcept
    {
        return slices_.size();
    }

    void append(const CiRegexExtractInput& value)
    {
        if (value.is_borrowed) {
            slices_.push_back(CiRegexExtractSlice{
                value.data, 0, value.length
            });
            return;
        }

        const size_t offset = owned_.size();
        owned_.append(value.data, static_cast<size_t>(value.length));
        slices_.push_back(CiRegexExtractSlice{NULL, offset, value.length});
    }

    const CiRegexExtractSlice& slice(size_t i) const
    {
        return slices_[i];
    }

    const char* data(const CiRegexExtractSlice& value) const
    {
        if (value.borrowed)
            return value.borrowed;
        if (value.length == 0)
            return "";
        return owned_.data() + value.owned_offset;
    }
};


enum class CiRegexExtractState : uint8_t {
    argument_na,
    no_match,
    matches
};


struct CiRegexExtractElement {
    CiRegexExtractState state;
    size_t slices_begin;
    R_len_t slices_count;

    CiRegexExtractElement() :
        state(CiRegexExtractState::argument_na),
        slices_begin(0), slices_count(0)
    {
    }
};


void ci__regex_extract_firstlast_scalar(
    SEXP str, SEXP pattern, R_len_t vectorize_length,
    const StriRegexMatcherOptions& pattern_opts, bool first,
    vector<CiRegexExtractElement>& elements, CiRegexExtractArena& arena
)
{
    Utf8Input inputs(str, vectorize_length);
    StriContainerRegexPattern pattern_cont(
        pattern, vectorize_length, pattern_opts
    );
    if (vectorize_length <= 0)
        return;
    const bool pattern_unusable = pattern_cont.isNA(0) ||
        pattern_cont.get(0).length() <= 0;
    RegexMatcher* matcher = NULL;
    CiRegexExtractUtf16Subject subject;

    for (R_len_t i = 0; i < vectorize_length; ++i) {
        const Utf8Record input = inputs.record(i);
        if (input.is_na() || pattern_unusable)
            continue;

        if (!matcher)
            matcher = pattern_cont.getMatcher(0);
        matcher->reset(subject.set(input, inputs.is_borrowed(i)));
        UErrorCode status = U_ZERO_ERROR;
        int found = static_cast<int>(matcher->find(status));
        STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
        if (!found) {
            elements[static_cast<size_t>(i)].state =
                CiRegexExtractState::no_match;
            continue;
        }

        int32_t match_start = matcher->start(status);
        STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
        int32_t match_end = matcher->end(status);
        STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
        if (!first) {
            while (true) {
                found = static_cast<int>(matcher->find(status));
                STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
                if (!found)
                    break;
                match_start = matcher->start(status);
                STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
                match_end = matcher->end(status);
                STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
            }
        }

        CiRegexExtractElement& element = elements[static_cast<size_t>(i)];
        element.slices_begin = arena.size();
        arena.append(subject.slice(match_start, match_end));
        element.slices_count = 1;
        element.state = CiRegexExtractState::matches;
    }
}


void ci__regex_extract_all_scalar(
    SEXP str, SEXP pattern, R_len_t vectorize_length,
    const StriRegexMatcherOptions& pattern_opts,
    vector<CiRegexExtractElement>& elements, CiRegexExtractArena& arena
)
{
    Utf8Input inputs(str, vectorize_length);
    StriContainerRegexPattern pattern_cont(
        pattern, vectorize_length, pattern_opts
    );
    if (vectorize_length <= 0)
        return;
    const bool pattern_unusable = pattern_cont.isNA(0) ||
        pattern_cont.get(0).length() <= 0;
    RegexMatcher* matcher = NULL;
    CiRegexExtractUtf16Subject subject;

    for (R_len_t i = 0; i < vectorize_length; ++i) {
        const Utf8Record input = inputs.record(i);
        if (input.is_na() || pattern_unusable)
            continue;

        if (!matcher)
            matcher = pattern_cont.getMatcher(0);
        matcher->reset(subject.set(input, inputs.is_borrowed(i)));
        UErrorCode status = U_ZERO_ERROR;
        CiRegexExtractElement& element = elements[static_cast<size_t>(i)];
        element.slices_begin = arena.size();
        while (matcher->find(status)) {
            STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
            const int32_t match_start = matcher->start(status);
            STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
            const int32_t match_end = matcher->end(status);
            STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
            arena.append(subject.slice(match_start, match_end));
        }
        STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})

        element.slices_count = static_cast<R_len_t>(
            arena.size() - element.slices_begin
        );
        element.state = element.slices_count > 0
            ? CiRegexExtractState::matches
            : CiRegexExtractState::no_match;
    }
}

} // namespace


/**
 * Extract first occurrence of a regex pattern in each string
 *
 * @param str character vector
 * @param pattern character vector
 * @param opts_regex list
 * @param first logical - search for the first or the last occurrence?
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-20)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.0-2 (Marek Gagolewski, 2016-01-29)
 *    Issue #214: allow a regex pattern like `.*`  to match an empty string
 *
 * @version 1.4.7 (Marek Gagolewski, 2020-08-24)
 *    Use StriContainerRegexPattern::getRegexOptions
 */
SEXP ci__extract_firstlast_regex(SEXP str, SEXP pattern, SEXP opts_regex, bool first)
{
    PROTECT(str = ci__prepare_arg_string(str, "str")); // prepare string argument
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern")); // prepare string argument
    R_len_t vectorize_length = ci__recycling_rule(true, 2, LENGTH(str), LENGTH(pattern));

    StriRegexMatcherOptions pattern_opts =
        StriContainerRegexPattern::getRegexOptions(opts_regex);

    UText* str_text = NULL; // may potentially be slower, but definitely is more convenient!
    STRI__ERROR_HANDLER_BEGIN(2)
    SEXP ret;
    if (XLENGTH(pattern) == 1) {
        try {
            vector<CiRegexExtractElement> elements(
                static_cast<size_t>(vectorize_length)
            );
            CiRegexExtractArena arena;
            ci__regex_extract_firstlast_scalar(
                str, pattern, vectorize_length, pattern_opts,
                first, elements, arena
            );

            STRI__PROTECT(ret = Rf_allocVector(STRSXP, vectorize_length));
            for (R_len_t i = 0; i < vectorize_length; ++i) {
                const CiRegexExtractElement& element =
                    elements[static_cast<size_t>(i)];
                if (element.state != CiRegexExtractState::matches) {
                    SET_STRING_ELT(ret, i, NA_STRING);
                    continue;
                }
                const CiRegexExtractSlice& slice =
                    arena.slice(element.slices_begin);
                SET_STRING_ELT(
                    ret, i,
                    Rf_mkCharLenCE(
                        arena.data(slice), slice.length, CE_UTF8
                    )
                );
            }
        }
        catch (const StriException&) {
            throw;
        }
        catch (const exception& error) {
            throw StriException("%s", error.what());
        }
    }
    else {
        Utf8Input str_cont(str, vectorize_length);
        StriContainerRegexPattern pattern_cont(
            pattern, vectorize_length, pattern_opts
        );
        STRI__PROTECT(ret = Rf_allocVector(STRSXP, vectorize_length));

        for (R_len_t i = pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            STRI__CONTINUE_ON_EMPTY_OR_NA_PATTERN(
                str_cont, pattern_cont,
                SET_STRING_ELT(ret, i, NA_STRING);
            )

            UErrorCode status = U_ZERO_ERROR;
            RegexMatcher *matcher = pattern_cont.getMatcher(i);
            str_text = utext_openUTF8(
                str_text, str_cont.get(i).data(),
                str_cont.get(i).length(), &status
            );
            STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})

            int m_start = -1;
            int m_end = -1;
            int m_res;
            matcher->reset(str_text);
            m_res = static_cast<int>(matcher->find(status));
            STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
            if (m_res) {
                m_start = static_cast<int>(matcher->start(status));
                STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
                m_end = static_cast<int>(matcher->end(status));
                STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
            }
            else {
                SET_STRING_ELT(ret, i, NA_STRING);
                continue;
            }

            if (!first) {
                while (1) {
                    m_res = static_cast<int>(matcher->find(status));
                    STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
                    if (!m_res) break;
                    m_start = static_cast<int>(matcher->start(status));
                    m_end = static_cast<int>(matcher->end(status));
                    STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
                }
            }

            SET_STRING_ELT(
                ret, i,
                Rf_mkCharLenCE(
                    str_cont.get(i).data()+m_start,
                    m_end-m_start, CE_UTF8
                )
            );
        }

        if (str_text) {
            utext_close(str_text);
            str_text = NULL;
        }
    }
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(if (str_text) utext_close(str_text);)
    }


/**
 * Extract first occurrence of a regex pattern in each string
 *
 * @param str character vector
 * @param pattern character vector
 * @param opts_regex list
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-20)
 */
SEXP ci_extract_first_regex(SEXP str, SEXP pattern, SEXP opts_regex)
{
    return ci__extract_firstlast_regex(str, pattern, opts_regex, true);
}


/**
 * Extract last occurrence of a regex pattern in each string
 *
 * @param str character vector
 * @param pattern character vector
 * @param opts_regex list
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-20)
 */
SEXP ci_extract_last_regex(SEXP str, SEXP pattern, SEXP opts_regex)
{
    return ci__extract_firstlast_regex(str, pattern, opts_regex, false);
}


/**
 * Extract all occurrences of a regex pattern in each string
 *
 * @param str character vector
 * @param pattern character vector
 * @param opts_regex list
 * @param simplify single logical value
 *
 * @return list of character vectors  or character matrix
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-20)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-10-24)
 *          added simplify param
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-27)
 *    FR #117: omit_no_match arg added
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-04)
 *    allow `simplify=NA`
 *
 * @version 1.0-2 (Marek Gagolewski, 2016-01-29)
 *    Issue #214: allow a regex pattern like `.*`  to match an empty string
 */
SEXP ci_extract_all_regex(SEXP str, SEXP pattern, SEXP simplify, SEXP omit_no_match, SEXP opts_regex)
{
    StriRegexMatcherOptions pattern_opts =
        StriContainerRegexPattern::getRegexOptions(opts_regex);
    bool omit_no_match1 = ci__prepare_arg_logical_1_notNA(omit_no_match, "omit_no_match");
    PROTECT(simplify = ci__prepare_arg_logical_1(simplify, "simplify"));
    PROTECT(str = ci__prepare_arg_string(str, "str")); // prepare string argument
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern")); // prepare string argument
    R_len_t vectorize_length = ci__recycling_rule(true, 2, LENGTH(str), LENGTH(pattern));

    UText* str_text = NULL; // may potentially be slower, but definitely is more convenient!
    STRI__ERROR_HANDLER_BEGIN(3)
    SEXP ret;
    if (XLENGTH(pattern) == 1) {
        try {
            vector<CiRegexExtractElement> elements(
                static_cast<size_t>(vectorize_length)
            );
            CiRegexExtractArena arena;
            ci__regex_extract_all_scalar(
                str, pattern, vectorize_length, pattern_opts,
                elements, arena
            );

            STRI__PROTECT(ret = Rf_allocVector(VECSXP, vectorize_length));
            for (R_len_t i = 0; i < vectorize_length; ++i) {
                const CiRegexExtractElement& element =
                    elements[static_cast<size_t>(i)];
                if (element.state == CiRegexExtractState::argument_na) {
                    SET_VECTOR_ELT(ret, i, ci__vector_NA_strings(1));
                    continue;
                }
                if (element.state == CiRegexExtractState::no_match) {
                    SET_VECTOR_ELT(
                        ret, i,
                        ci__vector_NA_strings(omit_no_match1 ? 0 : 1)
                    );
                    continue;
                }

                SEXP current;
                STRI__PROTECT(current = Rf_allocVector(
                    STRSXP, element.slices_count
                ));
                for (R_len_t j = 0; j < element.slices_count; ++j) {
                    const CiRegexExtractSlice& slice = arena.slice(
                        element.slices_begin + static_cast<size_t>(j)
                    );
                    SET_STRING_ELT(
                        current, j,
                        Rf_mkCharLenCE(
                            arena.data(slice), slice.length, CE_UTF8
                        )
                    );
                }
                SET_VECTOR_ELT(ret, i, current);
                STRI__UNPROTECT(1);
            }
        }
        catch (const StriException&) {
            throw;
        }
        catch (const exception& error) {
            throw StriException("%s", error.what());
        }
    }
    else {
        Utf8Input str_cont(str, vectorize_length);
        StriContainerRegexPattern pattern_cont(
            pattern, vectorize_length, pattern_opts
        );
        STRI__PROTECT(ret = Rf_allocVector(VECSXP, vectorize_length));

        for (R_len_t i = pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            STRI__CONTINUE_ON_EMPTY_OR_NA_PATTERN(
                str_cont, pattern_cont,
                SET_VECTOR_ELT(ret, i, ci__vector_NA_strings(1));
            )

            UErrorCode status = U_ZERO_ERROR;
            RegexMatcher *matcher = pattern_cont.getMatcher(i);
            str_text = utext_openUTF8(
                str_text, str_cont.get(i).data(),
                str_cont.get(i).length(), &status
            );
            STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
            matcher->reset(str_text);

            deque< pair<R_len_t, R_len_t> > occurrences;
            while (matcher->find(status)) {
                STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
                occurrences.push_back(pair<R_len_t, R_len_t>(
                    static_cast<R_len_t>(matcher->start(status)),
                    static_cast<R_len_t>(matcher->end(status))
                ));
                STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
            }
            STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})

            R_len_t noccurrences = static_cast<R_len_t>(occurrences.size());
            if (noccurrences <= 0) {
                SET_VECTOR_ELT(
                    ret, i,
                    ci__vector_NA_strings(omit_no_match1 ? 0 : 1)
                );
                continue;
            }

            const char* str_cur_s = str_cont.get(i).data();
            SEXP current;
            STRI__PROTECT(current = Rf_allocVector(
                STRSXP, noccurrences
            ));
            deque< pair<R_len_t, R_len_t> >::iterator iter =
                occurrences.begin();
            for (R_len_t j = 0; iter != occurrences.end(); ++iter, ++j) {
                pair<R_len_t, R_len_t> curo = *iter;
                SET_STRING_ELT(
                    current, j,
                    Rf_mkCharLenCE(
                        str_cur_s+curo.first,
                        curo.second-curo.first, CE_UTF8
                    )
                );
            }
            SET_VECTOR_ELT(ret, i, current);
            STRI__UNPROTECT(1);
        }

        if (str_text) {
            utext_close(str_text);
            str_text = NULL;
        }
    }

    if (LOGICAL(simplify)[0] == NA_LOGICAL || LOGICAL(simplify)[0]) {
        SEXP robj_TRUE, robj_zero, robj_na_strings, robj_empty_strings;
        STRI__PROTECT(robj_TRUE = Rf_ScalarLogical(TRUE));
        STRI__PROTECT(robj_zero = Rf_ScalarInteger(0));
        STRI__PROTECT(robj_na_strings = ci__vector_NA_strings(1));
        STRI__PROTECT(robj_empty_strings = ci__vector_empty_strings(1));
        STRI__PROTECT(ret = ci_list2matrix(ret, robj_TRUE,
                                             (LOGICAL(simplify)[0] == NA_LOGICAL)?robj_na_strings
                                             :robj_empty_strings,
                                             robj_zero));
    }

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(if (str_text) utext_close(str_text);)
    }

} } // namespace charr::base
