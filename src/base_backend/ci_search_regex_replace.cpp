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
#include "io/utf16_input.h"
#include "regex/pattern_set.h"
#include "io/utf8_input.h"

#include <exception>
#include <memory>
#include <stdexcept>
#include <unicode/ustring.h>


namespace charr { namespace base_backend {

namespace search_regex_replace {

class ReusableUtf16Text {
private:
    UnicodeString text_;

public:
    UnicodeString& set(const io::Utf8Record& value)
    {
        if (value.len <= 0) {
            text_.remove();
            return text_;
        }

        UChar* destination = text_.getBuffer(value.len);
        if (!destination)
            throw StriException(MSG__MEM_ALLOC_ERROR);

        int32_t length = 0;
        if (value.state == io::Utf8RecordState::ascii) {
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


void replace_matches(
    RegexMatcher* matcher, UnicodeString& subject,
    const UnicodeString& replacement, int type, UnicodeString& output
)
{
    matcher->reset(subject);
    UErrorCode status = U_ZERO_ERROR;

    if (type == 0) {
        output = matcher->replaceAll(replacement, status);
        STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
        return;
    }

    if (type == 1) {
        bool matched = static_cast<bool>(matcher->find(status));
        STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
        if (!matched) {
            output.setTo(subject);
            return;
        }

        output.remove();
        matcher->appendReplacement(output, replacement, status);
        STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
        matcher->appendTail(output);
        return;
    }

    if (type == -1) {
        int start = -1;
        int end = -1;
        while (matcher->find(status)) {
            STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
            start = matcher->start(status);
            STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
            end = matcher->end(status);
            STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
        }
        STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
        if (start < 0) {
            output.setTo(subject);
            return;
        }

        matcher->find(start, status);
        STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
        output.remove();
        matcher->appendReplacement(output, replacement, status);
        STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
        output.append(subject, end, subject.length()-end);
        return;
    }

    throw StriException(MSG__INTERNAL_ERROR);
}

} // namespace search_regex_replace

using namespace search_regex_replace;

/**
 * Replace occurrences of a regex pattern
 *
 * @param str strings to search in
 * @param pattern regex patterns to search for
 * @param replacement replacements
 * @param opts_regex list
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          use io::Utf16Input's vectorization
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-21)
 *          use regex::PatternSet + more general
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-07-10)
 *          BUGFIX: wrong behavior on empty str
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.0-2 (Marek Gagolewski, 2016-01-29)
 *    Issue #214: allow a regex pattern like `.*`  to match an empty string
 *
 * @version 1.0-2 (Marek Gagolewski, 2016-01-30)
 *    Issue #210: Allow NA replacement
 *
 * @version 1.4.7 (Marek Gagolewski, 2020-08-24)
 *    Use regex::PatternSet::getRegexOptions
 */
SEXP ci__replace_allfirstlast_regex(SEXP str, SEXP pattern, SEXP replacement, SEXP opts_regex, int type)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(replacement = ci__prepare_arg_string(replacement, "replacement"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));
    regex::Options pattern_opts =
        regex::PatternSet::getRegexOptions(opts_regex);

    STRI__ERROR_HANDLER_BEGIN(3)
    R_len_t vectorize_length = ci__recycling_rule(true, 3, LENGTH(str), LENGTH(pattern), LENGTH(replacement));
    SEXP ret;
    STRI__PROTECT(ret = Rf_allocVector(STRSXP, vectorize_length));

    try {
        io::Utf8Input subjects(
            str, vectorize_length, io::Utf8BomPolicy::preserve
        );
        regex::PatternSet pattern_cont(
            pattern, vectorize_length, pattern_opts
        );
        io::Utf8Input replacements(
            replacement, vectorize_length, io::Utf8BomPolicy::preserve
        );

        ReusableUtf16Text subject_text;
        ReusableUtf16Text replacement_text;
        UnicodeString output_text;
        std::string output_utf8;

        const bool scalar_pattern = vectorize_length > 0 &&
            XLENGTH(pattern) == 1;
        const bool scalar_replacement = vectorize_length > 0 &&
            XLENGTH(replacement) == 1;
        const UnicodeString* scalar_replacement_text = nullptr;
        if (scalar_replacement) {
            const io::Utf8Record scalar_record = replacements.record(0);
            if (!scalar_record.is_na())
                scalar_replacement_text = &replacement_text.set(scalar_record);
        }

        const bool scalar_pattern_unusable = scalar_pattern &&
            (pattern_cont.isNA(0) || pattern_cont.get(0).length() <= 0);
        RegexMatcher* scalar_matcher = nullptr;

        for (R_len_t i = 0; i < vectorize_length; ++i) {
            const io::Utf8Record subject_record = subjects.record(i);
            if (subject_record.is_na() || scalar_pattern_unusable ||
                    (!scalar_pattern && (pattern_cont.isNA(i) ||
                     pattern_cont.get(i).length() <= 0))) {
                SET_STRING_ELT(ret, i, NA_STRING);
                continue;
            }

            RegexMatcher* matcher;
            if (scalar_pattern) {
                if (!scalar_matcher)
                    scalar_matcher = pattern_cont.getMatcher(0);
                matcher = scalar_matcher;
            }
            else {
                matcher = pattern_cont.getMatcher(i);
            }

            UnicodeString& subject_value = subject_text.set(subject_record);
            const UnicodeString* replacement_value = scalar_replacement_text;
            if (!scalar_replacement) {
                const io::Utf8Record replacement_record =
                    replacements.record(i);
                if (!replacement_record.is_na())
                    replacement_value = &replacement_text.set(
                        replacement_record
                    );
            }
            if (!replacement_value) {
                matcher->reset(subject_value);
                UErrorCode status = U_ZERO_ERROR;
                const bool matched = static_cast<bool>(matcher->find(status));
                STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})
                if (matched) {
                    SET_STRING_ELT(ret, i, NA_STRING);
                    continue;
                }
                output_text.setTo(subject_value);
            }
            else {
                replace_matches(
                    matcher, subject_value, *replacement_value,
                    type, output_text
                );
            }

            output_utf8.clear();
            output_text.toUTF8String(output_utf8);
            if (output_utf8.size() > static_cast<size_t>(R_LEN_T_MAX))
                throw std::length_error(
                    "character output exceeds R's string length limit"
                );
            SET_STRING_ELT(
                ret, i,
                Rf_mkCharLenCE(
                    output_utf8.data(), static_cast<int>(output_utf8.size()),
                    CE_UTF8
                )
            );
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
 * Replace all occurrences of a regex pattern; vectorize_all=FALSE
 *
 * @param str character vector
 * @param pattern character vector
 * @param replacement character vector
 * @param opts_regex a named list
 * @return character vector
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-01)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-02)
 *          Second version, 3x faster, 2 for loops + replaceAll
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.0-2 (Marek Gagolewski, 2016-01-30)
 *    Issue #210: Allow NA replacement
 */
SEXP ci__replace_all_regex_no_vectorize_all(SEXP str, SEXP pattern, SEXP replacement, SEXP opts_regex)
{   // version beta
    PROTECT(str          = ci__prepare_arg_string(str, "str"));

    // if str_n is 0, then return an empty vector
    R_len_t str_n = LENGTH(str);
    if (str_n <= 0) {
        UNPROTECT(1);
        return ci__vector_empty_strings(0);
    }

    PROTECT(pattern      = ci__prepare_arg_string(pattern, "pattern"));
    PROTECT(replacement  = ci__prepare_arg_string(replacement, "replacement"));
    regex::Options pattern_opts =
        regex::PatternSet::getRegexOptions(opts_regex);

    R_len_t pattern_n = LENGTH(pattern);
    R_len_t replacement_n = LENGTH(replacement);
    if (pattern_n < replacement_n || pattern_n <= 0 || replacement_n <= 0) {
        UNPROTECT(3);
        Rf_error(MSG__WARN_RECYCLING_RULE2);
    }
    else if (pattern_n % replacement_n != 0)
        r_warning(MSG__WARN_RECYCLING_RULE);

    if (pattern_n == 1) {// this will be much faster:
        SEXP ret;
        PROTECT(ret = ci__replace_allfirstlast_regex(str, pattern, replacement, opts_regex, 0));
        UNPROTECT(4);
        return ret;
    }

    STRI__ERROR_HANDLER_BEGIN(3)
    io::Utf16Output str_cont(str, str_n); // writable
    regex::PatternSet pattern_cont(pattern, pattern_n, pattern_opts);
    io::Utf16Input replacement_cont(replacement, pattern_n);

    for (R_len_t i = 0; i<pattern_n; ++i)
    {
        if (pattern_cont.isNA(i)) {
            STRI__UNPROTECT_ALL
            return ci__vector_NA_strings(str_n);
        }
        else if (pattern_cont.get(i).length() <= 0) {
            r_warning(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
            STRI__UNPROTECT_ALL
            return ci__vector_NA_strings(str_n);
        }

        RegexMatcher *matcher = pattern_cont.getMatcher(i); // will be deleted automatically

        for (R_len_t j = 0; j<str_n; ++j) {
            if (str_cont.isNA(j)) continue;

            matcher->reset(str_cont.get(j));

            UErrorCode status = U_ZERO_ERROR;

            if (replacement_cont.isNA(i)) {
                int m_res = matcher->find(status);
                STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
                if (m_res)
                    str_cont.setNA(j);
                continue;
            }


            str_cont.set(j, matcher->replaceAll(replacement_cont.get(i), status));
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
        }
    }

    STRI__UNPROTECT_ALL
    return str_cont.toR();
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


// version alpha == to slow == too many toutf16 conversions
//{
//   PROTECT(pattern      = ci__prepare_arg_string(pattern, "pattern"));
//   PROTECT(replacement  = ci__prepare_arg_string(replacement, "replacement"));
//
//   R_len_t pattern_n = LENGTH(pattern);
//   R_len_t replacement_n = LENGTH(replacement);
//   if (pattern_n < replacement_n || pattern_n <= 0 || replacement_n <= 0)
//      Rf_error(MSG__WARN_RECYCLING_RULE2);
//   if (pattern_n % replacement_n != 0)
//      Rf_warning(MSG__WARN_RECYCLING_RULE);
//
//   // no str_error_handlers needed here
//   SEXP pattern_cur, replacement_cur;
//   PROTECT(pattern_cur = Rf_allocVector(STRSXP, 1));
//   PROTECT(replacement_cur = Rf_allocVector(STRSXP, 1));
//
//   PROTECT(str);
//   for (R_len_t i=0; i<pattern_n; ++i) {
//      SET_STRING_ELT(pattern_cur, 0, STRING_ELT(pattern, i));
//      SET_STRING_ELT(replacement_cur, 0, STRING_ELT(replacement, i%replacement_n));
//      str = ci__replace_allfirstlast_regex(str, pattern_cur, replacement_cur, opts_regex, 0);
//      UNPROTECT(1);
//      PROTECT(str);
//   }
//
//   UNPROTECT(5);
//   return str;
//}


/**
 * Replace all occurrences of a regex pattern
 *
 * @param str strings to search in
 * @param pattern regex patterns to search for
 * @param replacement replacements
 * @param opts_regex list
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-21)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-01)
 *          vectorize_all argument added
 */
SEXP ci_replace_all_regex(SEXP str, SEXP pattern, SEXP replacement, SEXP vectorize_all, SEXP opts_regex)
{
    if (ci__prepare_arg_logical_1_notNA(vectorize_all, "vectorize_all"))
        return ci__replace_allfirstlast_regex(str, pattern, replacement, opts_regex, 0);
    else
        return ci__replace_all_regex_no_vectorize_all(str, pattern, replacement, opts_regex);
}


/**
 * Replace first occurrence of a regex pattern
 *
 * @param str strings to search in
 * @param pattern regex patterns to search for
 * @param replacement replacements
 * @param opts_regex list
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-21)
 */
SEXP ci_replace_first_regex(SEXP str, SEXP pattern, SEXP replacement, SEXP opts_regex)
{
    return ci__replace_allfirstlast_regex(str, pattern, replacement, opts_regex, 1);
}


} } // namespace charr::base_backend
