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
#include "ci_brkiter.h"
#include "ci_string8buf.h"
#include "altrep/native_to_utf8.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

#include <unicode/ucasemap.h>


#define STRI_CASEMAP_TOLOWER   1
#define STRI_CASEMAP_TOUPPER   2
#define STRI_CASEMAP_CASEFOLD  3
#define STRI_CASEMAP_TOTITLE   4


namespace {

struct CasemapInput {
    const char* data;
    int32_t length;
    bool ascii;
};


bool ci__casemap_title_locale(const char* locale) noexcept
{
    if (!locale)
        return false;
    if (ci__is_C_locale(locale))
        return true;
    if (locale[0] == '\0')
        return true;
    if (locale[0] == 'r' && locale[1] == 'o' && locale[2] == 'o' &&
        locale[3] == 't' && locale[4] == '\0')
        return true;
    if (locale[0] != 'e' || locale[1] != 'n')
        return false;
    if (locale[2] != '\0' && locale[2] != '_' && locale[2] != '-')
        return false;
    for (const char* current = locale + 2; *current; ++current) {
        if (*current == '@')
            return false;
    }
    return true;
}


class CasemapTitleOptions : public StriBrkIterOptions {
public:
    CasemapTitleOptions(
        SEXP options, ci::DeferredWarnings& warnings
    ) : StriBrkIterOptions(options, "word", warnings) {}

    bool ascii_fast_path(const char* locale) const noexcept
    {
        // Only the standard English/root word iterator has the simple
        // letter-run behavior used by the byte loop below.
        return type == UBRK_WORD && rules.isEmpty() && skip_size == 0 &&
            ci__casemap_title_locale(locale);
    }
};


bool ci__casemap_has_bom(const char* data, int32_t length) noexcept
{
    return length >= 3 && STRI__ENC_HAS_BOM_UTF8(data, length);
}


bool ci__casemap_is_turkic_locale(const char* locale) noexcept
{
    if (!locale || locale[0] == '\0' || locale[1] == '\0')
        return false;
    const bool delimiter = locale[2] == '\0' || locale[2] == '_' ||
        locale[2] == '-' || locale[2] == '@';
    return delimiter &&
        ((locale[0] == 't' && locale[1] == 'r') ||
         (locale[0] == 'a' && locale[1] == 'z'));
}


bool ci__casemap_is_turkic(const UCaseMap* casemap) noexcept
{
    return ci__casemap_is_turkic_locale(ucasemap_getLocale(casemap));
}


bool ci__casemap_ascii_simple(const char* locale, int type) noexcept
{
    return type == STRI_CASEMAP_CASEFOLD ||
        ((type == STRI_CASEMAP_TOLOWER ||
          type == STRI_CASEMAP_TOUPPER) &&
         !ci__casemap_is_turkic_locale(locale));
}


bool ci__casemap_ascii_output(
    const UCaseMap* casemap, const CasemapInput& input
) noexcept
{
    return input.ascii && !ci__casemap_is_turkic(casemap);
}


void ci__casemap_ascii(
    const char* input, int32_t length, int type, char* output
) noexcept
{
    if (type == STRI_CASEMAP_TOUPPER) {
        for (int32_t i = 0; i < length; ++i) {
            unsigned char byte = static_cast<unsigned char>(input[i]);
            if (byte >= 'a' && byte <= 'z')
                byte -= static_cast<unsigned char>('a'-'A');
            output[i] = static_cast<char>(byte);
        }
    }
    else {
        for (int32_t i = 0; i < length; ++i) {
            unsigned char byte = static_cast<unsigned char>(input[i]);
            if (byte >= 'A' && byte <= 'Z')
                byte += static_cast<unsigned char>('a'-'A');
            output[i] = static_cast<char>(byte);
        }
    }
}


bool ci__casemap_ascii_title_eligible(
    const CasemapInput& input
) noexcept
{
    for (int32_t i = 0; i < input.length; ++i) {
        const unsigned char byte = static_cast<unsigned char>(input.data[i]);
        if (byte >= 0x80U)
            return false;
        if ((byte >= 'A' && byte <= 'Z') ||
            (byte >= 'a' && byte <= 'z'))
            continue;
        const bool whitespace = byte == ' ' || (byte >= '\t' && byte <= '\r');
        const bool punctuation =
            (byte >= 0x21U && byte <= 0x2fU) ||
            (byte >= 0x3aU && byte <= 0x40U) ||
            (byte >= 0x5bU && byte <= 0x60U) ||
            (byte >= 0x7bU && byte <= 0x7eU);
        // ICU can keep these characters inside a word. Digits take the same
        // fallback through the non-punctuation branch.
        if ((!whitespace && !punctuation) || byte == '\'' || byte == '_')
            return false;
        if (byte == '.' && i > 0 && i + 1 < input.length) {
            const unsigned char previous = static_cast<unsigned char>(
                input.data[i-1]
            );
            const unsigned char next = static_cast<unsigned char>(
                input.data[i+1]
            );
            const bool previous_letter =
                (previous >= 'A' && previous <= 'Z') ||
                (previous >= 'a' && previous <= 'z');
            const bool next_letter =
                (next >= 'A' && next <= 'Z') ||
                (next >= 'a' && next <= 'z');
            if (previous_letter && next_letter)
                return false;
        }
    }
    return true;
}


void ci__casemap_ascii_title(
    const CasemapInput& input, char* output
) noexcept
{
    bool word_start = true;
    for (int32_t i = 0; i < input.length; ++i) {
        unsigned char byte = static_cast<unsigned char>(input.data[i]);
        if (byte >= 'A' && byte <= 'Z') {
            if (!word_start)
                byte += static_cast<unsigned char>('a'-'A');
            word_start = false;
        }
        else if (byte >= 'a' && byte <= 'z') {
            if (word_start)
                byte -= static_cast<unsigned char>('a'-'A');
            word_start = false;
        }
        else {
            word_start = true;
        }
        output[i] = static_cast<char>(byte);
    }
}


void ci__casemap_ascii_title(
    const CasemapInput& input, charport::charvec::Builder& builder,
    R_xlen_t index
)
{
    char* output = builder.reserve(
        index, static_cast<std::size_t>(input.length),
        cetype_ext_t::CE_ASCII
    );
    ci__casemap_ascii_title(input, output);
}


int32_t ci__casemap_call(
    UCaseMap* casemap, int type, char* dest, int32_t capacity,
    const char* src, int32_t length, UErrorCode* status
) noexcept
{
    if (type == STRI_CASEMAP_TOLOWER) {
        return ucasemap_utf8ToLower(
            casemap, dest, capacity, src, length, status
        );
    }
    if (type == STRI_CASEMAP_TOUPPER) {
        return ucasemap_utf8ToUpper(
            casemap, dest, capacity, src, length, status
        );
    }
    if (type == STRI_CASEMAP_CASEFOLD) {
        return ucasemap_utf8FoldCase(
            casemap, dest, capacity, src, length, status
        );
    }
    return ucasemap_utf8ToTitle(
        casemap, dest, capacity, src, length, status
    );
}


void ci__casemap_icu(
    UCaseMap* casemap, int type, const CasemapInput& input,
    String8buf& buffer, charport::charvec::Builder& builder,
    R_xlen_t index
)
{
    const std::size_t margin = 10;
    buffer.resize(static_cast<std::size_t>(input.length) + margin, false);

    const std::size_t int32_max = static_cast<std::size_t>(
        std::numeric_limits<int32_t>::max()
    );
    int32_t capacity = static_cast<int32_t>(
        std::min(buffer.size(), int32_max)
    );

    UErrorCode status = U_ZERO_ERROR;
    int32_t output_length = ci__casemap_call(
        casemap, type, buffer.data(), capacity,
        input.data, input.length, &status
    );
    if (!U_FAILURE(status)) {
        const bool output_ascii = ci__casemap_ascii_output(casemap, input) ||
            ci::is_ascii(buffer.data(), static_cast<std::size_t>(output_length));
        ci::builder_set(
            builder, index, buffer.data(), output_length,
            output_ascii ? cetype_ext_t::CE_ASCII : cetype_ext_t::CE_UTF8
        );
        return;
    }

    if (output_length < 0)
        STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})

    buffer.resize(static_cast<std::size_t>(output_length), false);
    capacity = static_cast<int32_t>(
        std::min(buffer.size(), int32_max)
    );
    status = U_ZERO_ERROR;
    output_length = ci__casemap_call(
        casemap, type, buffer.data(), capacity,
        input.data, input.length, &status
    );
    STRI__CHECKICUSTATUS_THROW(status, {/* nothing special */})

    const bool output_ascii = ci__casemap_ascii_output(casemap, input) ||
        ci::is_ascii(buffer.data(), static_cast<std::size_t>(output_length));
    ci::builder_set(
        builder, index, buffer.data(), output_length,
        output_ascii ? cetype_ext_t::CE_ASCII : cetype_ext_t::CE_UTF8
    );
}


CasemapInput ci__casemap_input(
    const charport::StrView& value,
    charr::altrep::NativeToUtf8& converter,
    bool classify_ascii = true
)
{
    if (value.len < 0 || !value.ptr)
        throw StriException("Reader returned an invalid string view");

    const char* data = value.ptr;
    int32_t length = value.len;
    bool strip_bom = false;
    bool ascii = false;
    bool ascii_known = false;

    switch (value.enc) {
    case cetype_ext_t::CE_ASCII:
        ascii = true;
        ascii_known = true;
        break;
    case cetype_ext_t::CE_UTF8:
        strip_bom = true;
        // An explicit UTF-8 mark is authoritative. Among records already in
        // UTF-8, only the ambiguous mark needs a payload scan.
        ascii_known = true;
        break;
    case cetype_ext_t::CE_ASCII_OR_UTF8:
        strip_bom = ci__casemap_has_bom(data, length);
        break;
    case cetype_ext_t::CE_BYTES:
        throw StriException(MSG__BYTESENC);
    case cetype_ext_t::CE_LATIN1: {
        const charport::ByteView converted = converter.latin1(data, length);
        data = converted.ptr;
        length = converted.len;
        break;
    }
    case cetype_ext_t::CE_NATIVE: {
        const bool native_bom = ci__casemap_has_bom(data, length);
        const charport::ByteView converted = converter.native(data, length);
        data = converted.ptr;
        length = converted.len;
        strip_bom = native_bom;
        break;
    }
    case cetype_ext_t::CE_NA:
        throw StriException("non-missing Reader record has NA encoding");
    default:
        throw StriException("unknown charport string encoding");
    }

    if (strip_bom && ci__casemap_has_bom(data, length)) {
        data += 3;
        length -= 3;
    }
    if (!ascii_known && classify_ascii)
        ascii = ci::is_ascii(data, static_cast<std::size_t>(length));
    return CasemapInput{data, length, ascii};
}

} // namespace


/**
 *  Convert case (TitleCase)
 *
 *
 *  @param str character vector
 *  @param opts_brkiter list
 *  @return character vector
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-03)
 *    separated from ci_trans_casemap;
 *    use StriUBreakIterator
 */
SEXP ci_trans_totitle(SEXP str, SEXP opts_brkiter) {
// version 0.2-1 - Does not work with ICU 4.8 (but we require ICU >= 50)
    UCaseMap* ucasemap = NULL;

    STRI__ERROR_HANDLER_BEGIN(0)
    SEXP ret;
    {
        // Deviation from stringi: construct break-iterator options inside the
        // C++ error boundary before preparing str, preserving condition order
        // without letting a later R unwind skip their ICU-owned storage.
        CasemapTitleOptions opts_brkiter2(
            opts_brkiter, STRI__DEFERRED_WARNINGS
        );
        STRI__PROTECT(str = charport::unwind_protect([&]() -> SEXP {
            return ci__prepare_arg_string(
                str, "str", true, &STRI__DEFERRED_WARNINGS
            );
        }));

        StriUBreakIterator brkiter(opts_brkiter2);

        UErrorCode status = U_ZERO_ERROR;
        ucasemap = ucasemap_open(brkiter.getLocale(), U_FOLD_CASE_DEFAULT, &status);
        STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

        status = U_ZERO_ERROR;
        ucasemap_setBreakIterator(
            ucasemap, brkiter.getIterator(STRI__DEFERRED_WARNINGS), &status
        );
        STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
        brkiter.free(false);
        const bool ascii_fast_path = opts_brkiter2.ascii_fast_path(
            ucasemap_getLocale(ucasemap)
        );
        // ucasemap_setOptions(ucasemap, U_TITLECASE_NO_LOWERCASE, &status); // to do?
        // now briter is owned by ucasemap.
        // it will be released on ucasemap_close
        // (checked with ICU man & src code)

        // Stage native storage first so ALTREP construction happens after the
        // foreign Reader and its views have been released.
        charport::charvec::Store output = [&]() {
            charport::Reader reader(str);
            R_len_t str_n = ci::checked_r_len(
                reader.size(), "character vectors"
            );
            charport::charvec::Builder builder(str_n);
            charport::StrViews values(str_n);
            if (str_n > 0)
                reader.views(0, str_n, values);
            charr::altrep::NativeToUtf8 converter;
            std::unique_ptr<String8buf> buffer;
            for (R_len_t i = 0; i < str_n; ++i) {
                const charport::StrView value = values[i];
                if (value.is_na()) {
                    builder.set_na(i);
                    continue;
                }

                const CasemapInput input = ci__casemap_input(
                    value, converter, !ascii_fast_path
                );
                if (ascii_fast_path &&
                    ci__casemap_ascii_title_eligible(input)) {
                    ci__casemap_ascii_title(input, builder, i);
                }
                else {
                    if (!buffer)
                        buffer.reset(new String8buf(64));
                    ci__casemap_icu(
                        ucasemap, STRI_CASEMAP_TOTITLE, input,
                        *buffer, builder, i
                    );
                }
            }
            return builder.release_store();
        }();

        if (ucasemap) {
            ucasemap_close(ucasemap);
            ucasemap = NULL;
        }

        STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;

    STRI__ERROR_HANDLER_END({
        if (ucasemap) {
            ucasemap_close(ucasemap);
            ucasemap = NULL;
        }
    })
}


/**
 *  Convert case (upper, lowercase, fold)
 *
 *
 *  @param str character vector
 *  @param locale single string identifying
 *         the locale ("" or NULL for default locale)
 *  @return character vector
 *
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          use StriContainerUTF16
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException-friendly
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-11-19)
 *          use UCaseMap + Utf8Input
 *          **THIS DOES NOT WORK WITH ICU 4.8**, we have to revert the changes
 *          ** BTW, since stringi_0.1-25 we require ICU>=50 **
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-18)
 *          use UCaseMap + Utf8Input
 *          (this is much faster for UTF-8 and slightly faster for 8bit enc)
 *          Estimates minimal buffer size.
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-10-24)
 *          Use a custom BreakIterator with ci_trans_totitle
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-03)
 *    use StriUBreakIterator
 *
 * @version 0.6-1 (Marek Gagolewski, 2015-07-11)
 *    now this is an internal function
 *
 * @version 1.6.1 (Marek Gagolewski, 2021-04-30)
 *    add casefold
*/
SEXP ci_trans_casemap(SEXP str, int _type, SEXP locale)
{
    if (_type < 1 || _type > 3)
        Rf_error(MSG__INCORRECT_INTERNAL_ARG);
    const char* qloc = ci__prepare_arg_locale(locale, "locale"); /* this is R_alloc'ed */
    PROTECT(str = ci__prepare_arg_string(str, "str")); // prepare string argument

    // version 0.2-1 - Does not work with ICU 4.8 (but we require ICU >= 50)
    UCaseMap* ucasemap = NULL;

    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP ret;
    {
        ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
        R_len_t str_n = ci::checked_r_len(
            context.size(str), "character vectors"
        );
        charport::charvec::Builder builder(str_n);
        {
            std::shared_ptr<ci::ReaderBorrow> borrow = context.acquire(str);
            const charport::StrViews& values = borrow->views();
            charr::altrep::NativeToUtf8 converter;
            const bool simple_ascii = ci__casemap_ascii_simple(
                qloc, _type
            );
            std::unique_ptr<String8buf> buffer;
            for (R_len_t i = 0; i < str_n; ++i) {
                const charport::StrView value = values[i];
                if (value.is_na()) {
                    builder.set_na(i);
                    continue;
                }

                const CasemapInput input = ci__casemap_input(
                    value, converter
                );
                if (simple_ascii && input.ascii) {
                    char* output = builder.reserve(
                        i, static_cast<std::size_t>(input.length),
                        cetype_ext_t::CE_ASCII
                    );
                    ci__casemap_ascii(
                        input.data, input.length, _type, output
                    );
                    continue;
                }

                if (!ucasemap) {
                    UErrorCode status = U_ZERO_ERROR;
                    ucasemap = ucasemap_open(
                        qloc, U_FOLD_CASE_DEFAULT, &status
                    );
                    STRI__CHECKICUSTATUS_THROW(
                        status, {/* nothing special */}
                    )
                }
                if (!buffer)
                    buffer.reset(new String8buf(64));
                ci__casemap_icu(
                    ucasemap, _type, input, *buffer, builder, i
                );
            }
        }

        if (ucasemap) {
            ucasemap_close(ucasemap);
            ucasemap = NULL;
        }

        STRI__PROTECT(ret = builder.to_sexp());
    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;

    STRI__ERROR_HANDLER_END({
        if (ucasemap) {
            ucasemap_close(ucasemap);
            ucasemap = NULL;
        }
    })
}


/**
 *  Convert to lower case
 *
 *
 *  @param str character vector
 *  @param locale single string identifying
 *         the locale ("" or NULL for default locale)
 *  @return character vector
 *
 * @version 0.6-1 (Marek Gagolewski, 2015-07-11)
 *    call ci_trans_casemap
*/
SEXP ci_trans_tolower(SEXP str, SEXP locale)
{
    return ci_trans_casemap(str, STRI_CASEMAP_TOLOWER, locale);
}


/**
 *  Convert to lower case
 *
 *
 *  @param str character vector
 *  @param locale single string identifying
 *         the locale ("" or NULL for default locale)
 *  @return character vector
 *
 * @version 0.6-1 (Marek Gagolewski, 2015-07-11)
 *    call ci_trans_casemap
*/
SEXP ci_trans_toupper(SEXP str, SEXP locale)
{
    return ci_trans_casemap(str, STRI_CASEMAP_TOUPPER, locale);
}



/**
 *  Case folding
 *
 *  @param str character vector
 *  @return character vector
 *
 * @version 1.6.1 (Marek Gagolewski, 2021-04-30)
 *    call ci_trans_casemap
*/
SEXP ci_trans_casefold(SEXP str) {
    return ci_trans_casemap(str, STRI_CASEMAP_CASEFOLD, R_NilValue);
}


// v0.1-?? - UTF-16 - WORKS WITH ICU 4.8
// (this is much slower for UTF-8 and slightly slower for 8bit enc)
// Slower than v0.2-1
////    BreakIterator* briter = NULL;
//
//   STRI__ERROR_HANDLER_BEGIN
//
//   if (!Rf_isInteger(type) || LENGTH(type) != 1)
//      throw StriException(MSG__INCORRECT_INTERNAL_ARG); // this is an internal arg, check manually
//   int _type = INTEGER(type)[0];
//
//
//   Locale loc = Locale::createFromName(qloc); // this will be freed automatically
//   StriContainerUTF16 str_cont(str, LENGTH(str), false); // writable, no recycle
//
////    if (_type == 6) {
////       UErrorCode status = U_ZERO_ERROR;
////       briter = BreakIterator::createWordInstance(loc, status);
////       STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
////    }
//
//   for (R_len_t i = str_cont.vectorize_init();
//         i != str_cont.vectorize_end();
//         i = str_cont.vectorize_next(i))
//   {
//      if (!str_cont.isNA(i)) {
//         switch (_type) {
//            case 1:
//               str_cont.getWritable(i).toLower(loc);
//               break;
//            case 2:
//               str_cont.getWritable(i).toUpper(loc);
//               break;
//            case 3:
//               str_cont.getWritable(i).toTitle(NULL, loc); // use default ICU's BreakIterator
//               break;
//            case 4:
//               str_cont.getWritable(i).foldCase(U_FOLD_CASE_DEFAULT);
//               break;
//            case 5:
//               str_cont.getWritable(i).foldCase(U_FOLD_CASE_EXCLUDE_SPECIAL_I);
//               break;
////             case 6:
////                str_cont.getWritable(i).toTitle(briter, loc); // how to get it working properly with English text???
////                                                                 I guess ICU doesn't support language-sensitive title casing at all...
////                break;
//            default:
//               throw StriException("ci_trans_case: incorrect case conversion type");
//         }
//      }
//   }
//
////    if (briter) { delete briter; briter = NULL; }
//   SEXP ret;
//   PROTECT(ret = str_cont.toR());
//   UNPROTECT(1);
//   return ret;
//   STRI__ERROR_HANDLER_END(/*noop*/;
////       if (briter) delete briter;
//   )
