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
#include "ci_builder.h"
#include "ci_utf8.h"
#include "ci_container_utf16.h"
#include "ci_container_regex.h"
#include "altrep/utf8_input.h"

#include <memory>
#include <unicode/ustring.h>


namespace {

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

} // namespace


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
 *          use StriContainerUTF16's vectorization
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-21)
 *          use StriContainerRegexPattern + more general
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
 *    Use StriContainerRegexPattern::getRegexOptions
 */
SEXP ci__replace_allfirstlast_regex(SEXP str, SEXP pattern, SEXP replacement, SEXP opts_regex, int type)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(replacement = ci__prepare_arg_string(replacement, "replacement"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));

    STRI__ERROR_HANDLER_BEGIN(3)
    StriRegexMatcherOptions pattern_opts;
    // Deviation from stringi: keep option parsing in its copied position but
    // queue controlled option warnings through the operation boundary.
    charport::unwind_protect([&]() -> SEXP {
        pattern_opts = StriContainerRegexPattern::getRegexOptions(
            STRI__DEFERRED_WARNINGS, opts_regex
        );
        return R_NilValue;
    });
    SEXP ret;
    {
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    R_len_t str_n = ci::checked_r_len(
        context.size(str), "character vectors"
    );
    R_len_t pattern_n = ci::checked_r_len(
        context.size(pattern), "character vectors"
    );
    R_len_t replacement_n = ci::checked_r_len(
        context.size(replacement), "character vectors"
    );
    R_len_t vectorize_length = 0;
    charport::unwind_protect([&]() -> SEXP {
        vectorize_length = ci__recycling_rule(
            false, 3, str_n, pattern_n, replacement_n
        );
        return R_NilValue;
    });
    // Deviation from stringi: queue the recycling warning until regex/Reader
    // owners, writable UTF-16 staging, and the output Builder are released.
    if (vectorize_length > 0 &&
            (vectorize_length % str_n != 0 ||
             vectorize_length % pattern_n != 0 ||
             vectorize_length % replacement_n != 0))
        context.warn(MSG__WARN_RECYCLING_RULE);

    charport::charvec::Builder builder(vectorize_length);
    {
        charr::altrep::Utf8Input subjects(
            context, str, vectorize_length, true,
            charr::altrep::Utf8BomPolicy::preserve
        );
        StriContainerRegexPattern pattern_cont(
            context, pattern, vectorize_length, pattern_opts
        );
        charr::altrep::Utf8Input replacements(
            context, replacement, vectorize_length, true,
            charr::altrep::Utf8BomPolicy::preserve
        );

        ReusableUtf16Text subject_text;
        ReusableUtf16Text replacement_text;
        UnicodeString output_text;
        std::vector<char> utf8_buffer;

        const bool scalar_pattern = vectorize_length > 0 && pattern_n == 1;
        const bool scalar_replacement = vectorize_length > 0 &&
            replacement_n == 1;
        const UnicodeString* scalar_replacement_text = nullptr;
        if (scalar_replacement) {
            const charport::StrView scalar_record =
                replacements.record(0).view();
            if (!scalar_record.is_na())
                scalar_replacement_text = &replacement_text.set(scalar_record);
        }

        const bool scalar_pattern_unusable = scalar_pattern &&
            (pattern_cont.isNA(0) || pattern_cont.get(0).length() <= 0);
        RegexMatcher* scalar_matcher = nullptr;

        for (R_len_t i = 0; i < vectorize_length; ++i) {
            const charport::StrView subject_record =
                subjects.record(i).view();
            if (subject_record.is_na() || scalar_pattern_unusable ||
                    (!scalar_pattern && (pattern_cont.isNA(i) ||
                     pattern_cont.get(i).length() <= 0))) {
                builder.set_na(i);
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
                const charport::StrView replacement_record =
                    replacements.record(i).view();
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
                    builder.set_na(i);
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

            ci::builder_set(
                builder, i, output_text, utf8_buffer
            );
        }
    }

    STRI__PROTECT(ret = builder.to_sexp());
    }
    STRI__DEFERRED_WARNINGS.emit();
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

    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP ret;
    {
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);

    // if str_n is 0, then return an empty vector
    R_len_t str_n = ci::checked_r_len(
        context.size(str), "character vectors"
    );
    if (str_n <= 0) {
        charport::charvec::Builder builder(0);
        STRI__PROTECT(ret = builder.to_sexp());
        STRI__UNPROTECT_ALL
        return ret;
    }

    // Deviation from stringi: lazy preparation now runs inside the C++
    // boundary, so queue its controlled warnings with the operation.
    STRI__PROTECT(pattern = charport::unwind_protect([&]() -> SEXP {
        return ci__prepare_arg_string(
            pattern, "pattern", true, &STRI__DEFERRED_WARNINGS
        );
    }));
    STRI__PROTECT(replacement = charport::unwind_protect([&]() -> SEXP {
        return ci__prepare_arg_string(
            replacement, "replacement", true,
            &STRI__DEFERRED_WARNINGS
        );
    }));
    StriRegexMatcherOptions pattern_opts;
    charport::unwind_protect([&]() -> SEXP {
        pattern_opts = StriContainerRegexPattern::getRegexOptions(
            STRI__DEFERRED_WARNINGS, opts_regex
        );
        return R_NilValue;
    });

    R_len_t pattern_n = ci::checked_r_len(
        context.size(pattern), "character vectors"
    );
    R_len_t replacement_n = ci::checked_r_len(
        context.size(replacement), "character vectors"
    );
    // Deviation from stringi: a controlled length error crosses the outer
    // C++ boundary before it is signalled to R.
    charport::unwind_protect([&]() -> SEXP {
        if (pattern_n < replacement_n || pattern_n <= 0 || replacement_n <= 0)
            throw StriException(MSG__WARN_RECYCLING_RULE2);
        return R_NilValue;
    });
    // Deviation from stringi: queue this sequential-substitution recycling
    // warning until regex/Reader owners and the output Builder are released.
    if (pattern_n % replacement_n != 0)
        context.warn(MSG__WARN_RECYCLING_RULE);

    if (pattern_n == 1) {// this will be much faster:
        // Deviation from stringi: the delegated scalar-pattern path parses
        // options again. Replay this call's queued diagnostics before entering
        // it, while no Reader, regex, or output owner is active, to preserve
        // stringi's two-parser warning order (and warn=2 short-circuit).
        context.emitWarnings();
        STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
            return ci__replace_allfirstlast_regex(
                str, pattern, replacement, opts_regex, 0
            );
        }));
        STRI__UNPROTECT_ALL
        return ret;
    }

    charport::charvec::Builder builder(str_n);
    {
        StriContainerUTF16 str_cont(context, str, str_n, false); // writable
        StriContainerRegexPattern pattern_cont(
            context, pattern, pattern_n, pattern_opts
        );
        StriContainerUTF16 replacement_cont(
            context, replacement, pattern_n
        );
        bool return_all_na = false;

        for (R_len_t i = 0; i<pattern_n; ++i)
        {
            if (pattern_cont.isNA(i)) {
                // Deviation from stringi: stage the all-NA return until the
                // Reader-backed containers have been destroyed.
                return_all_na = true;
                break;
            }
            else if (pattern_cont.get(i).length() <= 0) {
                // StriContainerRegexPattern already queued stringi's first
                // warning; preserve this sequential path's second warning.
                context.warn(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
                return_all_na = true;
                break;
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

        std::vector<char> utf8_buffer;
        for (R_len_t j=0; j<str_n; ++j) {
            if (return_all_na || str_cont.isNA(j))
                builder.set_na(j);
            else
                ci::builder_set(builder, j, str_cont.get(j), utf8_buffer);
        }
    }

    STRI__PROTECT(ret = builder.to_sexp());
    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
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


/**
 * Replace last occurrence of a regex pattern
 *
 * @param str strings to search in
 * @param pattern regex patterns to search for
 * @param replacement replacements
 * @param opts_regex list
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-21)
 */
SEXP ci_replace_last_regex(SEXP str, SEXP pattern, SEXP replacement, SEXP opts_regex)
{
    return ci__replace_allfirstlast_regex(str, pattern, replacement, opts_regex, -1);
}






/**
 * Converts a single gsub to ci_replace replacement string
 *
 * @param x
 * @return UTF-8 bytes
 */
std::string ci__replace_rstr_1(const Utf8Record& _x)
{
    STRI_ASSERT(!_x.isNA());
    R_len_t n = _x.length();
    const char* x = _x.data();

    std::string buf;
    // Deviation from stringi: widen before adding so a maximum-length input
    // cannot overflow R_len_t while calculating the reserve size.
    buf.reserve(static_cast<size_t>(n)+1);  // whatever

    R_len_t i=0;
    while (i < n) {
        if (x[i] == '$')
            buf.append("\\$");
        else if (x[i] == '\\') {
            i++;
            if (i >= n)  {
                // dangling backslash
                //throw StriException(MSG__INVALID_FORMAT_SPECIFIER, "");
                // gsub compatibility:
                break;
            }

            if (x[i] == '$')
                buf.append("\\$");
            else if (x[i] == '\\')
                buf.append("\\\\");
            else if (x[i] >= '1' && x[i] <= '9') {  // \\0 not supported
                buf.push_back('$');
                buf.push_back(x[i]);
                if (i+1 < n && (x[i+1] >= '0' && x[i+1] <= '9'))
                    buf.push_back('\\');
            }
            else
                buf.push_back(x[i]);
        }
        else
            buf.push_back(x[i]);

        i++;
    }

    return buf;
}



/**
 * Convert \1 to $1 and $ to \$ and \a to a
 * (gsub vs. ci_replace replacement strings)
 *
 * @param x character vector
 *
 * @return character vector
 *
 * @version 1.6.4 (Marek Gagolewski, 2021-06-16)
 */
SEXP ci_replace_rstr(SEXP x)
{
    PROTECT(x = ci__prepare_arg_string(x, "x"));

    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP ret;
    {
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    R_len_t vectorize_length = ci::checked_r_len(
        context.size(x), "character vectors"
    );
    charport::charvec::Builder builder(vectorize_length);
    {
        Utf8Input x_cont(context, x, vectorize_length);

        for (
            R_len_t i = x_cont.vectorize_init();
            i != x_cont.vectorize_end();
            i = x_cont.vectorize_next(i)
        ) {
            if (x_cont.isNA(i)) {
                builder.set_na(i);
                continue;
            }

            std::string out = ci__replace_rstr_1(x_cont.get(i));
            // Deviation from stringi: reject expanded output before narrowing
            // its length to the R string-length type.
            if (out.size() > static_cast<size_t>(R_LEN_T_MAX))
                throw std::length_error(
                    "replacement string exceeds R's string length limit"
                );
            const char* out_data = out.empty() ? "" : out.data();
            ci::builder_set(
                builder, i, out_data, static_cast<int>(out.size()),
                cetype_ext_t::CE_ASCII_OR_UTF8
            );
        }
    }

    STRI__PROTECT(ret = builder.to_sexp());
    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}
