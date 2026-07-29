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
#include "fixed/pattern_set.h"
#include "ci_string8buf.h"
#include "altrep_backend/io/utf8_output.h"
//#include "ci_interval.h"
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>

namespace charr { namespace altrep_backend {
//#include <queue>
//#include <algorithm>
using namespace std;


namespace search_fixed_replace {

struct CiDirectView {
    const char* data;
    R_len_t length;
    bool is_na;
    cetype_ext_t encoding;
};


bool ci__direct_view(
    const charport::StrView& value, CiDirectView& output
)
{
    if (value.is_na()) {
        output = {NULL, NA_INTEGER, true, cetype_ext_t::CE_NA};
        return true;
    }
    if (value.enc != cetype_ext_t::CE_ASCII &&
            value.enc != cetype_ext_t::CE_UTF8 &&
            value.enc != cetype_ext_t::CE_ASCII_OR_UTF8) {
        return false;
    }

    output.data = value.ptr;
    output.length = value.len;
    output.is_na = false;
    output.encoding = value.enc;
    const bool has_bom = value.enc != cetype_ext_t::CE_ASCII &&
        STRI__ENC_HAS_BOM_UTF8(output.data, output.length);
    if (has_bom) {
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


size_t ci__replacement_size(
    R_len_t source_length, R_len_t replacement_length,
    size_t count, size_t matched_bytes
)
{
    const size_t source_size = static_cast<size_t>(source_length);
    if (matched_bytes > source_size)
        throw std::length_error(MSG__CHARSXP_2147483647);
    const size_t unmatched = source_size-matched_bytes;
    const size_t maximum = static_cast<size_t>(R_LEN_T_MAX);
    if (replacement_length > 0 &&
            count > (maximum-unmatched) /
                static_cast<size_t>(replacement_length)) {
        throw std::length_error(MSG__CHARSXP_2147483647);
    }
    return unmatched+count*static_cast<size_t>(replacement_length);
}


struct CiAllByteScan {
    size_t count;
    bool output_is_ascii;
};


CiAllByteScan ci__scan_all_byte_replacements(
    const CiDirectView& source, unsigned char pattern,
    bool replacement_is_ascii
)
{
    CiAllByteScan result{0, replacement_is_ascii};
    for (R_len_t i = 0; i < source.length; ++i) {
        const unsigned char value = static_cast<unsigned char>(
            source.data[i]
        );
        if (value == pattern)
            ++result.count;
        else if (value > 0x7fU)
            result.output_is_ascii = false;
    }
    return result;
}


bool ci__one_byte_output_is_ascii(
    const CiDirectView& source, R_len_t match,
    bool replacement_is_ascii
)
{
    if (!replacement_is_ascii)
        return false;
    for (R_len_t i = 0; i < source.length; ++i) {
        if (i != match &&
                static_cast<unsigned char>(source.data[i]) > 0x7fU) {
            return false;
        }
    }
    return true;
}


void ci__write_one_byte_replacement(
    char* output, const CiDirectView& source, R_len_t match,
    const CiDirectView& replacement
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
    char* output, const CiDirectView& source, unsigned char pattern,
    const CiDirectView& replacement
)
{
    if (replacement.length == 1) {
        if (source.length > 0) {
            std::memcpy(
                output, source.data, static_cast<size_t>(source.length)
            );
        }
        for (R_len_t i = 0; i < source.length; ++i) {
            if (static_cast<unsigned char>(output[i]) == pattern)
                output[i] = replacement.data[0];
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


struct CiReplacementLayout {
    size_t size;
    bool is_ascii;
};


CiReplacementLayout ci__replacement_layout(
    const char* source, R_len_t source_length,
    const io::Utf8Record& replacement,
    const deque<pair<R_len_t, R_len_t> >& occurrences,
    size_t matched_bytes
)
{
    CiReplacementLayout layout{
        ci__replacement_size(
            source_length, replacement.length(),
            occurrences.size(), matched_bytes
        ),
        replacement.isASCII()
    };
    if (!layout.is_ascii)
        return layout;

    R_len_t previous = 0;
    for (const pair<R_len_t, R_len_t>& occurrence : occurrences) {
        if (!ci::is_ascii(
                source+previous,
                static_cast<size_t>(occurrence.first-previous))) {
            layout.is_ascii = false;
            return layout;
        }
        previous = occurrence.second;
    }
    layout.is_ascii = ci::is_ascii(
        source+previous,
        static_cast<size_t>(source_length-previous)
    );
    return layout;
}


void ci__write_replacements(
    char* output, const char* source, R_len_t source_length,
    const io::Utf8Record& replacement,
    const deque<pair<R_len_t, R_len_t> >& occurrences
)
{
    size_t used = 0;
    R_len_t previous = 0;
    for (const pair<R_len_t, R_len_t>& occurrence : occurrences) {
        const size_t prefix = static_cast<size_t>(
            occurrence.first-previous
        );
        if (prefix > 0) {
            std::memcpy(output+used, source+previous, prefix);
            used += prefix;
        }
        if (replacement.length() > 0) {
            std::memcpy(
                output+used, replacement.data(),
                static_cast<size_t>(replacement.length())
            );
            used += static_cast<size_t>(replacement.length());
        }
        previous = occurrence.second;
    }
    const size_t suffix = static_cast<size_t>(source_length-previous);
    if (suffix > 0)
        std::memcpy(output+used, source+previous, suffix);
}


bool ci__replace_scalar_byte_direct(
    const charport::StrViews& strings,
    const charport::StrView& pattern,
    const charport::StrView& replacement,
    R_len_t vectorize_length, uint32_t pattern_flags, int type,
    charr::altrep_backend::io::OutputBuilder& builder,
    R_len_t& general_start
)
{
    if (vectorize_length == 0)
        return true;
    if (pattern_flags != 0 || strings.size() != vectorize_length)
        return false;

    CiDirectView pattern_value;
    CiDirectView replacement_value;
    if (!ci__direct_view(pattern, pattern_value) ||
            pattern_value.is_na || pattern_value.length != 1 ||
            !ci__direct_view(replacement, replacement_value)) {
        return false;
    }

    const unsigned char pattern_byte = static_cast<unsigned char>(
        pattern_value.data[0]
    );
    const bool replacement_is_ascii = !replacement_value.is_na &&
        ci::is_ascii(
            replacement_value.data,
            static_cast<size_t>(replacement_value.length)
        );

    for (R_len_t i = 0; i < vectorize_length; ++i) {
        CiDirectView source;
        if (!ci__direct_view(strings[i], source)) {
            general_start = i;
            return false;
        }
        if (source.is_na) {
            builder.set_na(i);
            continue;
        }

        R_len_t match = shared::ByteSearchMatcher::not_found;
        size_t count = 0;
        bool output_is_ascii = false;
        if (type == 0) {
            const CiAllByteScan scan = ci__scan_all_byte_replacements(
                source, pattern_byte, replacement_is_ascii
            );
            count = scan.count;
            output_is_ascii = scan.output_is_ascii;
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
            builder.set(
                i, source.data, static_cast<size_t>(source.length),
                source.encoding
            );
            continue;
        }
        if (replacement_value.is_na) {
            builder.set_na(i);
            continue;
        }

        if (type != 0) {
            output_is_ascii = ci__one_byte_output_is_ascii(
                source, match, replacement_is_ascii
            );
        }
        const size_t output_length = ci__replacement_size(
            source.length, replacement_value.length,
            type == 0 ? count : 1,
            type == 0 ? count : 1
        );
        char* output = builder.reserve(
            i, output_length,
            output_is_ascii
                ? cetype_ext_t::CE_ASCII
                : cetype_ext_t::CE_UTF8
        );
        if (type == 0) {
            ci__write_all_byte_replacements(
                output, source, pattern_byte, replacement_value
            );
        }
        else {
            ci__write_one_byte_replacement(
                output, source, match, replacement_value
            );
        }
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

    STRI__ERROR_HANDLER_BEGIN(3)
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
    ci::unwind_protect([&]() -> SEXP {
        vectorize_length = ci__recycling_rule(
            STRI__DEFERRED_WARNINGS, 3,
            str_n, pattern_n, replacement_n
        );
        return R_NilValue;
    });

    charr::altrep_backend::io::OutputBuilder builder(vectorize_length);
    bool direct = vectorize_length == 0;
    R_len_t general_start = 0;
    std::shared_ptr<ci::ReaderBorrow> str_borrow;
    std::shared_ptr<ci::ReaderBorrow> pattern_borrow;
    std::shared_ptr<ci::ReaderBorrow> replacement_borrow;
    if (!direct && pattern_flags == 0 && pattern_n == 1 &&
            replacement_n == 1) {
        str_borrow = context.acquire(str);
        pattern_borrow = context.acquire(pattern);
        replacement_borrow = context.acquire(replacement);
        direct = ci__replace_scalar_byte_direct(
            str_borrow->views(), pattern_borrow->views()[0],
            replacement_borrow->views()[0], vectorize_length,
            pattern_flags, type, builder, general_start
        );
    }

    if (!direct) {
        {
        io::Utf8Input str_cont(context, str, vectorize_length);
        io::Utf8Input replacement_cont(
            context, replacement, vectorize_length
        );
        fixed::PatternSet pattern_cont(
            context, pattern, vectorize_length, pattern_flags
        );

        for (R_len_t i = general_start > 0
                    ? general_start
                    : pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            STRI__CONTINUE_ON_EMPTY_OR_NA_STR_PATTERN(str_cont, pattern_cont,
                    builder.set_na(i);,
                    builder.set(
                        i, "", 0, cetype_ext_t::CE_ASCII
                    );)

            shared::ByteSearchMatcher* matcher = pattern_cont.getMatcher(i);
            matcher->reset(str_cont.get(i).data(), str_cont.get(i).length());
            R_len_t start;
            if (type >= 0) { // first or all
                start = matcher->find_first();
            } else {
                start = matcher->find_last();
            }

            if (start == shared::ByteSearchMatcher::not_found) {
                const io::Utf8Record& source = str_cont.get(i);
                builder.set(
                    i, source.data(), source.length(),
                    source.isASCII()
                        ? cetype_ext_t::CE_ASCII
                        : cetype_ext_t::CE_UTF8
                );
                continue;
            }

            if (replacement_cont.isNA(i)) {
                builder.set_na(i);
                continue;
            }

            R_len_t len = matcher->matched_length();
            size_t matched_bytes = static_cast<size_t>(len);
            deque< pair<R_len_t, R_len_t> > occurrences;
            occurrences.push_back(pair<R_len_t, R_len_t>(start, start+len));

            if (type == 0) {
                while (shared::ByteSearchMatcher::not_found != matcher->find_next()) { // all
                    start = matcher->matched_start();
                    len = matcher->matched_length();
                    occurrences.push_back(pair<R_len_t, R_len_t>(start, start+len));
                    matched_bytes += static_cast<size_t>(len);
                }
            }

            const io::Utf8Record& source = str_cont.get(i);
            const io::Utf8Record& replacement_current = replacement_cont.get(i);
            const CiReplacementLayout layout = ci__replacement_layout(
                source.data(), source.length(), replacement_current,
                occurrences, matched_bytes
            );
            char* output = builder.reserve(
                i, layout.size,
                layout.is_ascii
                    ? cetype_ext_t::CE_ASCII
                    : cetype_ext_t::CE_UTF8
            );
            ci__write_replacements(
                output, source.data(), source.length(),
                replacement_current, occurrences
            );
        }
        }
    }

    replacement_borrow.reset();
    pattern_borrow.reset();
    str_borrow.reset();
    STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
        return builder.to_sexp();
    }));
    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


// Version 2, 2014-11-02, using io::Utf8Record::replaceAllAtPos, slower
//SEXP ci__replace_allfirstlast_fixed(SEXP str, SEXP pattern, SEXP replacement, int type)
//{
//   str          = ci__prepare_arg_string(str, "str");
//   pattern      = ci__prepare_arg_string(pattern, "pattern");
//   replacement  = ci__prepare_arg_string(replacement, "replacement");
//   R_len_t vectorize_length = ci__recycling_rule(true, 3, LENGTH(str), LENGTH(pattern), LENGTH(replacement));
//
//   STRI__ERROR_HANDLER_BEGIN
//   io::Utf8Input str_cont(str, vectorize_length, false); // writable);
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
//         pattern_cont.setupMatcherFwd(i, str_cont.get(i).c_str(), str_cont.get(i).length());
//         start = pattern_cont.findFirst();
//      } else {
//         pattern_cont.setupMatcherBack(i, str_cont.get(i).c_str(), str_cont.get(i).length());
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
//         replacement_cont.get(i).c_str(), replacement_cur_n,
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

    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP ret;
    {
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    R_len_t str_n = ci::checked_r_len(
        context.size(str), "character vectors"
    );

    if (str_n <= 0) {
        charr::altrep_backend::io::OutputBuilder builder(0);
        STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
            return builder.to_sexp();
        }));
        STRI__UNPROTECT_ALL
        return ret;
    }

    // Deviation from stringi: lazy preparation now runs inside the C++
    // boundary, so queue its controlled warnings with the operation.
    STRI__PROTECT(pattern = ci::unwind_protect([&]() -> SEXP {
        return ci__prepare_arg_string(
            pattern, "pattern", true, &STRI__DEFERRED_WARNINGS
        );
    }));
    STRI__PROTECT(replacement = ci::unwind_protect([&]() -> SEXP {
        return ci__prepare_arg_string(
            replacement, "replacement", true,
            &STRI__DEFERRED_WARNINGS
        );
    }));

    R_len_t pattern_n = ci::checked_r_len(
        context.size(pattern), "character vectors"
    );
    R_len_t replacement_n = ci::checked_r_len(
        context.size(replacement), "character vectors"
    );
    // Deviation from stringi: signal this controlled validation failure only
    // after the outer C++ boundary has released operation state.
    ci::unwind_protect([&]() -> SEXP {
        if (pattern_n < replacement_n || pattern_n <= 0 || replacement_n <= 0)
            throw StriException(MSG__WARN_RECYCLING_RULE2);
        return R_NilValue;
    });
    if (pattern_n % replacement_n != 0)
        context.warn(MSG__WARN_RECYCLING_RULE);

    if (pattern_n == 1) { // this will be much faster:
        // Deviation from stringi: replay outer preparation diagnostics before
        // delegation, while no Reader or output owner is active.
        context.emitWarnings();
        STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
            return ci__replace_allfirstlast_fixed(
                str, pattern, replacement, opts_fixed, 0
            );
        }));
        STRI__UNPROTECT_ALL
        return ret;
    }

    uint32_t pattern_flags = 0;
    ci::unwind_protect([&]() -> SEXP {
        pattern_flags = fixed::PatternSet::getByteSearchFlags(
            opts_fixed, false, &STRI__DEFERRED_WARNINGS
        );
        return R_NilValue;
    });
    charr::altrep_backend::io::OutputBuilder builder(str_n);

    {
        io::Utf8Workspace str_cont(context, str, str_n);
        io::Utf8Input replacement_cont(
            context, replacement, pattern_n
        );
        fixed::PatternSet pattern_cont(
            context, pattern, pattern_n, pattern_flags
        );
        bool return_all_na = false;

        for (R_len_t i = 0; i<pattern_n; ++i)
        {
            if (pattern_cont.isNA(i)) {
                return_all_na = true;
                break;
            }
            else if (pattern_cont.get(i).length() <= 0) {
                context.warn(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
                return_all_na = true;
                break;
            }

            shared::ByteSearchMatcher* matcher = pattern_cont.getMatcher(i);
            for (R_len_t j = 0; j<str_n; ++j) {
                if (str_cont.isNA(j)) continue;
                matcher->reset(str_cont.get(j).data(), str_cont.get(j).length());
                R_len_t start = matcher->find_first();
                if (start == shared::ByteSearchMatcher::not_found)  continue;  // nothing to do now

                if (replacement_cont.isNA(i)) {
                    str_cont.setNA(j);
                    continue;
                }

                R_len_t len = matcher->matched_length();
                size_t matched_bytes = static_cast<size_t>(len);
                deque< pair<R_len_t, R_len_t> > occurrences;
                occurrences.push_back(pair<R_len_t, R_len_t>(start, start+len));

                while (shared::ByteSearchMatcher::not_found != matcher->find_next()) { // all
                    start = matcher->matched_start();
                    len = matcher->matched_length();
                    occurrences.push_back(pair<R_len_t, R_len_t>(start, start+len));
                    matched_bytes += static_cast<size_t>(len);
                }

                R_len_t str_cur_n         = str_cont.get(j).length();
                R_len_t replacement_cur_n = replacement_cont.get(i).length();
                const size_t output_size = ci__replacement_size(
                    str_cur_n, replacement_cur_n,
                    occurrences.size(), matched_bytes
                );

                str_cont.replaceAllAtPos(
                    j, static_cast<R_len_t>(output_size),
                    replacement_cont.get(i).data(),
                    replacement_cur_n, occurrences
                );
            }
        }

        for (R_len_t j=0; j<str_n; ++j) {
            if (return_all_na)
                builder.set_na(j);
            else {
                const io::Utf8Record& value = str_cont.getNAble(j);
                if (value.isNA()) {
                    builder.set_na(j);
                }
                else {
                    builder.set(
                        j, value.data(), value.length(),
                        value.isASCII()
                            ? cetype_ext_t::CE_ASCII
                            : cetype_ext_t::CE_UTF8
                    );
                }
            }
        }
    }

    STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
        return builder.to_sexp();
    }));
    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
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
//         pattern_cont.setupMatcherFwd(i, str_cont.get(j).c_str(), str_cont.get(j).length());
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
//      const char* str_cur_s = str_cont.get(i).c_str();
//      R_len_t str_cur_n = str_cont.get(i).length();
//
//      R_len_t last_b = 0;
//      for (deque< StriInterval<R_len_t> >::iterator iter = queues[i].begin();
//               iter != queues[i].end(); ++iter) {
//         StriInterval<R_len_t> cur_int = *iter;
//         memcpy(curbuf+bufused, str_cur_s+last_b, cur_int.a-last_b);
//         bufused += (cur_int.a-last_b);
//         memcpy(curbuf+bufused, replacement_cont.get(cur_int.data).c_str(),
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

} } // namespace charr::altrep_backend
