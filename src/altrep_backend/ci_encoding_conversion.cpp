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
#include "io/utf16_input.h"
#include "io/raw_list_input.h"
#include "ci_string8buf.h"
#include "ci_ucnv.h"
#include "../shared/native_to_utf8.h"
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace charr { namespace altrep_backend {


#define BUF_MAX_LENGTH 2147483647


namespace encoding_conversion {

static const char CI__EMBEDDED_NUL_MESSAGE[] =
    "embedded nul in string";


struct CiRawResult {
    bool is_na;
    std::vector<unsigned char> data;

    CiRawResult() : is_na(true), data() {}
};


static cetype_ext_t ci__extended_encoding(cetype_t encoding)
{
    switch (encoding) {
    case CE_UTF8:
        return cetype_ext_t::CE_UTF8;
    case CE_LATIN1:
        return cetype_ext_t::CE_LATIN1;
    case CE_BYTES:
        return cetype_ext_t::CE_BYTES;
    default:
        return cetype_ext_t::CE_NATIVE;
    }
}


static const char* ci__nonnull_bytes(const char* data, size_t length)
{
    return length == 0 && data == NULL ? "" : data;
}


static bool ci__utf8_identity_name(const char* encoding)
{
    return encoding != NULL &&
        ucnv_compareNames(encoding, "UTF-8") == 0;
}


static bool ci__valid_utf8_bytes(
    const char* data, R_len_t length, bool& ascii, bool& embedded_nul
)
{
    ascii = true;
    embedded_nul = false;
    R_len_t i = 0;
    while (i < length) {
        const uint8_t byte = static_cast<uint8_t>(data[i]);
        if (byte == 0)
            embedded_nul = true;
        if (byte <= 0x7fU) {
            ++i;
            continue;
        }

        ascii = false;
        UChar32 code_point = 0;
        U8_NEXT(data, i, length, code_point);
        if (code_point < 0)
            return false;
    }
    return true;
}


static bool ci__utf8_identity_character(
    SEXP input, bool respect_marks, ci::ReaderContext& context,
    std::shared_ptr<ci::ReaderBorrow>& borrow,
    charport::charvec::Store& output
)
{
    borrow = context.acquire(input);
    const R_len_t size = ci::checked_r_len(
        borrow->size(), "character vectors"
    );
    const charport::StrViews& views = borrow->views();
    std::vector<cetype_ext_t> encodings(static_cast<size_t>(size));
    for (R_len_t i=0; i<size; ++i) {
        const charport::StrView value = views[i];
        if (value.is_na()) {
            encodings[static_cast<size_t>(i)] = cetype_ext_t::CE_NA;
            continue;
        }
        if (value.len < 0 || (value.ptr == nullptr && value.len != 0))
            throw std::runtime_error("Reader returned an invalid string view");
        if (respect_marks &&
                value.enc != cetype_ext_t::CE_ASCII &&
                value.enc != cetype_ext_t::CE_UTF8 &&
                value.enc != cetype_ext_t::CE_ASCII_OR_UTF8) {
            return false;
        }
        if (value.enc == cetype_ext_t::CE_ASCII) {
            encodings[static_cast<size_t>(i)] = cetype_ext_t::CE_ASCII;
            continue;
        }

        bool value_is_ascii = false;
        bool embedded_nul = false;
        const char* data = ci__nonnull_bytes(
            value.ptr, static_cast<size_t>(value.len)
        );
        if (!ci__valid_utf8_bytes(
                data, value.len, value_is_ascii, embedded_nul
            ) || embedded_nul) {
            return false;
        }
        encodings[static_cast<size_t>(i)] = value_is_ascii
            ? cetype_ext_t::CE_ASCII
            : cetype_ext_t::CE_UTF8;
    }

    charport::charvec::Builder builder(size);
    for (R_len_t i=0; i<size; ++i) {
        const charport::StrView value = views[i];
        const cetype_ext_t encoding = encodings[static_cast<size_t>(i)];
        if (encoding == cetype_ext_t::CE_NA) {
            builder.set_na(i);
            continue;
        }
        ci::builder_set(
            builder, i,
            ci__nonnull_bytes(
                value.ptr, static_cast<size_t>(value.len)
            ),
            static_cast<size_t>(value.len),
            encoding
        );
    }
    output = builder.release_store();
    return true;
}


static size_t ci__transcode_direct(
    UConverter* source_converter, UConverter* target_converter,
    const char* input, R_len_t input_length, String8buf& output
)
{
    const size_t maximum = static_cast<size_t>(BUF_MAX_LENGTH);
    const size_t max_char_size = static_cast<size_t>(
        ucnv_getMaxCharSize(target_converter)
    );
    size_t capacity = 64;
    if (input_length > 0) {
        const size_t input_size = static_cast<size_t>(input_length);
        capacity = input_size > (maximum-1)/max_char_size
            ? maximum-1
            : input_size*max_char_size;
        if (capacity < 64)
            capacity = 64;
    }
    output.resize(capacity-1, false);

    const char empty[] = "";
    const char* source = input_length == 0 && input == nullptr ? empty : input;
    const char* source_limit = source+input_length;
    UChar pivot[1024];
    UChar* pivot_source = pivot;
    UChar* pivot_target = pivot;
    bool reset = true;
    size_t used = 0;

    for (;;) {
        char* target = output.data()+used;
        const char* target_limit = output.data()+output.size();
        UErrorCode status = U_ZERO_ERROR;
        ucnv_convertEx(
            target_converter, source_converter,
            &target, target_limit, &source, source_limit,
            pivot, &pivot_source, &pivot_target, pivot+1024,
            reset, true, &status
        );
        used = static_cast<size_t>(target-output.data());
        if (status != U_BUFFER_OVERFLOW_ERROR) {
            STRI__CHECKICUSTATUS_THROW(status, {})
            return used;
        }

        const size_t old_capacity = output.size();
        if (old_capacity >= maximum)
            throw StriException(MSG__BUF_SIZE_EXCEEDED);
        const size_t new_capacity = old_capacity > maximum/2
            ? maximum
            : old_capacity*2;
        output.resize(new_capacity-1, true);
        // A target overflow leaves converter and pivot state mid-record.
        // Continue that record after growing the buffer; the next record's
        // first call uses reset=true again.
        reset = false;
    }
}


static void ci__queue_formatted_warning(
    ci::DeferredWarnings& warnings, const char* format, ...
)
{
    char message[StriException_BUFSIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(message, StriException_BUFSIZE, format, args);
    va_end(args);
    warnings.push(message);
}


static void ci__reject_embedded_nul(const char* data, size_t length)
{
    if (length > 0 && memchr(data, 0, length) != NULL)
        throw StriException(CI__EMBEDDED_NUL_MESSAGE);
}


static void ci__set_validated_utf8(
    charport::charvec::Builder& builder, R_xlen_t i,
    const char* data, R_len_t length, cetype_ext_t encoding,
    int validate, ci::ReaderContext& context
)
{
    data = ci__nonnull_bytes(data, static_cast<size_t>(length));
    if (validate == FALSE) {
        ci::builder_set(builder, i, data, length, encoding);
        return;
    }

    R_len_t j = 0;
    UChar32 c = 0;
    while (c >= 0 && j < length)
        U8_NEXT(data, j, length, c);

    if (c >= 0) {
        ci::builder_set(builder, i, data, length, encoding);
        return;
    }

    if (validate == NA_LOGICAL) {
        context.warn(MSG__INVALID_CODE_POINT_REPLNA);
        builder.set_na(i);
        return;
    }

    size_t bufsize = static_cast<size_t>(length)*3;
    String8buf buf(bufsize);
    char* bufdata = buf.data();
    j = 0;
    size_t k = 0;
    UBool err = FALSE;
    while (!err && j < length) {
        U8_NEXT(data, j, length, c);
        if (c >= 0) {
            U8_APPEND((uint8_t*)bufdata, k, bufsize, c, err);
        }
        else {
            context.warn(MSG__INVALID_CODE_POINT_FIXING);
            bufdata[k++] = (char)UCHAR_REPLACEMENT_UTF8_BYTE1;
            bufdata[k++] = (char)UCHAR_REPLACEMENT_UTF8_BYTE2;
            bufdata[k++] = (char)UCHAR_REPLACEMENT_UTF8_BYTE3;
        }
    }

    if (err)
        throw StriException(MSG__INTERNAL_ERROR);
    ci::builder_set(
        builder, i, bufdata, k,
        cetype_ext_t::CE_UTF8
    );
}


} // namespace encoding_conversion

using namespace encoding_conversion;


// ------------------------------------------------------------------------

/**
 * Convert character vector between marked encodings and the encoding provided
 *
 * @param str     input character vector
 * @param to    target encoding, \code{NULL} or \code{""} for default enc
 * @param to_raw single logical, should list of raw vectors be returned?
 * @return a converted character vector or list of raw vectors
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-11-12)
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-28)
 *     use StriUcnv
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-01)
 *     calc required buf size a priori
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *     #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.8.4.9001 (Marek Gagolewski, 2024-06-13)
 *     #512: Fixed PROTECT stack imbalance in `ci_encode_from_marked`.
 */
SEXP ci_encode_from_marked(SEXP str, SEXP to, SEXP to_raw)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    const char* selected_to   = ci__prepare_arg_enc(to, "to", true); /* this is R_alloc'ed */
    bool to_raw_logical = ci__prepare_arg_logical_1_notNA(to_raw, "to_raw");

    STRI__ERROR_HANDLER_BEGIN(1)
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    std::shared_ptr<ci::ReaderBorrow> identity_borrow;

    if (!to_raw_logical && ci__utf8_identity_name(selected_to)) {
        charport::charvec::Store output(0, 0);
        if (ci__utf8_identity_character(
                str, true, context, identity_borrow, output
            )) {
            identity_borrow.reset();
            SEXP ret;
            STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
                return charport::charvec::wrap(std::move(output));
            }));
            context.emitWarnings();
            STRI__UNPROTECT_ALL
            return ret;
        }
    }

    SEXP ret;
    {
        R_len_t str_n = ci::checked_r_len(
            context.size(str), "character vectors"
        );
        std::vector<CiRawResult> raw_outputs;
        std::unique_ptr<charport::charvec::Builder> builder;
        if (to_raw_logical)
            raw_outputs.resize(static_cast<size_t>(str_n));
        else
            builder.reset(new charport::charvec::Builder(str_n));

        if (str_n > 0) {
            {
                io::Utf16Input str_cont(context, str, str_n);

                // Open converters
                StriUcnv ucnv(
                    selected_to ? selected_to : "UTF-8",
                    STRI__DEFERRED_WARNINGS
                );
                UConverter* uconv_to = ucnv.getConverter();
                // Staging is only needed where the native encoding differs
                // from the UTF-8 side ICU sees. When it is UTF-8 the pass is
                // an identity transcode.
                std::unique_ptr<charr::shared::NativeToUtf8>
                    native_output;
                if (!selected_to) {
                    native_output.reset(
                        new charr::shared::NativeToUtf8()
                    );
                    if (native_output->native_is_utf8())
                        native_output.reset();
                }
                // Deviation from stringi: converter callbacks queue their warnings
                // until the converter and Reader-backed inputs have been released.

                // Get target encoding mark
                cetype_t encmark_to = to_raw_logical
                    ? CE_BYTES
                    : (selected_to ? ucnv.getCE() : CE_NATIVE);
                cetype_ext_t encmark_to2 =
                    ci__extended_encoding(encmark_to);
                if (encmark_to2 == cetype_ext_t::CE_UTF8)
                    encmark_to2 = cetype_ext_t::CE_ASCII_OR_UTF8;

                // calculate required buf size
                size_t bufsize = 0;
                for (R_len_t i=0; i<str_n; ++i) {
                    if (!str_cont.isNA(i) &&
                            (size_t)str_cont.get(i).length() > bufsize)
                        bufsize = str_cont.get(i).length();
                }
                bufsize = UCNV_GET_MAX_BYTES_FOR_STRING(
                    bufsize, ucnv_getMaxCharSize(uconv_to)
                );
                // "The calculated size is guaranteed to be sufficient for this conversion."
                if (bufsize > BUF_MAX_LENGTH)
                    bufsize = BUF_MAX_LENGTH;
                String8buf buf(bufsize);

                for (R_len_t i=0; i<str_n; ++i) {
                    if (str_cont.isNA(i)) {
                        if (!to_raw_logical)
                            builder->set_na(i);
                        continue;
                    }

                    R_len_t curn_tmp = str_cont.get(i).length();
                    const UChar* curs_tmp = str_cont.get(i).getBuffer(); // The buffer content is (probably) not NUL-terminated.
                    if (!curs_tmp)
                        throw StriException(MSG__INTERNAL_ERROR);

                    UErrorCode status = U_ZERO_ERROR;
                    ucnv_resetFromUnicode(uconv_to);
                    size_t bufneed = ucnv_fromUChars(
                        uconv_to, buf.data(), buf.size(),
                        curs_tmp, curn_tmp, &status
                    );
                    if (bufneed <= buf.size()) {
                        STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
                    }
                    else {// larger buffer needed
                        if (bufneed > BUF_MAX_LENGTH)
                            throw StriException(MSG__BUF_SIZE_EXCEEDED);
                        buf.resize(bufneed, false/*destroy contents*/);
                        status = U_ZERO_ERROR;
                        ucnv_resetFromUnicode(uconv_to);
                        bufneed = ucnv_fromUChars(
                            uconv_to, buf.data(), buf.size(),
                            curs_tmp, curn_tmp, &status
                        );
                        STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
                    }

                    const char* output_data = buf.data();
                    size_t output_length = bufneed;
                    if (native_output) {
                        const shared::ByteView converted =
                            native_output->utf8_to_native(
                                output_data,
                                static_cast<int>(output_length)
                            );
                        output_data = converted.ptr;
                        output_length =
                            static_cast<size_t>(converted.len);
                    }

                    if (to_raw_logical) {
                        CiRawResult& output =
                            raw_outputs[static_cast<size_t>(i)];
                        output.is_na = false;
                        output.data.assign(
                            reinterpret_cast<const unsigned char*>(
                                output_data
                            ),
                            reinterpret_cast<const unsigned char*>(
                                output_data
                            )+output_length
                        );
                    }
                    else {
                        // Deviation from stringi: Builder accepts zero bytes, so
                        // preserve the copied character-output rejection locally.
                        ci__reject_embedded_nul(
                            output_data, output_length
                        );
                        ci::builder_set(
                            *builder, i, output_data, output_length,
                            encmark_to2
                        );
                    }
                }
            }
        }

        identity_borrow.reset();
        if (!to_raw_logical) {
            STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
                return builder->to_sexp();
            }));
        }
        else {
            // Deviation from stringi: stage data-dependent raw results until the
            // input and converter lifetimes have ended, then assemble the R list.
            STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
                return Rf_allocVector(VECSXP, str_n);
            }));
            for (R_len_t i=0; i<str_n; ++i) {
                const CiRawResult& output =
                    raw_outputs[static_cast<size_t>(i)];
                if (output.is_na)
                    continue;

                SEXP outobj;
                STRI__PROTECT(outobj = ci::unwind_protect([&]() -> SEXP {
                    return Rf_allocVector(
                        RAWSXP, static_cast<R_xlen_t>(output.data.size())
                    );
                }));
                if (!output.data.empty()) {
                    memcpy(
                        RAW(outobj), output.data.data(), output.data.size()
                    );
                }
                SET_VECTOR_ELT(ret, i, outobj);
                STRI__UNPROTECT(1);
            }
        }
    }

    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return ret;

    STRI__ERROR_HANDLER_END({/* nothing special on error */})
}


/**
 * Convert character vector between given encodings
 *
 * @param str     input character/raw vector or list of raw vectors
 * @param from  source encoding, \code{NULL} or \code{""} for default enc
 * @param to    target encoding, \code{NULL} or \code{""} for default enc
 * @param to_raw single logical, should list of raw vectors be returned?
 * @return a converted character vector or list of raw vectors
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          arg to_raw_added, encoding marking
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException-friendly
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-08)
 *          use io::RawListInput
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-11-20)
 *          BUGFIX call ci_encode_from_marked if necessary
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-28)
 *          use StriUcnv
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-01)
 *          estimate required buf size a priori
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci_encode(SEXP str, SEXP from, SEXP to, SEXP to_raw)
{
    const char* selected_from = ci__prepare_arg_enc(from, "from", true); /* this is R_alloc'ed */
    if (!selected_from && Rf_isVectorAtomic(str) && !isRaw(str))
        return ci_encode_from_marked(str, to, to_raw);
    const char* selected_to   = ci__prepare_arg_enc(to, "to", true); /* this is R_alloc'ed */
    bool to_raw_logical = ci__prepare_arg_logical_1_notNA(to_raw, "to_raw");

    // raw vector, character vector, or list of raw vectors:
    PROTECT(str = ci__prepare_arg_list_raw(str, "str"));


    STRI__ERROR_HANDLER_BEGIN(1)
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    std::shared_ptr<ci::ReaderBorrow> identity_borrow;

    // Explicit UTF-8-to-UTF-8 character conversion needs validation, but no
    // transcoding. Borrow each record once and copy valid bytes straight into
    // the output Store; malformed input and embedded NULs keep the converter
    // path, including its warnings and replacement behavior.
    if (!to_raw_logical && TYPEOF(str) == STRSXP &&
            ci__utf8_identity_name(selected_from) &&
            ci__utf8_identity_name(selected_to)) {
        charport::charvec::Store output(0, 0);
        if (ci__utf8_identity_character(
                str, false, context, identity_borrow, output
            )) {
            identity_borrow.reset();
            SEXP ret;
            STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
                return charport::charvec::wrap(std::move(output));
            }));
            context.emitWarnings();
            STRI__UNPROTECT_ALL
            return ret;
        }
    }

    SEXP ret;
    {
        R_len_t str_n = 0;
        std::vector<CiRawResult> raw_outputs;
        std::unique_ptr<charport::charvec::Builder> builder;
        {
            io::RawListInput str_cont(context, str);
            str_n = str_cont.get_n();
            if (to_raw_logical)
                raw_outputs.resize(static_cast<size_t>(str_n));
            else
                builder.reset(new charport::charvec::Builder(str_n));

            // get the number of strings to convert; if == 0, then you know what's the result
            if (str_n > 0) {
                // Optimized-backend deviation from stringi: R's native
                // encoding follows the current LC_CTYPE, whereas ICU's
                // default converter may be fixed to UTF-8 at build time.
                // Decode default raw input with Riconv; explicit source
                // encodings continue through ICU below.
                //
                // A UTF-8 native encoding needs no staging pass, because the
                // input bytes already are the target encoding. Falling
                // through to ICU is both cheaper and closer to stringi:
                // Riconv rejects malformed input and fails the whole vector,
                // while ICU's callbacks substitute per record and warn.
                std::unique_ptr<charr::shared::NativeToUtf8> raw_native;
                if (!selected_from && !to_raw_logical && selected_to &&
                        ucnv_compareNames(selected_to, "UTF-8") == 0) {
                    raw_native.reset(new charr::shared::NativeToUtf8());
                    if (raw_native->native_is_utf8())
                        raw_native.reset();
                }
                if (raw_native) {
                    for (R_len_t i=0; i<str_n; ++i) {
                        if (str_cont.isNA(i)) {
                            builder->set_na(i);
                            continue;
                        }

                        const charr::altrep_backend::io::ByteView& input = str_cont.get(i);
                        const shared::ByteView output =
                            raw_native->native(
                                input.data(), input.length()
                            );
                        ci__reject_embedded_nul(output.ptr, output.len);
                        ci::builder_set(
                            *builder, i, output.ptr, output.len,
                            cetype_ext_t::CE_ASCII_OR_UTF8
                        );
                    }
                }
                else {
                    // Open converters
                    StriUcnv ucnv1(
                        selected_from ? selected_from : "UTF-8",
                        STRI__DEFERRED_WARNINGS
                    );
                    StriUcnv ucnv2(
                        selected_to ? selected_to : "UTF-8",
                        STRI__DEFERRED_WARNINGS
                    );
                    UConverter* uconv_from = ucnv1.getConverter();
                    UConverter* uconv_to   = ucnv2.getConverter();
                    // Staging is only needed where the native encoding
                    // differs from the UTF-8 side ICU sees. When it is UTF-8
                    // the pass is an identity transcode.
                    std::unique_ptr<charr::shared::NativeToUtf8>
                        native_input;
                    if (!selected_from) {
                        native_input.reset(
                            new charr::shared::NativeToUtf8()
                        );
                        if (native_input->native_is_utf8())
                            native_input.reset();
                    }
                    std::unique_ptr<charr::shared::NativeToUtf8>
                        native_output;
                    if (!selected_to) {
                        native_output.reset(
                            new charr::shared::NativeToUtf8()
                        );
                        if (native_output->native_is_utf8())
                            native_output.reset();
                    }
                    // Deviation from stringi: converter callbacks queue their warnings
                    // until the converters and Reader-backed input have been released.

                    // Get target encoding mark
                    cetype_t encmark_to = to_raw_logical
                        ? CE_BYTES
                        : (selected_to ? ucnv2.getCE() : CE_NATIVE);
                    cetype_ext_t encmark_to2 =
                        ci__extended_encoding(encmark_to);
                    if (encmark_to2 == cetype_ext_t::CE_UTF8)
                        encmark_to2 = cetype_ext_t::CE_ASCII_OR_UTF8;

//               // estimate required buf size
//                size_t bufsize = 0;
//                for (R_len_t i=0; i<str_n; ++i) {
//                   if (!str_cont.isNA(i) && (size_t)str_cont.get(i).length() > bufsize)
//                      bufsize = str_cont.get(i).length();
//                }
//                bufsize = bufsize*4; // this is just an estimate (for 8bit->utf8 conversions)
//                String8buf buf(bufsize);
                    String8buf buf(0);

                    for (R_len_t i=0; i<str_n; ++i) {
                        if (str_cont.isNA(i)) {
                            if (!to_raw_logical)
                                builder->set_na(i);
                            continue;
                        }

                        const charr::altrep_backend::io::ByteView& input = str_cont.get(i);
                        const char* curs = input.data();
                        R_len_t curn     = input.length();
                        if (native_input) {
                            const shared::ByteView converted =
                                native_input->native(curs, curn);
                            curs = converted.ptr;
                            curn = converted.len;
                        }

                        const size_t bufneed = ci__transcode_direct(
                            uconv_from, uconv_to, curs, curn, buf
                        );
                        const char* output_data = buf.data();
                        size_t output_length = bufneed;
                        if (native_output) {
                            const shared::ByteView converted =
                                native_output->utf8_to_native(
                                    output_data,
                                    static_cast<int>(output_length)
                                );
                            output_data = converted.ptr;
                            output_length =
                                static_cast<size_t>(converted.len);
                        }

                        if (to_raw_logical) {
                            CiRawResult& output =
                                raw_outputs[static_cast<size_t>(i)];
                            output.is_na = false;
                            output.data.assign(
                                reinterpret_cast<const unsigned char*>(
                                    output_data
                                ),
                                reinterpret_cast<const unsigned char*>(
                                    output_data
                                )+output_length
                            );
                        }
                        else {
                            // Deviation from stringi: Builder accepts zero bytes, so
                            // preserve the copied character-output rejection locally.
                            ci__reject_embedded_nul(
                                output_data, output_length
                            );
                            ci::builder_set(
                                *builder, i, output_data, output_length,
                                encmark_to2
                            );
                        }
                    }
                }
            }
        }

        identity_borrow.reset();
        if (!to_raw_logical) {
            STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
                return builder->to_sexp();
            }));
        }
        else {
            // Deviation from stringi: stage data-dependent raw results until the
            // input and converter lifetimes have ended, then assemble the R list.
            STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
                return Rf_allocVector(VECSXP, str_n);
            }));
            for (R_len_t i=0; i<str_n; ++i) {
                const CiRawResult& output =
                    raw_outputs[static_cast<size_t>(i)];
                if (output.is_na)
                    continue;

                SEXP outobj;
                STRI__PROTECT(outobj = ci::unwind_protect([&]() -> SEXP {
                    return Rf_allocVector(
                        RAWSXP, static_cast<R_xlen_t>(output.data.size())
                    );
                }));
                if (!output.data.empty()) {
                    memcpy(
                        RAW(outobj), output.data.data(), output.data.size()
                    );
                }
                SET_VECTOR_ELT(ret, i, outobj);
                STRI__UNPROTECT(1);
            }
        }
    }

    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return ret;

    STRI__ERROR_HANDLER_END({/* no special action on error */})
}

} } // namespace charr::altrep_backend
