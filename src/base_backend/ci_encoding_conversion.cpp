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
#include "ci_string8buf.h"
#include "ci_ucnv.h"
#include "io/string_view.h"
#include "../shared/deferred_warnings.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/string_view.h"
#include "../shared/unwind.h"

#include <cstdint>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <vector>

namespace charr { namespace base_backend {

namespace encoding_conversion {

constexpr std::size_t maximum_buffer_length = 2147483647U;

enum class InputKind : unsigned char {
    null_value,
    raw_vector,
    raw_list,
    character
};

struct ByteRecord {
    const char* data;
    R_len_t length;
    bool missing;
};

CHARR_NEUTRAL_HELPER bool utf8_encoding_name(
    const char* encoding
) noexcept {
    return encoding != nullptr &&
        ucnv_compareNames(encoding, "UTF-8") == 0;
}

CHARR_NEUTRAL_HELPER bool valid_utf8_bytes(
    const char* data,
    R_len_t length,
    bool& ascii,
    bool& embedded_nul
) noexcept {
    ascii = true;
    embedded_nul = false;
    R_len_t i = 0;
    while (i < length) {
        const std::uint8_t byte = static_cast<std::uint8_t>(data[i]);
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

CHARR_R_HELPER bool plain_utf8_identity_r(
    SEXP input,
    bool respect_marks
) noexcept {
    const R_len_t size = LENGTH(input);
    const SEXP* values = size > 0 ? STRING_PTR_RO(input) : nullptr;
    for (R_len_t i = 0; i < size; ++i) {
        const SEXP value = values[i];
        if (value == NA_STRING || IS_ASCII(value))
            continue;
        if (respect_marks && !IS_UTF8(value))
            return false;

        bool ascii = false;
        bool embedded_nul = false;
        if (!valid_utf8_bytes(
                CHAR(value), LENGTH(value), ascii, embedded_nul
            ) || embedded_nul) {
            return false;
        }
    }
    return true;
}

CHARR_R_HELPER ByteRecord explicit_record_r(
    SEXP input,
    InputKind kind,
    R_len_t index
) noexcept {
    if (kind == InputKind::null_value)
        return ByteRecord{nullptr, 0, true};

    if (kind == InputKind::raw_vector) {
        return ByteRecord{
            reinterpret_cast<const char*>(RAW(input)),
            LENGTH(input), false
        };
    }

    if (kind == InputKind::raw_list) {
        const SEXP value = VECTOR_ELT(input, index);
        if (Rf_isNull(value))
            return ByteRecord{nullptr, 0, true};
        return ByteRecord{
            reinterpret_cast<const char*>(RAW(value)),
            LENGTH(value), false
        };
    }

    const SEXP value = STRING_ELT(input, index);
    if (value == NA_STRING)
        return ByteRecord{nullptr, 0, true};
    return ByteRecord{CHAR(value), LENGTH(value), false};
}

CHARR_R_HELPER void copy_identity_r(
    SEXP output,
    SEXP input
) noexcept {
    const R_len_t size = LENGTH(input);
    const SEXP* values = size > 0 ? STRING_PTR_RO(input) : nullptr;
    for (R_len_t i = 0; i < size; ++i) {
        const SEXP value = values[i];
        if (value == NA_STRING) {
            SET_STRING_ELT(output, i, NA_STRING);
        }
        else if (IS_ASCII(value) || IS_UTF8(value)) {
            SET_STRING_ELT(output, i, value);
        }
        else {
            SET_STRING_ELT(
                output, i,
                Rf_mkCharLenCE(CHAR(value), LENGTH(value), CE_UTF8)
            );
        }
    }
}

CHARR_CXX_HELPER void decode_marked(
    const shared::StringView& input,
    shared::NativeToUtf8& converter,
    icu::UnicodeString& output
) {
    if (input.is_na()) {
        output.setToBogus();
        return;
    }
    if (input.len < 0 || (input.ptr == nullptr && input.len != 0))
        throw std::runtime_error("invalid marked string view");

    const char* data = input.ptr == nullptr ? "" : input.ptr;
    R_len_t length = input.len;
    switch (input.enc) {
    case shared::StringEncoding::ascii:
    case shared::StringEncoding::utf8:
    case shared::StringEncoding::ascii_or_utf8:
        break;
    case shared::StringEncoding::latin1: {
        const shared::ByteView converted = converter.latin1(data, length);
        data = converted.ptr;
        length = converted.len;
        break;
    }
    case shared::StringEncoding::native: {
        const shared::ByteView converted = converter.native(data, length);
        data = converted.ptr;
        length = converted.len;
        break;
    }
    case shared::StringEncoding::bytes:
        throw StriException(MSG__BYTESENC);
    case shared::StringEncoding::missing:
        output.setToBogus();
        return;
    case shared::StringEncoding::unknown:
        throw std::runtime_error("unknown marked string encoding");
    }

    output.setTo(icu::UnicodeString::fromUTF8(
        icu::StringPiece(data == nullptr ? "" : data, length)
    ));
}

CHARR_CXX_HELPER std::size_t transcode_utf16(
    UConverter* target_converter,
    const icu::UnicodeString& input,
    String8buf& output
) {
    const UChar* data = input.getBuffer();
    const R_len_t length = input.length();
    if (data == nullptr)
        throw StriException(MSG__INTERNAL_ERROR);

    std::size_t capacity = UCNV_GET_MAX_BYTES_FOR_STRING(
        static_cast<std::size_t>(length),
        ucnv_getMaxCharSize(target_converter)
    );
    if (capacity > maximum_buffer_length)
        capacity = maximum_buffer_length;
    output.resize(capacity, false);

    UErrorCode status = U_ZERO_ERROR;
    ucnv_resetFromUnicode(target_converter);
    std::size_t required = static_cast<std::size_t>(ucnv_fromUChars(
        target_converter, output.data(), output.size(),
        data, length, &status
    ));
    if (required <= output.size()) {
        STRI__CHECKICUSTATUS_THROW(status, {})
        return required;
    }
    if (required > maximum_buffer_length)
        throw StriException(MSG__BUF_SIZE_EXCEEDED);

    output.resize(required, false);
    status = U_ZERO_ERROR;
    ucnv_resetFromUnicode(target_converter);
    required = static_cast<std::size_t>(ucnv_fromUChars(
        target_converter, output.data(), output.size(),
        data, length, &status
    ));
    STRI__CHECKICUSTATUS_THROW(status, {})
    return required;
}

CHARR_CXX_HELPER std::size_t transcode_direct(
    UConverter* source_converter,
    UConverter* target_converter,
    const char* input,
    R_len_t input_length,
    String8buf& output
) {
    const std::size_t maximum = maximum_buffer_length;
    const std::size_t max_char_size = static_cast<std::size_t>(
        ucnv_getMaxCharSize(target_converter)
    );
    std::size_t capacity = 64;
    if (input_length > 0) {
        const std::size_t input_size = static_cast<std::size_t>(input_length);
        capacity = input_size > (maximum-1)/max_char_size
            ? maximum-1
            : input_size*max_char_size;
        if (capacity < 64)
            capacity = 64;
    }
    output.resize(capacity-1, false);

    const char empty[] = "";
    const char* source = input_length == 0 && input == nullptr
        ? empty
        : input;
    const char* source_limit = source+input_length;
    UChar pivot[1024];
    UChar* pivot_source = pivot;
    UChar* pivot_target = pivot;
    bool reset = true;
    std::size_t used = 0;

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
        used = static_cast<std::size_t>(target-output.data());
        if (status != U_BUFFER_OVERFLOW_ERROR) {
            STRI__CHECKICUSTATUS_THROW(status, {})
            return used;
        }

        const std::size_t old_capacity = output.size();
        if (old_capacity >= maximum)
            throw StriException(MSG__BUF_SIZE_EXCEEDED);
        const std::size_t new_capacity = old_capacity > maximum/2
            ? maximum
            : old_capacity*2;
        output.resize(new_capacity-1, true);
        reset = false;
    }
}

CHARR_R_HELPER void install_output_r(
    SEXP output,
    R_len_t index,
    bool raw_output,
    const char* data,
    std::size_t length,
    cetype_t encoding,
    shared::ProtHelper& protections
) noexcept {
    if (raw_output) {
        SEXP value = protections.protect_one(Rf_allocVector(
            RAWSXP, static_cast<R_xlen_t>(length)
        ));
        if (length > 0)
            std::memcpy(RAW(value), data, length);
        SET_VECTOR_ELT(output, index, value);
        protections.release(1);
        return;
    }

    SET_STRING_ELT(
        output, index,
        Rf_mkCharLenCE(data, static_cast<int>(length), encoding)
    );
}

CHARR_CXX_HELPER void release_conversion_state(
    StriUcnv& source,
    StriUcnv& target,
    shared::NativeToUtf8& native
) {
    source = StriUcnv();
    target = StriUcnv();
    native.reset();
}

} // namespace encoding_conversion

using namespace encoding_conversion;

CHARR_ENTRYPOINT SEXP ci_encode(
    SEXP str,
    SEXP from,
    SEXP to,
    SEXP to_raw
) noexcept {
    CHARR_ENTRYPOINT_BEGIN();

    const char* selected_from = ci__prepare_arg_enc_r(
        from, "from", true
    );
    const bool marked_input = selected_from == nullptr &&
        Rf_isVectorAtomic(str) && !isRaw(str);

    PROTECT_INDEX str_index;
    entry_protections.protect_with_index(str, &str_index);

    const char* selected_to = nullptr;
    bool raw_output = false;
    if (marked_input) {
        SEXP prepared = ci__prepare_arg_string_r(str, "str");
        str = entry_protections.reprotect_one(prepared, str_index);
        selected_to = ci__prepare_arg_enc_r(to, "to", true);
        raw_output = ci__prepare_arg_logical_1_notNA_r(
            to_raw, "to_raw"
        );
    }
    else {
        selected_to = ci__prepare_arg_enc_r(to, "to", true);
        raw_output = ci__prepare_arg_logical_1_notNA_r(
            to_raw, "to_raw"
        );
        SEXP prepared = ci__prepare_arg_list_raw_r(str, "str");
        str = entry_protections.reprotect_one(prepared, str_index);
    }

    InputKind input_kind = InputKind::character;
    R_len_t input_size = LENGTH(str);
    if (!marked_input) {
        if (Rf_isNull(str)) {
            input_kind = InputKind::null_value;
            input_size = 1;
        }
        else if (isRaw(str)) {
            input_kind = InputKind::raw_vector;
            input_size = 1;
        }
        else if (Rf_isVectorList(str)) {
            input_kind = InputKind::raw_list;
        }
    }

    try {
        shared::DeferredWarnings warnings;
        StriUcnv source_converter(
            selected_from == nullptr ? "UTF-8" : selected_from,
            warnings
        );
        StriUcnv target_converter(
            selected_to == nullptr ? "UTF-8" : selected_to,
            warnings
        );
        shared::NativeToUtf8 native_converter;
        String8buf buffer(0);
        std::vector<ByteRecord> explicit_records(
            marked_input ? 0U : static_cast<std::size_t>(input_size)
        );
        std::vector<icu::UnicodeString> marked_records(
            marked_input ? static_cast<std::size_t>(input_size) : 0U
        );
        std::exception_ptr pending_error;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                if (!raw_output && TYPEOF(str) == STRSXP &&
                        utf8_encoding_name(selected_to) &&
                        (marked_input || utf8_encoding_name(selected_from)) &&
                        plain_utf8_identity_r(str, marked_input)) {
                    result = entry_protections.reprotect_one(
                        Rf_allocVector(STRSXP, input_size), result_index
                    );
                    copy_identity_r(result, str);
                }
                else if (input_size <= 0) {
                    result = entry_protections.reprotect_one(
                        Rf_allocVector(
                            raw_output ? VECSXP : STRSXP, 0
                        ),
                        result_index
                    );
                }
                else {
                    try {
                        if (marked_input) {
                            for (R_len_t i = 0; i < input_size; ++i) {
                                decode_marked(
                                    io::as_shared_view(STRING_ELT(str, i)),
                                    native_converter,
                                    marked_records[
                                        static_cast<std::size_t>(i)
                                    ]
                                );
                            }
                        }
                        else {
                            for (R_len_t i = 0; i < input_size; ++i) {
                                explicit_records[
                                    static_cast<std::size_t>(i)
                                ] = explicit_record_r(str, input_kind, i);
                            }
                        }

                        UConverter* source_handle = nullptr;
                        if (!marked_input) {
                            source_handle =
                                source_converter.getConverter(true);
                        }
                        UConverter* target_handle =
                            target_converter.getConverter(true);
                        const cetype_t target_mark = raw_output
                            ? CE_BYTES
                            : (selected_to == nullptr
                                ? CE_NATIVE
                                : target_converter.getCE());

                        bool native_input = false;
                        bool native_output = false;
                        if ((!marked_input && selected_from == nullptr) ||
                                selected_to == nullptr) {
                            const bool native_is_utf8 =
                                native_converter.native_is_utf8();
                            native_input = !marked_input &&
                                selected_from == nullptr && !native_is_utf8;
                            native_output = selected_to == nullptr &&
                                !native_is_utf8;
                        }

                        result = entry_protections.reprotect_one(
                            Rf_allocVector(
                                raw_output ? VECSXP : STRSXP, input_size
                            ),
                            result_index
                        );

                        for (R_len_t i = 0; i < input_size; ++i) {
                            if (marked_input) {
                                const icu::UnicodeString& input =
                                    marked_records[
                                        static_cast<std::size_t>(i)
                                    ];
                                if (input.isBogus()) {
                                    if (!raw_output) {
                                        SET_STRING_ELT(
                                            result, i, NA_STRING
                                        );
                                    }
                                    continue;
                                }

                                const std::size_t converted_size =
                                    transcode_utf16(
                                        target_handle, input, buffer
                                    );
                                const char* output_data = buffer.data();
                                std::size_t output_size = converted_size;
                                if (native_output) {
                                    const shared::ByteView converted =
                                        native_converter.utf8_to_native(
                                            output_data,
                                            static_cast<int>(output_size)
                                        );
                                    output_data = converted.ptr;
                                    output_size = static_cast<std::size_t>(
                                        converted.len
                                    );
                                }
                                install_output_r(
                                    result, i, raw_output,
                                    output_data, output_size, target_mark,
                                    callback_protections
                                );
                                continue;
                            }

                            const ByteRecord& input = explicit_records[
                                static_cast<std::size_t>(i)
                            ];
                            if (input.missing) {
                                if (!raw_output) {
                                    SET_STRING_ELT(result, i, NA_STRING);
                                }
                                continue;
                            }

                            const char* input_data = input.data;
                            R_len_t input_length = input.length;
                            if (native_input) {
                                const shared::ByteView converted =
                                    native_converter.native(
                                        input_data, input_length
                                    );
                                input_data = converted.ptr;
                                input_length = converted.len;
                            }

                            const std::size_t converted_size =
                                transcode_direct(
                                    source_handle, target_handle,
                                    input_data, input_length, buffer
                                );
                            const char* output_data = buffer.data();
                            std::size_t output_size = converted_size;
                            if (native_output) {
                                const shared::ByteView converted =
                                    native_converter.utf8_to_native(
                                        output_data,
                                        static_cast<int>(output_size)
                                    );
                                output_data = converted.ptr;
                                output_size = static_cast<std::size_t>(
                                    converted.len
                                );
                            }
                            install_output_r(
                                result, i, raw_output, output_data,
                                output_size, target_mark,
                                callback_protections
                            );
                        }
                    }
                    catch (...) {
                        pending_error = std::current_exception();
                    }

                    release_conversion_state(
                        source_converter, target_converter, native_converter
                    );
                    warnings.emit_r();
                    if (pending_error)
                        std::rethrow_exception(pending_error);
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}

} } // namespace charr::base_backend
