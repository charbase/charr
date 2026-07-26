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
#include "ci_container_utf16.h"
#include "ci_container_usearch.h"
#include "ci_utf16_cursor.h"
#include <vector>
using namespace std;


namespace {

typedef pair<R_len_t, R_len_t> CollOccurrence;


UnicodeString ci__coll_replace_splice(
    const UnicodeString& source,
    const UnicodeString& replacement,
    const vector<CollOccurrence>& occurrences,
    R_len_t removed
)
{
    const R_len_t replacement_n = replacement.length();
    const R_len_t occurrence_n = static_cast<R_len_t>(occurrences.size());
    UnicodeString answer(
        source.length()-removed+occurrence_n*replacement_n,
        static_cast<UChar>(0xfffd), 0
    );
    R_len_t source_last = 0;
    for (const CollOccurrence& match : occurrences) {
        answer.append(source, source_last, match.first-source_last);
        source_last = match.second;
        answer.append(replacement);
    }
    answer.append(source, source_last, source.length()-source_last);
    return answer;
}

} // namespace


/**
 * Replace all/first/last occurrences of a fixed pattern [with collation]
 *
 * @param str character vector
 * @param pattern character vector
 * @param replacement character vector
 * @param opts_collator list
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-26)
 *          StriException friendly & Use StriContainers
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-07-10)
 *          BUGFIX: wrong behavior on empty str
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          new fun: ci__replace_allfirstlast_coll (opts_collator == NA not allowed)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.0-2 (Marek Gagolewski, 2016-01-30)
 *    Issue #210: Allow NA replacement
 */
SEXP ci__replace_allfirstlast_coll(SEXP str, SEXP pattern, SEXP replacement, SEXP opts_collator, int type)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(replacement = ci__prepare_arg_string(replacement, "replacement"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));

    UCollator* collator = NULL;

    STRI__ERROR_HANDLER_BEGIN(3)
    // Deviation from stringi: catch R errors from collator option parsing so
    // queued warnings and any opened collator are released before R resumes.
    charport::unwind_protect([&]() -> SEXP {
        collator = ci__ucol_open(
            STRI__DEFERRED_WARNINGS, opts_collator
        );
        return R_NilValue;
    });
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
    // Deviation from stringi: queue recycling warnings while the collator is
    // live and emit them after the collator closes.
    charport::unwind_protect([&]() -> SEXP {
        vectorize_length = ci__recycling_rule(
            false, 3, str_n, pattern_n, replacement_n
        );
        if (vectorize_length > 0 &&
                (vectorize_length%str_n != 0 ||
                 vectorize_length%pattern_n != 0 ||
                 vectorize_length%replacement_n != 0))
            STRI__DEFERRED_WARNINGS.push(MSG__WARN_RECYCLING_RULE);
        return R_NilValue;
    });

    charport::charvec::Builder builder(vectorize_length);
    {
        ci::Utf16Cursor str_cont(context, str, vectorize_length);
        StriContainerUStringSearch pattern_cont(
            context, pattern, vectorize_length, collator
        );  // collator is not owned by pattern_cont
        StriContainerUTF16 replacement_cont(
            context, replacement, vectorize_length
        );
        vector<CollOccurrence> occurrences;
        vector<char> utf8_buffer;
        const auto set_unchanged = [&] (R_len_t i) {
            const charr::altrep::Utf8Record* value =
                str_cont.utf8_if_valid(i);
            if (value) {
                ci::builder_set(builder, i, *value);
            }
            else {
                ci::builder_set(builder, i, str_cont.get(i), utf8_buffer);
            }
        };

        for (R_len_t i = pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            STRI__CONTINUE_ON_EMPTY_OR_NA_STR_PATTERN(
                str_cont, pattern_cont,
                builder.set_na(i);,
                set_unchanged(i);
            )

            const UnicodeString& source = str_cont.get(i);
            UStringSearch *matcher = pattern_cont.getMatcher(i, source);
            usearch_reset(matcher);

            UErrorCode status = U_ZERO_ERROR;
            R_len_t remUChars = 0;
            occurrences.clear();

            if (type >= 0) { // first or all
                int start = (int)usearch_first(matcher, &status);
                STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

                if (start == USEARCH_DONE) {
                    set_unchanged(i);
                    continue;
                }

                if (replacement_cont.isNA(i)) {
                    builder.set_na(i);
                    continue;
                }

                while (start != USEARCH_DONE) {
                    R_len_t mlen = usearch_getMatchedLength(matcher);
                    remUChars += mlen;
                    occurrences.emplace_back(start, start+mlen);
                    if (type > 0) break; // break if first and not all
                    start = usearch_next(matcher, &status);
                    STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
                }
            }
            else { // if last
                int start = (int)usearch_last(matcher, &status);
                STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
                if (start == USEARCH_DONE) {
                    set_unchanged(i);
                    continue;
                }

                if (replacement_cont.isNA(i)) {
                    builder.set_na(i);
                    continue;
                }
                R_len_t mlen = usearch_getMatchedLength(matcher);
                remUChars += mlen;
                occurrences.emplace_back(start, start+mlen);
            }

            const UnicodeString answer = ci__coll_replace_splice(
                source, replacement_cont.get(i), occurrences, remUChars
            );
            ci::builder_set(builder, i, answer, utf8_buffer);
        }
    }

    SEXP ret;
    STRI__PROTECT(ret = builder.to_sexp());
    if (collator) {
        ucol_close(collator);
        collator=NULL;
    }
    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(
        if (collator) ucol_close(collator);
    )
    }


/**
 * Replace all occurrences of a coll pattern; vectorize_all=FALSE
 *
 * @param str character vector
 * @param pattern character vector
 * @param replacement character vector
 * @param opts_collator a named list
 * @return character vector
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-06)
 *    Added missing ucol_close
 *
 * @version 1.0-2 (Marek Gagolewski, 2016-01-30)
 *    Issue #210: Allow NA replacement
 */
SEXP ci__replace_all_coll_no_vectorize_all(SEXP str, SEXP pattern, SEXP replacement, SEXP opts_collator)
{   // version beta
    PROTECT(str          = ci__prepare_arg_string(str, "str"));

    UCollator* collator = NULL;

    STRI__ERROR_HANDLER_BEGIN(1)
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);

    // if str_n is 0, then return an empty vector
    R_len_t str_n = ci::checked_r_len(
        context.size(str), "character vectors"
    );
    if (str_n <= 0) {
        charport::charvec::Builder builder(0);
        SEXP ret;
        STRI__PROTECT(ret = builder.to_sexp());
        context.emitWarnings();
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
    charport::unwind_protect([&]() -> SEXP {
        // Deviation from stringi: keep controlled recycling conditions on the
        // C++ path so R signalling happens after operation cleanup.
        if (pattern_n < replacement_n || pattern_n <= 0 || replacement_n <= 0)
            throw StriException(MSG__WARN_RECYCLING_RULE2);
        if (pattern_n % replacement_n != 0)
            context.warn(MSG__WARN_RECYCLING_RULE);
        return R_NilValue;
    });

    if (pattern_n == 1) {// this will be much faster:
        SEXP ret;
        // Deviation from stringi: replay outer preparation diagnostics before
        // delegation, while no Reader, collator, or output owner is active.
        context.emitWarnings();
        STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
            return ci__replace_allfirstlast_coll(
                str, pattern, replacement, opts_collator, 0
            );
        }));
        STRI__UNPROTECT_ALL
        return ret;
    }

    // Deviation from stringi: catch R errors from collator option parsing so
    // queued warnings and any opened collator are released before R resumes.
    charport::unwind_protect([&]() -> SEXP {
        collator = ci__ucol_open(
            STRI__DEFERRED_WARNINGS, opts_collator
        );
        return R_NilValue;
    });

    charport::charvec::Builder builder(str_n);
    {
        StriContainerUTF16 str_cont(
            context, str, str_n, false
        ); // writable
        StriContainerUStringSearch pattern_cont(
            context, pattern, pattern_n, collator
        );  // collator is not owned by pattern_cont
        StriContainerUTF16 replacement_cont(
            context, replacement, pattern_n
        );
        bool return_all_na = false;
        vector<CollOccurrence> occurrences;

        for (R_len_t i = 0; i<pattern_n; ++i)
        {
            if (pattern_cont.isNA(i)) {
                // Deviation from stringi: stage the all-NA return until the
                // pattern container destroys its matcher before the borrowed
                // collator closes and before Builder output is wrapped.
                return_all_na = true;
                break;
            }
            else if (pattern_cont.get(i).length() <= 0) {
                // StriContainerUStringSearch already queued stringi's first
                // warning; preserve this sequential path's second warning.
                context.warn(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
                return_all_na = true;
                break;
            }

            for (R_len_t j = 0; j<str_n; ++j) {
                if (str_cont.isNA(j) || str_cont.get(j).length() <= 0) continue;

                UStringSearch *matcher = pattern_cont.getMatcher(i, str_cont.get(j));
                usearch_reset(matcher);
                UErrorCode status = U_ZERO_ERROR;
                R_len_t remUChars = 0;
                occurrences.clear();

                int start = (int)usearch_first(matcher, &status);
                STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

                if (start == USEARCH_DONE) // no match
                    continue; // no change in str_cont[j] at all

                if (replacement_cont.isNA(i)) {
                    str_cont.setNA(j);
                    continue;
                }

                while (start != USEARCH_DONE) {
                    R_len_t mlen = usearch_getMatchedLength(matcher);
                    remUChars += mlen;
                    occurrences.emplace_back(start, start+mlen);
                    start = usearch_next(matcher, &status);
                    STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
                }

                str_cont.getWritable(j) = ci__coll_replace_splice(
                    str_cont.get(j), replacement_cont.get(i),
                    occurrences, remUChars
                );
            }
        }

        vector<char> utf8_buffer;
        for (R_len_t j=0; j<str_n; ++j) {
            if (return_all_na || str_cont.isNA(j))
                builder.set_na(j);
            else
                ci::builder_set(builder, j, str_cont.get(j), utf8_buffer);
        }
    }

    SEXP ret;
    STRI__PROTECT(ret = builder.to_sexp());
    if (collator) {
        ucol_close(collator);
        collator=NULL;
    }
    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(
        if (collator) ucol_close(collator);
    )
    }


/**
 * Replace all occurrences of a fixed pattern [with collation]
 *
 * @param str character vector
 * @param pattern character vector
 * @param replacement character vector
 * @param opts_collator list
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-26)
 *          use ci__replace_allfirstlast_fixed
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          new fun: ci_replace_all_coll (opts_collator == NA not allowed)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *          vectorize_all arg added
 */
SEXP ci_replace_all_coll(SEXP str, SEXP pattern, SEXP replacement, SEXP vectorize_all, SEXP opts_collator)
{
    if (ci__prepare_arg_logical_1_notNA(vectorize_all, "vectorize_all"))
        return ci__replace_allfirstlast_coll(str, pattern, replacement, opts_collator, 0);
    else
        return ci__replace_all_coll_no_vectorize_all(str, pattern, replacement, opts_collator);
}


/**
 * Replace last occurrence of a fixed pattern [with collation]
 *
 * @param str character vector
 * @param pattern character vector
 * @param replacement character vector
 * @param opts_collator list
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-26)
 *          use ci__replace_allfirstlast_fixed
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          new fun: ci_replace_last_coll (opts_collator == NA not allowed)
 */
SEXP ci_replace_last_coll(SEXP str, SEXP pattern, SEXP replacement, SEXP opts_collator)
{
    return ci__replace_allfirstlast_coll(str, pattern, replacement, opts_collator, -1);
}


/**
 * Replace first occurrence of a fixed pattern [with collation]
 *
 * @param str character vector
 * @param pattern character vector
 * @param replacement character vector
 * @param opts_collator list
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-26)
 *          use ci__replace_allfirstlast_fixed
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          new fun: ci_replace_first_coll (opts_collator == NA not allowed)
 */
SEXP ci_replace_first_coll(SEXP str, SEXP pattern, SEXP replacement, SEXP opts_collator)
{
    return ci__replace_allfirstlast_coll(str, pattern, replacement, opts_collator, 1);
}
