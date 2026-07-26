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
#include "ci_container_charclass.h"
#include "ci_container_logical.h"
#include "ci_string8buf.h"
#include <deque>
#include <utility>
using namespace std;


/**
 * Replace all occurrences of a character class
 *
 * @param str character vector; strings to search in
 * @param pattern character vector; charclasses to search for
 * @param replacement character vector; strings to replace with
 * @param merge merge consecutive matches into a single one?
 *
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-07)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-15)
 *          Use StrContainerCharClass
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException-friendly
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-03)
 *          detects invalid UTF-8 byte stream;
 *          merge arg added (replacement of old ci_trim_both/double by BT)
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-05)
 *          StriContainerCharClass now relies on UnicodeSet
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-02)
 *          using String8buf::replaceAllAtPos and StriContainerCharClass::locateAll;
 *          no longer vectorized over merge
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.0-2 (Marek Gagolewski, 2016-01-30)
 *    Issue #210: Allow NA replacement
 */
SEXP ci__replace_all_charclass_yes_vectorize_all(SEXP str, SEXP pattern, SEXP replacement, SEXP merge)
{
    PROTECT(str            = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern        = ci__prepare_arg_string(pattern, "pattern"));
    PROTECT(replacement    = ci__prepare_arg_string(replacement, "replacement"));
    bool merge_cur = ci__prepare_arg_logical_1_notNA(merge, "merge");

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
    charport::unwind_protect([&]() -> SEXP {
        vectorize_length = ci__recycling_rule(
            STRI__DEFERRED_WARNINGS, 3,
            str_n, pattern_n, replacement_n
        );
        return R_NilValue;
    });

    charport::charvec::Builder builder(vectorize_length);
    {
        Utf8Input str_cont(context, str, vectorize_length);
        Utf8Input replacement_cont(
            context, replacement, vectorize_length
        );
        StriContainerCharClass pattern_cont(
            context, pattern, vectorize_length
        );

        String8buf buf(0); // @TODO: calculate buf len a priori?

        for (R_len_t i = pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            if (str_cont.isNA(i) || pattern_cont.isNA(i)) {
                builder.set_na(i);
                continue;
            }

            R_len_t str_cur_n     = str_cont.get(i).length();
            const char* str_cur_s = str_cont.get(i).data();
            deque< pair<R_len_t, R_len_t> > occurrences;
            R_len_t sumbytes = StriContainerCharClass::locateAll(
                                   occurrences, &pattern_cont.get(i),
                                   str_cur_s, str_cur_n, merge_cur,
                                   false /* byte-based indices */
                               );

            if (occurrences.size() == 0) {
                ci::builder_set(builder, i, str_cont.get(i)); // no change
                continue;
            }

            if (replacement_cont.isNA(i)) {
                builder.set_na(i);
                continue;
            }

            R_len_t replacement_cur_n = replacement_cont.get(i).length();
            R_len_t buf_need = str_cur_n+
                (R_len_t)occurrences.size()*replacement_cur_n-sumbytes;
            buf.resize(buf_need, false/*destroy contents*/);

            R_len_t buf_used = buf.replaceAllAtPos(
                str_cur_s, str_cur_n,
                replacement_cont.get(i).data(), replacement_cur_n,
                occurrences
            );

#ifndef NDEBUG
            if (buf_need != buf_used)
                throw StriException("!NDEBUG: ci__replace_allfirstlast_fixed: (buf_need != buf_used)");
#endif

            ci::builder_set(
                builder, i, buf.data(), buf_used,
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


/**
 * Replace all occurrences of a character class
 *
 * @param str character vector; strings to search in
 * @param pattern character vector; charclasses to search for
 * @param replacement character vector; strings to replace with
 * @param merge merge consecutive matches into a single one?
 *
 * @return character vector
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-02)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.0-2 (Marek Gagolewski, 2016-01-30)
 *    Issue #210: Allow NA replacement
 */
SEXP ci__replace_all_charclass_no_vectorize_all(SEXP str, SEXP pattern, SEXP replacement, SEXP merge)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));

    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP ret;
    {
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
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
    R_len_t pattern_n = ci::checked_r_len(
        context.size(pattern), "character vectors"
    );
    R_len_t replacement_n = ci::checked_r_len(
        context.size(replacement), "character vectors"
    );
    // Deviation from stringi: signal this controlled validation failure only
    // after the outer C++ boundary has released operation state.
    charport::unwind_protect([&]() -> SEXP {
        if (pattern_n < replacement_n || pattern_n <= 0 || replacement_n <= 0)
            throw StriException(MSG__WARN_RECYCLING_RULE2);
        return R_NilValue;
    });
    if (pattern_n % replacement_n != 0)
        context.warn(MSG__WARN_RECYCLING_RULE);

    if (pattern_n == 1) {// this will be much faster:
        // Deviation from stringi: replay outer preparation diagnostics before
        // delegation, while no Reader or output owner is active.
        context.emitWarnings();
        STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
            return ci__replace_all_charclass_yes_vectorize_all(
                str, pattern, replacement, merge
            );
        }));
        STRI__UNPROTECT_ALL
        return ret;
    }

    bool merge_cur = false;
    charport::unwind_protect([&]() -> SEXP {
        merge_cur = ci__prepare_arg_logical_1_notNA(
            merge, "merge", &STRI__DEFERRED_WARNINGS
        );
        return R_NilValue;
    });
    charport::charvec::Builder builder(str_n);

    {
        Utf8Workspace str_cont(context, str, str_n);
        Utf8Input replacement_cont(
            context, replacement, pattern_n
        );
        StriContainerCharClass pattern_cont(
            context, pattern, pattern_n
        );
        bool return_all_na = false;

        String8buf buf(0); // @TODO: calculate buf len a priori?

        for (R_len_t i = 0; i<pattern_n; ++i)
        {
            if (pattern_cont.isNA(i)) {
                return_all_na = true;
                break;
            }

            for (R_len_t j = 0; j<str_n; ++j) {
                if (str_cont.isNA(j)) continue;

                R_len_t str_cur_n     = str_cont.get(j).length();
                const char* str_cur_s = str_cont.get(j).data();
                deque< pair<R_len_t, R_len_t> > occurrences;
                R_len_t sumbytes = StriContainerCharClass::locateAll(
                                       occurrences, &pattern_cont.get(i),
                                       str_cur_s, str_cur_n, merge_cur,
                                       false /* byte-based indices */
                                   );

                if (occurrences.size() == 0)
                    continue;

                if (replacement_cont.isNA(i)) {
                    str_cont.setNA(j);
                    continue;
                }

                R_len_t replacement_cur_n = replacement_cont.get(i).length();
                R_len_t buf_need = str_cur_n+
                    (R_len_t)occurrences.size()*replacement_cur_n-sumbytes;
                buf.resize(buf_need, false/*destroy contents*/);

                str_cont.replaceAllAtPos(
                    j, buf_need, replacement_cont.get(i).data(),
                    replacement_cur_n, occurrences
                );
            }
        }

        for (R_len_t j=0; j<str_n; ++j) {
            if (return_all_na)
                builder.set_na(j);
            else
                ci::builder_set(builder, j, str_cont.getNAble(j));
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
 * Replace all occurrences of a character class
 *
 * @param str character vector; strings to search in
 * @param pattern character vector; charclasses to search for
 * @param replacement character vector; strings to replace with
 * @param merge merge consecutive matches into a single one?
 * @param vectorize_all single logical value
 *
 * @return character vector
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-02)
 *          added `vectorize_all` arg
 */
SEXP ci_replace_all_charclass(SEXP str, SEXP pattern, SEXP replacement, SEXP merge, SEXP vectorize_all)
{
    if (ci__prepare_arg_logical_1_notNA(vectorize_all, "vectorize_all"))
        return ci__replace_all_charclass_yes_vectorize_all(str, pattern, replacement, merge);
    else
        return ci__replace_all_charclass_no_vectorize_all(str, pattern, replacement, merge);
}


/**
 * Replace first or last occurrence of a character class [internal]
 *
 * @param str character vector; strings to search in
 * @param pattern character vector; charclasses to search for
 * @param replacement character vector; strings to replace with
 * @param first replace first (TRUE) or last (FALSE)?
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-06)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-15)
 *                Use StrContainerCharClass
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *                make StriException-friendly
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-03)
 *          detects invalid UTF-8 byte stream
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-05)
 *          StriContainerCharClass now relies on UnicodeSet
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci__replace_firstlast_charclass(SEXP str, SEXP pattern, SEXP replacement, bool first)
{
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
    charport::unwind_protect([&]() -> SEXP {
        vectorize_length = ci__recycling_rule(
            STRI__DEFERRED_WARNINGS, 3,
            str_n, pattern_n, replacement_n
        );
        return R_NilValue;
    });

    charport::charvec::Builder builder(vectorize_length);
    {
        Utf8Input str_cont(context, str, vectorize_length);
        Utf8Input replacement_cont(
            context, replacement, vectorize_length
        );
        StriContainerCharClass pattern_cont(
            context, pattern, vectorize_length
        );

        String8buf buf(0); // @TODO: consider calculating buflen a priori

        for (R_len_t i = pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            if (str_cont.isNA(i) || pattern_cont.isNA(i)) {
                builder.set_na(i);
                continue;
            }

            const UnicodeSet* pattern_cur = &pattern_cont.get(i);
            R_len_t str_cur_n     = str_cont.get(i).length();
            const char* str_cur_s = str_cont.get(i).data();
            R_len_t j, jlast;
            UChar32 chr;

            if (first) { // search for first
                for (jlast=j=0; j<str_cur_n; ) {
                    U8_NEXT(str_cur_s, j, str_cur_n, chr); // "look ahead"
                    if (chr < 0) // invalid utf-8 sequence
                        throw StriException(MSG__INVALID_UTF8);
                    if (pattern_cur->contains(chr)) {
                        break; // break at first occurrence
                    }
                    jlast = j;
                }
            }
            else { // search for last
                for (jlast=j=str_cur_n; jlast>0; ) {
                    U8_PREV(str_cur_s, 0, jlast, chr); // "look behind"
                    if (chr < 0) // invalid utf-8 sequence
                        throw StriException(MSG__INVALID_UTF8);
                    if (pattern_cur->contains(chr)) {
                        break; // break at first occurrence
                    }
                    j = jlast;
                }
            }

            // match is at jlast, and ends right before j

            if (j == jlast) { // iff not found
                ci::builder_set(builder, i, str_cont.get(i)); // no change
                continue;
            }

            if (replacement_cont.isNA(i)) {
                builder.set_na(i);
                continue;
            }

            R_len_t replacement_cur_n = replacement_cont.get(i).length();
            const char* replacement_cur_s = replacement_cont.get(i).data();
            R_len_t buf_need = str_cur_n+replacement_cur_n-(j-jlast);
            buf.resize(buf_need, false/*destroy contents*/);
            memcpy(buf.data(), str_cur_s, (size_t)jlast);
            memcpy(buf.data()+jlast, replacement_cur_s, (size_t)replacement_cur_n);
            memcpy(buf.data()+jlast+replacement_cur_n, str_cur_s+j, (size_t)str_cur_n-j);
            ci::builder_set(
                builder, i, buf.data(), buf_need,
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


/**
 * Replace first occurrence of a character class
 *
 * @param str character vector; strings to search in
 * @param pattern character vector; charclasses to search for
 * @param replacement character vector; strings to replace with
 *
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-06)
 */
SEXP ci_replace_first_charclass(SEXP str, SEXP pattern, SEXP replacement)
{
    return ci__replace_firstlast_charclass(str, pattern, replacement, true);
}


/**
 * Replace last occurrence of a character class
 *
 * @param str character vector; strings to search in
 * @param pattern character vector; charclasses to search for
 * @param replacement character vector; strings to replace with
 *
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-06)
 */
SEXP ci_replace_last_charclass(SEXP str, SEXP pattern, SEXP replacement)
{
    return ci__replace_firstlast_charclass(str, pattern, replacement, false);
}
