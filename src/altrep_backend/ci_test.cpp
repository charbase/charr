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
#include "ci_container_bytesearch.h"
#include "ci_container_listutf8.h"
#include "ci_container_utf8.h"
#include "ci_container_utf16.h"

#include <R_ext/Altrep.h>

namespace {

// charr-owned: deterministic foreign-ALTREP error canary for the outer
// STRI__ERROR_HANDLER boundary. This class deliberately remains unregistered
// with charport so Reader construction takes the protected fallback path.
R_altrep_class_t ci_test_erroring_altrep_class;

const char* ci_test_erroring_altrep_message =
    "charr test ALTREP access failure";

R_xlen_t ci_test_erroring_altrep_length(SEXP x)
{
    return XLENGTH(R_altrep_data1(x));
}

SEXP ci_test_erroring_altrep_elt(SEXP, R_xlen_t)
{
    Rf_error("%s", ci_test_erroring_altrep_message);
    return NA_STRING;
}

void* ci_test_erroring_altrep_dataptr(SEXP, Rboolean)
{
    Rf_error("%s", ci_test_erroring_altrep_message);
    return NULL;
}

const void* ci_test_erroring_altrep_dataptr_or_null(SEXP)
{
    return NULL;
}

std::unique_ptr<StriByteSearchMatcher> make_test_byte_matcher(
    const String8& pattern, bool case_insensitive
)
{
    if (case_insensitive) {
        return std::unique_ptr<StriByteSearchMatcher>(
            new StriByteSearchMatcherKMPci(
                pattern.data(), pattern.length(), false
            )
        );
    }
    if (pattern.length() == 1) {
        return std::unique_ptr<StriByteSearchMatcher>(
            new StriByteSearchMatcher1(
                pattern.data(), pattern.length(), false
            )
        );
    }
    if (pattern.length() < 16) {
        return std::unique_ptr<StriByteSearchMatcher>(
            new StriByteSearchMatcherShort(
                pattern.data(), pattern.length(), false
            )
        );
    }
    return std::unique_ptr<StriByteSearchMatcher>(
        new StriByteSearchMatcherKMP(
            pattern.data(), pattern.length(), false
        )
    );
}

} // namespace


void ci_test_init_erroring_altrep(DllInfo* dll)
{
    ci_test_erroring_altrep_class =
        R_make_altstring_class("test_erroring_altrep", "charr", dll);
    R_set_altrep_Length_method(
        ci_test_erroring_altrep_class, ci_test_erroring_altrep_length
    );
    R_set_altvec_Dataptr_method(
        ci_test_erroring_altrep_class, ci_test_erroring_altrep_dataptr
    );
    R_set_altvec_Dataptr_or_null_method(
        ci_test_erroring_altrep_class,
        ci_test_erroring_altrep_dataptr_or_null
    );
    R_set_altstring_Elt_method(
        ci_test_erroring_altrep_class, ci_test_erroring_altrep_elt
    );
}


SEXP ci_test_erroring_altrep(SEXP n)
{
    const int length = Rf_asInteger(n);
    if (length == NA_INTEGER || length < 0)
        Rf_error("n must be a non-negative integer");

    SEXP data = PROTECT(Rf_allocVector(RAWSXP, length));
    SEXP out = R_new_altrep(
        ci_test_erroring_altrep_class, data, R_NilValue
    );
    UNPROTECT(1);
    return out;
}


/** dummy fun to measure the performance of .Call
 *
 * @version 0.1-?? (Marek Gagolewski)
 */
SEXP ci_test_returnasis(SEXP x)
{
    return x;
}


/** Check R encoding marking *for testing only*
 *  This function should not be exported
 *
 *  @param s character vector
 *
 *  Results are printed on STDERR
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci_test_Rmark(SEXP s)
{
#ifndef NDEBUG
    PROTECT(s = ci__prepare_arg_string(s, "str"));
    int ns = LENGTH(s);
    for (int i=0; i < ns; ++i) {
        fprintf(stdout, "!NDEBUG: Element #%d:\n", i);
        SEXP curs = STRING_ELT(s, i);
        if (curs == NA_STRING) {
            fprintf(stdout, "!NDEBUG: \tNA\n");
            continue;
        }
        //const char* string = CHAR(curs);
        fprintf(stdout, "!NDEBUG: \tMARK_ASCII = %d\n", (IS_ASCII(curs) > 0));
        fprintf(stdout, "!NDEBUG: \tMARK_UTF8  = %d\n", (IS_UTF8(curs) > 0));
        fprintf(stdout, "!NDEBUG: \tMARK_LATIN1= %d\n", (IS_LATIN1(curs) > 0));
        fprintf(stdout, "!NDEBUG: \tMARK_BYTES = %d\n", (IS_BYTES(curs) > 0));
        fprintf(stdout, "!NDEBUG: \n");
    }
    UNPROTECT(1);
    return R_NilValue;
#else
    Rf_error("This function is enabled only if NDEBUG is undef.");
    return s;  // s here avoids compiler warning
#endif
}


/** for testing efficiency of StriContainerUTF16 [internal]
 *
 * @param str character vector
 * @return a charvec containing the UTF-8 round trip
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci_test_UnicodeContainer16(SEXP str)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    STRI__ERROR_HANDLER_BEGIN(1)
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    {
        R_len_t n = ci::checked_r_len(
            context.size(str), "character vectors"
        );
        StriContainerUTF16 ss(context, str, n);
    }
    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return R_NilValue;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/** for testing efficiency of StriContainerUTF16 [internal]
 *
 * @param str character vector
 * @return R_NilValue
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-03)
 */
SEXP ci_test_UnicodeContainer16b(SEXP str)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP ret;
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    {
        R_len_t n = ci::checked_r_len(
            context.size(str), "character vectors"
        );
        charport::charvec::Builder builder(n);
        {
            StriContainerUTF16 ss(context, str, n);
            std::vector<char> utf8_buffer;
            for (R_len_t i=0; i<n; ++i) {
                if (ss.isNA(i)) {
                    builder.set_na(i);
                    continue;
                }
                ci::builder_set(builder, i, ss.get(i), utf8_buffer);
            }
        }
        STRI__PROTECT(ret = builder.to_sexp());
    }
    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/** for testing efficiency of StriContainerUTF8  [internal]
 * @param str character vector
 * @return R_NilValue
 *
 * @version 0.1-?? (Marek Gagolewski)
 */
SEXP ci_test_UnicodeContainer8(SEXP str)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    STRI__ERROR_HANDLER_BEGIN(1)
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    {
        R_len_t n = ci::checked_r_len(
            context.size(str), "character vectors"
        );
        StriContainerUTF8 ss(context, str, n);
    }
    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return R_NilValue;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/** Round-trip through StriContainerUTF8 and a charvec Builder [internal]. */
SEXP ci_test_UnicodeContainer8b(SEXP str)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP ret;
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    {
        R_len_t n = ci::checked_r_len(
            context.size(str), "character vectors"
        );
        charport::charvec::Builder builder(n);
        {
            StriContainerUTF8 ss(context, str, n);
            for (R_len_t i=0; i<n; ++i)
                ci::builder_set(builder, i, ss.getNAble(i));
        }
        STRI__PROTECT(ret = builder.to_sexp());
    }
    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/** Exercise two UTF-8 containers using one shared ReaderContext [internal]. */
SEXP ci_test_UnicodeContainer8_alias(SEXP str)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP ret;
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    {
        R_len_t n = ci::checked_r_len(
            context.size(str), "character vectors"
        );
        charport::charvec::Builder builder(n);
        {
            StriContainerUTF8 first(context, str, n);
            StriContainerUTF8 second(context, str, n);
            for (R_len_t i=0; i<n; ++i)
                ci::builder_set(builder, i, second.getNAble(i));
        }
        STRI__PROTECT(ret = builder.to_sexp());
    }
    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/** Exercise two independent Readers over the same exact source [internal]. */
SEXP ci_test_UnicodeContainer8_independent(SEXP str)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP ret;
    ci::ReaderContext first_context(STRI__DEFERRED_WARNINGS);
    ci::ReaderContext second_context(STRI__DEFERRED_WARNINGS);
    {
        R_len_t n = ci::checked_r_len(
            first_context.size(str), "character vectors"
        );
        charport::charvec::Builder builder(n);
        {
            StriContainerUTF8 first(first_context, str, n);
            StriContainerUTF8 second(second_context, str, n);
            for (R_len_t i=0; i<n; ++i)
                ci::builder_set(builder, i, second.getNAble(i));
        }
        STRI__PROTECT(ret = builder.to_sexp());
    }
    first_context.emitWarnings();
    second_context.emitWarnings();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/** Exercise deferred warnings from a Reader-backed pattern container. */
SEXP ci_test_ByteSearchContainer(SEXP pattern)
{
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));
    STRI__ERROR_HANDLER_BEGIN(1)
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    {
        R_len_t n = ci::checked_r_len(
            context.size(pattern), "character vectors"
        );
        StriContainerByteSearch patterns(
            context, pattern, n, 0
        );
    }
    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return R_NilValue;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/** Queue a warning, then fail while the pattern Reader is active [internal]. */
SEXP ci_test_ByteSearchContainer_error(SEXP pattern)
{
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));
    STRI__ERROR_HANDLER_BEGIN(1)
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    {
        R_len_t n = ci::checked_r_len(
            context.size(pattern), "character vectors"
        );
        StriContainerByteSearch patterns(
            context, pattern, n, 0
        );
        throw StriException("error after queued warning");
    }
    STRI__UNPROTECT_ALL
    return R_NilValue;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/** Exercise length-bounded fixed matching on one subject record [internal]. */
SEXP ci_test_ByteSearchMatcher(
    SEXP str, SEXP pattern, SEXP case_insensitive
)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));
    bool case_insensitive_value = ci__prepare_arg_logical_1_notNA(
        case_insensitive, "case_insensitive"
    );
    STRI__ERROR_HANDLER_BEGIN(2)
    int first = USEARCH_DONE;
    int last = USEARCH_DONE;
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    {
        R_len_t str_n = ci::checked_r_len(
            context.size(str), "character vectors"
        );
        R_len_t pattern_n = ci::checked_r_len(
            context.size(pattern), "character vectors"
        );
        if (str_n < 1 || pattern_n != 1)
            throw StriException("test matcher requires a subject and one pattern");

        StriContainerUTF8 strings(context, str, str_n, true);
        StriContainerByteSearch patterns(
            context, pattern, pattern_n, 0
        );
        if (strings.isNA(0) || patterns.isNA(0) ||
                patterns.get(0).length() == 0) {
            throw StriException("test matcher requires non-missing, non-empty strings");
        }

        std::unique_ptr<StriByteSearchMatcher> forward =
            make_test_byte_matcher(patterns.get(0), case_insensitive_value);
        forward->reset(strings.get(0).data(), strings.get(0).length());
        first = forward->findFirst();

        // KMP tables are directional in the copied stringi matcher.
        std::unique_ptr<StriByteSearchMatcher> reverse =
            make_test_byte_matcher(patterns.get(0), case_insensitive_value);
        reverse->reset(strings.get(0).data(), strings.get(0).length());
        last = reverse->findLast();
    }
    context.emitWarnings();

    SEXP ret;
    STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
        SEXP value = Rf_allocVector(INTSXP, 2);
        INTEGER(value)[0] = first;
        INTEGER(value)[1] = last;
        return value;
    }));
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/** Exercise owned String8 self-assignment [internal]. */
SEXP ci_test_String8_assignment()
{
    STRI__ERROR_HANDLER_BEGIN(0)
    charport::charvec::Builder builder(3);
    String8 value("owned", 5, true, false, true);
    value = value;
    ci::builder_set(builder, 0, value);
    value.setNA();
    String8 missing(value);
    value = missing;
    ci::builder_set(builder, 1, value);
    char source_bytes[8] = {'b', 'o', 'r', 'r', 'o', 'w', 'e', 'd'};
    String8 borrowed(source_bytes, 8, false, false, true);
    value.assignOwned(borrowed);
    source_bytes[0] = 'X';
    ci::builder_set(builder, 2, value);

    SEXP ret;
    STRI__PROTECT(ret = builder.to_sexp());
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/** Verify Builder empty handling and which marks require an ASCII scan. */
SEXP ci_test_UTF8EncodingMarks()
{
    STRI__ERROR_HANDLER_BEGIN(0)
    const char ascii[] = {'a', 'b', 'c'};
    const char utf8[] = {static_cast<char>(0xc3), static_cast<char>(0xa9)};
    charport::charvec::Builder input_builder(5);
    ci::builder_set(input_builder, 0, utf8, 2, cetype_ext_t::CE_ASCII);
    ci::builder_set(input_builder, 1, ascii, 3, cetype_ext_t::CE_UTF8);
    ci::builder_set(
        input_builder, 2, ascii, 3, cetype_ext_t::CE_ASCII_OR_UTF8
    );
    ci::builder_set(
        input_builder, 3, utf8, 2, cetype_ext_t::CE_ASCII_OR_UTF8
    );
    // Exercise the raw-pointer adapter used by empty vector::data() calls.
    ci::builder_set(input_builder, 4, NULL, 0, cetype_ext_t::CE_LATIN1);

    charport::charvec::GrowableBuilder growable;
    ci::builder_append(growable, NULL, 0, cetype_ext_t::CE_UTF8);
    ci::builder_append(growable, NULL, 0, cetype_ext_t::CE_NA);

    SEXP input;
    STRI__PROTECT(input = input_builder.to_sexp());
    SEXP grown;
    STRI__PROTECT(grown = growable.to_sexp());
    int classification[7] = {
        FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE
    };
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    {
        StriContainerUTF8 values(context, input, 5, true);
        for (R_len_t i=0; i<4; ++i)
            classification[i] = values.get(i).isASCII() ? TRUE : FALSE;
        classification[4] = !values.isNA(4) && values.get(4).length() == 0;

        StriContainerUTF8 grown_values(context, grown, 2, true);
        classification[5] = !grown_values.isNA(0) &&
            grown_values.get(0).length() == 0 &&
            !grown_values.get(0).isASCII();
        classification[6] = grown_values.isNA(1);
    }
    context.emitWarnings();

    SEXP ret;
    STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
        SEXP value = Rf_allocVector(LGLSXP, 7);
        for (R_len_t i=0; i<7; ++i)
            LOGICAL(value)[i] = classification[i];
        return value;
    }));
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/** Exercise list preparation, exact aliases, and empty children [internal]. */
SEXP ci_test_ListUTF8(SEXP str, SEXP nrecycle)
{
    PROTECT(str = ci__prepare_arg_list_string(str, "str"));
    int nrecycle_value = ci__prepare_arg_integer_1_notNA(
        nrecycle, "nrecycle"
    );
    STRI__ERROR_HANDLER_BEGIN(1)
    if (nrecycle_value < 0)
        throw StriException("nrecycle must be non-negative");
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    {
        StriContainerListUTF8 values(
            context, str, nrecycle_value, true
        );
    }
    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return R_NilValue;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}
