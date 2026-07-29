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
#include "ci_string8buf.h"
//#include "ci_interval.h"
#include <cstdint>
#include <cstring>
#include <deque>
namespace charr { namespace base_backend {

//#include <queue>
//#include <algorithm>
using namespace std;


namespace search_fixed_replace {

struct CiDirectString {
    const char* data;
    R_len_t length;
    bool has_bom;
};


bool ci__direct_charsxp(SEXP value, CiDirectString& output)
{
    if (value == NA_STRING) {
        output = {NULL, NA_INTEGER, false};
        return true;
    }
    if (!IS_ASCII(value) && !IS_UTF8(value))
        return false;

    output.data = CHAR(value);
    output.length = LENGTH(value);
    output.has_bom = IS_UTF8(value) && STRI__ENC_HAS_BOM_UTF8(
        output.data, output.length
    );
    if (output.has_bom) {
        output.data += 3;
        output.length -= 3;
    }
    return true;
}


R_len_t ci__find_byte_first(
    const char* data, R_len_t length, unsigned char pattern
)
{
    const void* found = std::memchr(
        data, pattern, static_cast<size_t>(length)
    );
    return found == NULL
        ? shared::ByteSearchMatcher::not_found
        : static_cast<R_len_t>(static_cast<const char*>(found)-data);
}


R_len_t ci__find_byte_last(
    const char* data, R_len_t length, unsigned char pattern
)
{
    for (R_len_t i = length; i > 0; --i) {
        if (static_cast<unsigned char>(data[i-1]) == pattern)
            return i-1;
    }
    return shared::ByteSearchMatcher::not_found;
}


size_t ci__count_byte(
    const char* data, R_len_t length, unsigned char pattern
)
{
    size_t count = 0;
    for (R_len_t i = 0; i < length; ++i)
        count += static_cast<unsigned char>(data[i]) == pattern;
    return count;
}


R_len_t ci__replacement_size(
    R_len_t source_length, R_len_t replacement_length,
    size_t count, size_t matched_bytes
)
{
    const size_t source_size = static_cast<size_t>(source_length);
    if (matched_bytes > source_size)
        throw StriException(MSG__CHARSXP_2147483647);
    const size_t unmatched = source_size-matched_bytes;
    const size_t maximum = static_cast<size_t>(R_LEN_T_MAX);
    if (replacement_length > 0 &&
            count > (maximum-unmatched) /
                static_cast<size_t>(replacement_length)) {
        throw StriException(MSG__CHARSXP_2147483647);
    }
    return static_cast<R_len_t>(
        unmatched+count*static_cast<size_t>(replacement_length)
    );
}


R_len_t ci__checked_replacement_match_end(
    R_len_t start, R_len_t length, R_len_t source_length,
    R_len_t previous_end
)
{
    if (source_length < 0 || start < previous_end || length <= 0 ||
            start > source_length || length > source_length-start) {
        throw StriException("fixed replacement match is out of bounds");
    }
    return start+length;
}


void ci__write_one_byte_replacement(
    char* output, const CiDirectString& source, R_len_t match,
    const CiDirectString& replacement
)
{
    if (match > 0)
        std::memcpy(output, source.data, static_cast<size_t>(match));
    if (replacement.length > 0) {
        std::memcpy(
            output+match, replacement.data,
            static_cast<size_t>(replacement.length)
        );
    }
    const R_len_t suffix = source.length-match-1;
    if (suffix > 0) {
        std::memcpy(
            output+match+replacement.length, source.data+match+1,
            static_cast<size_t>(suffix)
        );
    }
}


void ci__write_all_byte_replacements(
    char* output, const CiDirectString& source, unsigned char pattern,
    const CiDirectString& replacement
)
{
    if (replacement.length == 1) {
        if (source.length > 0) {
            std::memcpy(
                output, source.data, static_cast<size_t>(source.length)
            );
        }
        for (R_len_t i = 0; i < source.length; ++i) {
            if (static_cast<unsigned char>(output[i]) == pattern) {
                output[i] = replacement.data[0];
            }
        }
        return;
    }

    size_t used = 0;
    R_len_t previous = 0;
    for (R_len_t i = 0; i < source.length; ++i) {
        if (static_cast<unsigned char>(source.data[i]) != pattern)
            continue;
        const size_t prefix = static_cast<size_t>(i-previous);
        if (prefix > 0) {
            std::memcpy(output+used, source.data+previous, prefix);
            used += prefix;
        }
        if (replacement.length > 0) {
            std::memcpy(
                output+used, replacement.data,
                static_cast<size_t>(replacement.length)
            );
            used += static_cast<size_t>(replacement.length);
        }
        previous = i+1;
    }
    const size_t suffix = static_cast<size_t>(source.length-previous);
    if (suffix > 0)
        std::memcpy(output+used, source.data+previous, suffix);
}


bool ci__replace_scalar_byte_direct(
    SEXP str, SEXP pattern, SEXP replacement,
    R_len_t vectorize_length, uint32_t pattern_flags, int type,
    SEXP result, String8buf& buffer, R_len_t& general_start
)
{
    if (vectorize_length == 0)
        return true;
    if (pattern_flags != 0 || LENGTH(pattern) != 1 ||
            LENGTH(replacement) != 1) {
        return false;
    }

    CiDirectString pattern_value;
    CiDirectString replacement_value;
    if (!ci__direct_charsxp(STRING_ELT(pattern, 0), pattern_value) ||
            pattern_value.data == NULL || pattern_value.length != 1 ||
            !ci__direct_charsxp(
                STRING_ELT(replacement, 0), replacement_value
            )) {
        return false;
    }

    const unsigned char pattern_byte = static_cast<unsigned char>(
        pattern_value.data[0]
    );
    for (R_len_t i = 0; i < vectorize_length; ++i) {
        SEXP source_sexp = STRING_ELT(str, i);
        CiDirectString source;
        if (!ci__direct_charsxp(source_sexp, source)) {
            general_start = i;
            return false;
        }
        if (source.data == NULL) {
            SET_STRING_ELT(result, i, NA_STRING);
            continue;
        }

        R_len_t match = shared::ByteSearchMatcher::not_found;
        size_t count = 0;
        if (type == 0) {
            if (replacement_value.data == NULL ||
                    replacement_value.length == 1) {
                match = ci__find_byte_first(
                    source.data, source.length, pattern_byte
                );
            }
            else {
                count = ci__count_byte(
                    source.data, source.length, pattern_byte
                );
            }
            if (count > 0)
                match = 0;
        }
        else if (type > 0) {
            match = ci__find_byte_first(
                source.data, source.length, pattern_byte
            );
        }
        else {
            match = ci__find_byte_last(
                source.data, source.length, pattern_byte
            );
        }

        if (match == shared::ByteSearchMatcher::not_found) {
            if (!source.has_bom) {
                SET_STRING_ELT(result, i, source_sexp);
            }
            else {
                SET_STRING_ELT(
                    result, i,
                    Rf_mkCharLenCE(source.data, source.length, CE_UTF8)
                );
            }
            continue;
        }
        if (replacement_value.data == NULL) {
            SET_STRING_ELT(result, i, NA_STRING);
            continue;
        }

        const R_len_t output_length = type == 0 &&
                replacement_value.length == 1
            ? source.length
            : ci__replacement_size(
                source.length, replacement_value.length,
                type == 0 ? count : 1,
                type == 0 ? count : 1
            );
        if (type == 0) {
            buffer.resize(static_cast<size_t>(output_length), false);
            ci__write_all_byte_replacements(
                buffer.data(), source, pattern_byte,
                replacement_value
            );
        }
        else {
            buffer.resize(static_cast<size_t>(output_length), false);
            ci__write_one_byte_replacement(
                buffer.data(), source, match, replacement_value
            );
        }
        SET_STRING_ELT(
            result, i,
            Rf_mkCharLenCE(buffer.data(), output_length, CE_UTF8)
        );
    }
    return true;
}

} // namespace search_fixed_replace

using namespace search_fixed_replace;


/**
 * Replace all/first/last occurrences of a fixed pattern
 *
 * @param str character vector
 * @param pattern character vector
 * @param replacement character vector
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-07-10)
 *          BUGFIX: wrong behavior on empty str
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          ci_replace_fixed now uses byte search only
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-02)
 *          using String8buf::replaceAllAtPos, slightly faster
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-07)
 *    FR #110, #23: opts_fixed arg added
 *
 * @version 1.0-2 (Marek Gagolewski, 2016-01-30)
 *    Issue #210: Allow NA replacement
 */
SEXP ci__replace_allfirstlast_fixed(SEXP str, SEXP pattern, SEXP replacement, SEXP opts_fixed, int type)
{
    uint32_t pattern_flags = fixed::PatternSet::getByteSearchFlags(opts_fixed);
    PROTECT(str          = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern      = ci__prepare_arg_string(pattern, "pattern"));
    PROTECT(replacement  = ci__prepare_arg_string(replacement, "replacement"));
    R_len_t vectorize_length = ci__recycling_rule(true, 3, LENGTH(str), LENGTH(pattern), LENGTH(replacement));

    STRI__ERROR_HANDLER_BEGIN(3)
    SEXP ret;
    STRI__PROTECT(ret = Rf_allocVector(STRSXP, vectorize_length));

    String8buf buf(0);
    R_len_t general_start = 0;
    if (ci__replace_scalar_byte_direct(
            str, pattern, replacement, vectorize_length,
            pattern_flags, type, ret, buf, general_start)) {
        STRI__UNPROTECT_ALL
        return ret;
    }

    io::Utf8Input str_cont(str, vectorize_length);
    io::Utf8Input replacement_cont(replacement, vectorize_length);
    fixed::PatternSet pattern_cont(pattern, vectorize_length, pattern_flags);

    for (R_len_t i = general_start > 0
                ? general_start
                : pattern_cont.vectorize_init();
            i != pattern_cont.vectorize_end();
            i = pattern_cont.vectorize_next(i))
    {
        STRI__CONTINUE_ON_EMPTY_OR_NA_STR_PATTERN(str_cont, pattern_cont,
                SET_STRING_ELT(ret, i, NA_STRING);,
                SET_STRING_ELT(ret, i, Rf_mkCharLenCE(NULL, 0, CE_UTF8));)

        shared::ByteSearchMatcher* matcher = pattern_cont.getMatcher(i);
        matcher->reset(str_cont.get(i).data(), str_cont.get(i).length());
        R_len_t start;
        if (type >= 0) { // first or all
            start = matcher->find_first();
        } else {
            start = matcher->find_last();
        }

        if (start == shared::ByteSearchMatcher::not_found) {
            const io::Utf8Record& original = str_cont.get(i);
            SET_STRING_ELT(
                ret, i, Rf_mkCharLenCE(
                    original.data(), original.length(),
                    original.isASCII() ? CE_NATIVE : CE_UTF8
                )
            );
            continue;
        }

        if (replacement_cont.isNA(i)) {
            SET_STRING_ELT(ret, i, NA_STRING);
            continue;
        }

        R_len_t str_cur_n = str_cont.get(i).length();
        R_len_t len = matcher->matched_length();
        R_len_t end = ci__checked_replacement_match_end(
            start, len, str_cur_n, 0
        );
        size_t matched_bytes = static_cast<size_t>(len);
        deque< pair<R_len_t, R_len_t> > occurrences;
        occurrences.push_back(pair<R_len_t, R_len_t>(start, end));
        R_len_t previous_end = end;

        if (type == 0) {
            while (shared::ByteSearchMatcher::not_found != matcher->find_next()) { // all
                start = matcher->matched_start();
                len = matcher->matched_length();
                end = ci__checked_replacement_match_end(
                    start, len, str_cur_n, previous_end
                );
                occurrences.push_back(pair<R_len_t, R_len_t>(start, end));
                matched_bytes += static_cast<size_t>(len);
                previous_end = end;
            }
        }

        R_len_t replacement_cur_n = replacement_cont.get(i).length();
        R_len_t buf_need = ci__replacement_size(
            str_cur_n, replacement_cur_n,
            occurrences.size(), matched_bytes
        );
        buf.resize(buf_need, false/*destroy contents*/);

        size_t buf_used = buf.replaceAllAtPos(
            str_cont.get(i).data(), str_cur_n,
            replacement_cont.get(i).data(), replacement_cur_n,
            occurrences
        );

        if (static_cast<size_t>(buf_need) != buf_used)
            throw StriException("fixed replacement size mismatch");

        SET_STRING_ELT(ret, i, Rf_mkCharLenCE(buf.data(), buf_need, CE_UTF8));
    }

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


// Version 2, 2014-11-02, using in-place replacement, slower
//SEXP ci__replace_allfirstlast_fixed(SEXP str, SEXP pattern, SEXP replacement, int type)
//{
//   str          = ci__prepare_arg_string(str, "str");
//   pattern      = ci__prepare_arg_string(pattern, "pattern");
//   replacement  = ci__prepare_arg_string(replacement, "replacement");
//   R_len_t vectorize_length = ci__recycling_rule(true, 3, LENGTH(str), LENGTH(pattern), LENGTH(replacement));
//
//   STRI__ERROR_HANDLER_BEGIN
//   io::Utf8Workspace str_cont(str, vectorize_length);
//   io::Utf8Input replacement_cont(replacement, vectorize_length);
//   fixed::PatternSet pattern_cont(pattern, vectorize_length);
//
//   for (R_len_t i = pattern_cont.vectorize_init();
//         i != pattern_cont.vectorize_end();
//         i = pattern_cont.vectorize_next(i))
//   {
//      STRI__CONTINUE_ON_EMPTY_OR_NA_STR_PATTERN(str_cont, pattern_cont,
//         str_cont.setNA(i),
//         {/* zero-length string, just continue */})
//
//      if (replacement_cont.isNA(i)) {
//         str_cont.setNA(i);
//         continue;
//      }
//
//      R_len_t start;
//      if (type >= 0) { // first or all
//         pattern_cont.setupMatcherFwd(i, str_cont.get(i).data(), str_cont.get(i).length());
//         start = pattern_cont.findFirst();
//      } else {
//         pattern_cont.setupMatcherBack(i, str_cont.get(i).data(), str_cont.get(i).length());
//         start = pattern_cont.findLast();
//      }
//
//      if (start == shared::ByteSearchMatcher::not_found) {
//         // nothing to do, no change, leave as-is
//         continue;
//      }
//
//      R_len_t len = pattern_cont.getMatchedLength();
//      R_len_t sumbytes = len;
//      deque< pair<R_len_t, R_len_t> > occurrences;
//      occurrences.push_back(pair<R_len_t, R_len_t>(start, start+len));
//
//      if (type == 0) {
//         while (shared::ByteSearchMatcher::not_found != pattern_cont.findNext()) { // all
//            start = pattern_cont.getMatchedStart();
//            len = pattern_cont.getMatchedLength();
//            occurrences.push_back(pair<R_len_t, R_len_t>(start, start+len));
//            sumbytes += len;
//         }
//      }
//
//      R_len_t str_cur_n     = str_cont.get(i).length();
//      R_len_t     replacement_cur_n = replacement_cont.get(i).length();
//      R_len_t buf_need =
//         str_cur_n+replacement_cur_n*(R_len_t)occurrences.size()-sumbytes;
//
//      str_cont.getWritable(i).replaceAllAtPos(buf_need,
//         replacement_cont.get(i).data(), replacement_cur_n,
//         occurrences);
//   }
//
//   STRI__UNPROTECT_ALL
//   return str_cont.toR();
//   STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
//}


/**
 * Replace all occurrences of a fixed pattern; vectorize_all=FALSE
 *
 * @param str character vector
 * @param pattern character vector
 * @param replacement character vector
 * @return character vector
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-01)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-02)
 *                Complete rewrite; faster
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-07)
 *    FR #110, #23: opts_fixed arg added
 *
 * @version 1.0-2 (Marek Gagolewski, 2016-01-30)
 *    Issue #210: Allow NA replacement
 */
SEXP ci__replace_all_fixed_no_vectorize_all(SEXP str, SEXP pattern, SEXP replacement, SEXP opts_fixed)
{   // version gamma:
    PROTECT(str          = ci__prepare_arg_string(str, "str"));

    // if str_n is 0, then return an empty vector
    R_len_t str_n = LENGTH(str);
    if (str_n <= 0) {
        UNPROTECT(1);
        return ci__vector_empty_strings(0);
    }

    PROTECT(pattern      = ci__prepare_arg_string(pattern, "pattern"));
    PROTECT(replacement  = ci__prepare_arg_string(replacement, "replacement"));

    R_len_t pattern_n = LENGTH(pattern);
    R_len_t replacement_n = LENGTH(replacement);
    if (pattern_n < replacement_n || pattern_n <= 0 || replacement_n <= 0) {
        UNPROTECT(3);
        Rf_error(MSG__WARN_RECYCLING_RULE2);
    }
    if (pattern_n % replacement_n != 0)
        r_warning(MSG__WARN_RECYCLING_RULE);

    if (pattern_n == 1) { // this will be much faster:
        SEXP ret;
        PROTECT(ret = ci__replace_allfirstlast_fixed(str, pattern, replacement, opts_fixed, 0));
        UNPROTECT(4);
        return ret;
    }

    uint32_t pattern_flags = fixed::PatternSet::getByteSearchFlags(opts_fixed);

    STRI__ERROR_HANDLER_BEGIN(3)
    io::Utf8Workspace str_cont(str, str_n);
    io::Utf8Input replacement_cont(replacement, pattern_n);
    fixed::PatternSet pattern_cont(pattern, pattern_n, pattern_flags);

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

        shared::ByteSearchMatcher* matcher = pattern_cont.getMatcher(i);
        for (R_len_t j = 0; j<str_n; ++j) {
            if (str_cont.isNA(j)) continue;
            matcher->reset(str_cont.get(j).data(), str_cont.get(j).length());
            R_len_t start = matcher->find_first();
            if (start == shared::ByteSearchMatcher::not_found)  continue;  // nothing to do now

            if (replacement_cont.isNA(i)) {
                str_cont.set_na(j);
                continue;
            }

            R_len_t str_cur_n = str_cont.get(j).length();
            R_len_t len = matcher->matched_length();
            R_len_t end = ci__checked_replacement_match_end(
                start, len, str_cur_n, 0
            );
            size_t matched_bytes = static_cast<size_t>(len);
            deque< pair<R_len_t, R_len_t> > occurrences;
            occurrences.push_back(pair<R_len_t, R_len_t>(start, end));
            R_len_t previous_end = end;

            while (shared::ByteSearchMatcher::not_found != matcher->find_next()) { // all
                start = matcher->matched_start();
                len = matcher->matched_length();
                end = ci__checked_replacement_match_end(
                    start, len, str_cur_n, previous_end
                );
                occurrences.push_back(pair<R_len_t, R_len_t>(start, end));
                matched_bytes += static_cast<size_t>(len);
                previous_end = end;
            }

            R_len_t replacement_cur_n = replacement_cont.get(i).length();
            R_len_t buf_need = ci__replacement_size(
                str_cur_n, replacement_cur_n,
                occurrences.size(), matched_bytes
            );

            str_cont.replace_all_at_pos(
                j, buf_need, replacement_cont.get(i).data(),
                replacement_cur_n, occurrences
            );
        }
    }

    STRI__UNPROTECT_ALL
    return str_cont.to_sexp();
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}

// ci__replace_all_fixed_no_vectorize_all
//{  // version beta: for-loop like, 2014-11-01
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
//      str = ci__replace_allfirstlast_fixed(str, pattern_cur, replacement_cur, 0);
//      UNPROTECT(1);
//      PROTECT(str);
//   }
//
//   UNPROTECT(5);
//   return str;
//}
// ci__replace_all_fixed_no_vectorize_all
// Version alpha: benchmarks: 32 ms vs 35 ms for the loop-version
// Not worth fighting for..... :/, 2014-11-01
//SEXP ci__replace_all_fixed_no_vectorize_all(SEXP str, SEXP pattern, SEXP replacement)
//{
//   str          = ci__prepare_arg_string(str, "str");
//   pattern      = ci__prepare_arg_string(pattern, "pattern");
//   replacement  = ci__prepare_arg_string(replacement, "replacement");
//
//   R_len_t str_n = LENGTH(str);
//   R_len_t pattern_n = LENGTH(pattern);
//   R_len_t replacement_n = LENGTH(replacement);
//   if (pattern_n < replacement_n || pattern_n <= 0 || replacement_n <= 0)
//      Rf_error(MSG__WARN_RECYCLING_RULE2);
//   if (pattern_n % replacement_n != 0)
//      Rf_warning(MSG__WARN_RECYCLING_RULE);
//
//   // if str_n is 0, then return an empty vector
//   if (str_n <= 0)
//      return ci__vector_empty_strings(0);
//
//   STRI__ERROR_HANDLER_BEGIN
//   io::Utf8Input str_cont(str, str_n);
//   io::Utf8Input replacement_cont(replacement, pattern_n);
//   fixed::PatternSet pattern_cont(pattern, pattern_n);
//
//   // if any of the patterns is missing, then return an NA vector
//   // if a pattern is empty, throw an error
//   for (R_len_t i=0; i<pattern_n; ++i) {
//      if (pattern_cont.isNA(i))
//         return ci__vector_NA_strings(str_n);
//      if (pattern_cont.get(i).length() <= 0)
//         throw StriException(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
//   }
//
//   vector< deque< StriInterval<R_len_t> > > queues(str_n); // matches
//
//   vector<bool> which_NA(str_n, false); // which str[i] will be NA
//   for (R_len_t j=0; j<str_n; ++j)
//      if (str_cont.isNA(j))
//         which_NA[j] = true;
//
//   // get indices where we have a pattern match
//   // for each pattern, for each search string
//
//   // THIS IS THE SLOWEST FOR LOOP IN THIS FUNCTION
//   for (R_len_t i = 0; i < pattern_n; ++i)
//   {
//      // current pattern is not NA and is not empty
//
//      for (R_len_t j=0; j<str_n; ++j) {
//         if (which_NA[j] || str_cont.get(j).length() <= 0)
//            continue; // there's nothing interesting to play with here
//
//         R_len_t match_idx;
//         pattern_cont.setupMatcherFwd(i, str_cont.get(j).data(), str_cont.get(j).length());
//         match_idx = pattern_cont.findFirst();
//         if (match_idx == shared::ByteSearchMatcher::not_found) continue; // no match at all
//
//         // otherwise, there is >= 1 match
//         if (replacement_cont.isNA(i)) {
//            which_NA[j] = true; // this string will be missing in result
//            // it may have overlapping patterns BTW, but we won't check for that
//            continue; // the same pattern, next string
//         }
//         do {
//            queues[j].push_back(StriInterval<R_len_t>(match_idx, match_idx+pattern_cont.getMatchedLength(), i));
//            match_idx = pattern_cont.findNext();
//         }
//         while (match_idx != shared::ByteSearchMatcher::not_found);
//      }
//   }
//
//   // check if there are overlapping patterns,
//   // determine max buf size
//   R_len_t bufsize = 0;
//   for (R_len_t i=0; i<str_n; ++i) {
//      if (which_NA[i] || str_cont.get(i).length() <= 0 || !queues[i].size())
//         continue; // nothing interesting
//
//      // sort the i-th queue w.r.t. lower interval bound:
//      sort(queues[i].begin(), queues[i].end());
//
//      R_len_t bufsize_cur = str_cont.get(i).length();
//      deque< StriInterval<R_len_t> >::iterator iter = queues[i].begin();
//
//      StriInterval<R_len_t> last_int = *(iter++);
//      bufsize_cur = bufsize_cur - pattern_cont.get(last_int.data).length()
//                                + replacement_cont.get(last_int.data).length();
//      for (; iter != queues[i].end(); ++iter) {
//         StriInterval<R_len_t> cur_int = *iter;
//         if (cur_int.a < last_int.b)
//            throw StriException(MSG__OVERLAPPING_PATTERN_UNSUPPORTED);
//         bufsize_cur = bufsize_cur - pattern_cont.get(cur_int.data).length()
//                                   + replacement_cont.get(cur_int.data).length();
//         last_int = cur_int;
//      }
//
//      if (bufsize < bufsize_cur) bufsize = bufsize_cur;
//   }
//
//   // construct the resulting vector
//   SEXP ret;
//   STRI__PROTECT(ret = Rf_allocVector(STRSXP, str_n));
//   String8buf buf(bufsize);
//   for (R_len_t i=0; i<str_n; ++i) {
//      if (which_NA[i]) {
//         SET_STRING_ELT(ret, i, NA_STRING);
//         continue;
//      }
//      else if (str_cont.get(i).length() <= 0 || !queues[i].size()) {
//         // copy as-is
//         SET_STRING_ELT(ret, i, str_cont.toR(i));
//         continue;
//      }
//
//      // all right, at least one match - replace, captain!
//      R_len_t bufused = 0;
//      char* curbuf = buf.data();
//      const char* str_cur_s = str_cont.get(i).data();
//      R_len_t str_cur_n = str_cont.get(i).length();
//
//      R_len_t last_b = 0;
//      for (deque< StriInterval<R_len_t> >::iterator iter = queues[i].begin();
//               iter != queues[i].end(); ++iter) {
//         StriInterval<R_len_t> cur_int = *iter;
//         memcpy(curbuf+bufused, str_cur_s+last_b, cur_int.a-last_b);
//         bufused += (cur_int.a-last_b);
//         memcpy(curbuf+bufused, replacement_cont.get(cur_int.data).data(),
//            replacement_cont.get(cur_int.data).length());
//         bufused += replacement_cont.get(cur_int.data).length();
//         last_b = cur_int.b;
//      }
//
//      // the remainder
//      memcpy(curbuf+bufused, str_cur_s+last_b, str_cur_n-last_b);
//      bufused += (str_cur_n-last_b);
//      SET_STRING_ELT(ret, i, Rf_mkCharLenCE(buf.data(), bufused, CE_UTF8));
//   }
//
//   STRI__UNPROTECT_ALL
//   return ret;
//   STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
//}


/**
 * Replace all occurrences of a fixed pattern
 *
 * @param str character vector
 * @param pattern character vector
 * @param replacement character vector
 * @param vectorize_all single logical value
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-26)
 *          use ci__replace_allfirstlast_fixed
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          ci_replace_fixed now uses byte search only
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-01)
 *          vectorize_all argument added
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-07)
 *    FR #110, #23: opts_fixed arg added
 */
SEXP ci_replace_all_fixed(SEXP str, SEXP pattern, SEXP replacement, SEXP vectorize_all, SEXP opts_fixed)
{
    if (ci__prepare_arg_logical_1_notNA(vectorize_all, "vectorize_all"))
        return ci__replace_allfirstlast_fixed(str, pattern, replacement, opts_fixed, 0);
    else
        return ci__replace_all_fixed_no_vectorize_all(str, pattern, replacement, opts_fixed);
}


/**
 * Replace first occurrence of a fixed pattern
 *
 * @param str character vector
 * @param pattern character vector
 * @param replacement character vector
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-26)
 *          use ci__replace_allfirstlast_fixed
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          ci_replace_fixed now uses byte search only
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-07)
 *    FR #110, #23: opts_fixed arg added
 */
SEXP ci_replace_first_fixed(SEXP str, SEXP pattern, SEXP replacement, SEXP opts_fixed)
{
    return ci__replace_allfirstlast_fixed(str, pattern, replacement, opts_fixed, 1);
}

} } // namespace charr::base_backend
