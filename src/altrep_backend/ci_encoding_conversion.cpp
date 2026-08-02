
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
#include "io/utf8_output.h"
#include "../shared/deferred_warnings.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/string_view.h"
#include "../shared/unwind.h"

#include <charport.h>

#include <cstdint>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <vector>

namespace charr { namespace altrep_backend {

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

struct CHARR_OWNER_TYPE RawResult {
    bool missing;
    std::vector<unsigned char> data;

    CHARR_CXX_HELPER RawResult() noexcept
        : missing(true), data() {}
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

CHARR_CXX_HELPER bool validate_identity_views(
    const charport::StrViews& input,
    bool respect_marks,
    std::vector<cetype_ext_t>& encodings
) {
    const R_xlen_t size = input.size();
    for (R_xlen_t i = 0; i < size; ++i) {
        const charport::StrView value = input[i];
        if (value.is_na()) {
            encodings[static_cast<std::size_t>(i)] = CETYPE_EXT_NA;
            continue;
        }
        if (value.len < 0 || (value.ptr == nullptr && value.len != 0))
            throw std::runtime_error("Reader returned an invalid string view");
        if (respect_marks &&
                value.enc != CETYPE_EXT_ASCII &&
                value.enc != CETYPE_EXT_UTF8 &&
                value.enc != CETYPE_EXT_ASCII_OR_UTF8) {
            return false;
        }
        if (value.enc == CETYPE_EXT_ASCII) {
            encodings[static_cast<std::size_t>(i)] = CETYPE_EXT_ASCII;
            continue;
        }

        bool ascii = false;
        bool embedded_nul = false;
        const char* data = value.ptr == nullptr ? "" : value.ptr;
        if (!valid_utf8_bytes(
                data, value.len, ascii, embedded_nul
            ) || embedded_nul) {
            return false;
        }
        encodings[static_cast<std::size_t>(i)] = ascii
            ? CETYPE_EXT_ASCII
            : CETYPE_EXT_UTF8;
    }
    return true;
}

CHARR_CXX_HELPER void copy_identity_views(
    const charport::StrViews& input,
    const std::vector<cetype_ext_t>& encodings,
    io::OutputBuilder& output
) {
    const R_xlen_t size = input.size();
    output.reset(size);
    for (R_xlen_t i = 0; i < size; ++i) {
        const cetype_ext_t encoding = encodings[
            static_cast<std::size_t>(i)
        ];
        if (encoding == CETYPE_EXT_NA) {
            output.set_na(i);
            continue;
        }
        const charport::StrView value = input[i];
        output.set(
            i, value.ptr, static_cast<std::size_t>(value.len), encoding
        );
    }
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

    const SEXP value = VECTOR_ELT(input, index);
    if (Rf_isNull(value))
        return ByteRecord{nullptr, 0, true};
    return ByteRecord{
        reinterpret_cast<const char*>(RAW(value)),
        LENGTH(value), false
    };
}

CHARR_CXX_HELPER ByteRecord reader_record(
    const charport::StrView& value
) {
    if (value.is_na())
        return ByteRecord{nullptr, 0, true};
    if (value.len < 0 || (value.ptr == nullptr && value.len != 0))
        throw std::runtime_error("Reader returned an invalid string view");
    return ByteRecord{value.ptr, value.len, false};
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

CHARR_NEUTRAL_HELPER cetype_ext_t extended_encoding(
    cetype_t encoding
) noexcept {
    switch (encoding) {
    case CE_UTF8:
        return CETYPE_EXT_UTF8;
    case CE_LATIN1:
        return CETYPE_EXT_LATIN1;
    case CE_BYTES:
        return CETYPE_EXT_BYTES;
    default:
        return CETYPE_EXT_NATIVE;
    }
}

CHARR_CXX_HELPER void reject_embedded_nul(
    const char* data,
    std::size_t length
) {
    if (length > 0 && std::memchr(data, 0, length) != nullptr)
        throw StriException("embedded nul in string");
}

CHARR_CXX_HELPER void store_converted(
    R_len_t index,
    bool raw_output,
    const char* data,
    std::size_t length,
    cetype_ext_t encoding,
    std::vector<RawResult>& raw_results,
    io::OutputBuilder& output
) {
    if (raw_output) {
        RawResult& value = raw_results[static_cast<std::size_t>(index)];
        value.missing = false;
        value.data.assign(
            reinterpret_cast<const unsigned char*>(data),
            reinterpret_cast<const unsigned char*>(data)+length
        );
        return;
    }

    reject_embedded_nul(data, length);
    if (length > maximum_buffer_length)
        throw std::length_error("character output exceeds R's string length limit");
    if (data == nullptr) {
        if (length != 0)
            throw std::invalid_argument("null character output has nonzero length");
        data = "";
    }
    output.set_validated(
        index,
        make_strview(data, static_cast<int>(length), encoding)
    );
}

CHARR_CXX_HELPER void release_conversion_state(
    charport::Reader& reader,
    StriUcnv& source,
    StriUcnv& target,
    shared::NativeToUtf8& native
) {
    reader = charport::Reader();
    source = StriUcnv();
    target = StriUcnv();
    native.reset();
}

CHARR_R_HELPER SEXP assemble_raw_output_r(
    const std::vector<RawResult>& values,
    shared::ProtHelper& protections
) noexcept {
    const R_xlen_t size = static_cast<R_xlen_t>(values.size());
    SEXP output = Rf_allocVector(VECSXP, size);
    for (R_xlen_t i = 0; i < size; ++i) {
        const std::size_t index = static_cast<std::size_t>(i);
        if (values[index].missing)
            continue;

        SEXP raw = protections.protect_one(Rf_allocVector(
            RAWSXP, static_cast<R_xlen_t>(values[index].data.size())
        ));
        if (!values[index].data.empty()) {
            std::memcpy(
                RAW(raw), values[index].data.data(),
                values[index].data.size()
            );
        }
        SET_VECTOR_ELT(output, i, raw);
        protections.release(1);
    }
    return output;
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
    const bool character_input = input_kind == InputKind::character;
    const bool identity_candidate = !raw_output && character_input &&
        utf8_encoding_name(selected_to) &&
        (marked_input || utf8_encoding_name(selected_from));

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
        charport::Reader reader;
        charport::StrViews character_views(
            character_input ? input_size : 0
        );
        std::vector<cetype_ext_t> identity_encodings(
            identity_candidate ? static_cast<std::size_t>(input_size) : 0U
        );
        std::vector<ByteRecord> explicit_records(
            !marked_input && !character_input
                ? static_cast<std::size_t>(input_size)
                : 0U
        );
        std::vector<icu::UnicodeString> marked_records(
            marked_input ? static_cast<std::size_t>(input_size) : 0U
        );
        std::vector<RawResult> raw_results(
            raw_output ? static_cast<std::size_t>(input_size) : 0U
        );
        io::OutputBuilder output(0);
        std::exception_ptr pending_error;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                try {
                    if (character_input) {
                        reader.reset(str);
                        if (reader.size() != input_size) {
                            throw std::runtime_error(
                                "Reader length changed during encoding conversion"
                            );
                        }
                        if (input_size > 0) {
                            reader.views(
                                0, input_size,
                                character_views.ptrs(),
                                character_views.lengths(),
                                character_views.encodings()
                            );
                        }
                    }

                    if (identity_candidate &&
                            validate_identity_views(
                                character_views, marked_input,
                                identity_encodings
                            )) {
                        copy_identity_views(
                            character_views, identity_encodings, output
                        );
                    }
                    else if (input_size <= 0) {
                        if (!raw_output)
                            output.reset(0);
                    }
                    else {
                        if (marked_input) {
                            for (R_len_t i = 0; i < input_size; ++i) {
                                decode_marked(
                                    io::as_shared_view(character_views[i]),
                                    native_converter,
                                    marked_records[
                                        static_cast<std::size_t>(i)
                                    ]
                                );
                            }
                        }
                        else if (!character_input) {
                            for (R_len_t i = 0; i < input_size; ++i) {
                                explicit_records[
                                    static_cast<std::size_t>(i)
                                ] = explicit_record_r(str, input_kind, i);
                            }
                        }

                        UConverter* source_handle = nullptr;
                        if (!marked_input) {
                            source_handle = source_converter.getConverter();
                        }
                        UConverter* target_handle =
                            target_converter.getConverter();
                        cetype_ext_t target_mark = extended_encoding(
                            raw_output
                                ? CE_BYTES
                                : (selected_to == nullptr
                                    ? CE_NATIVE
                                    : target_converter.getCE())
                        );
                        if (target_mark == CETYPE_EXT_UTF8)
                            target_mark = CETYPE_EXT_ASCII_OR_UTF8;

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

                        if (!raw_output)
                            output.reset(input_size);

                        for (R_len_t i = 0; i < input_size; ++i) {
                            if (marked_input) {
                                const icu::UnicodeString& input =
                                    marked_records[
                                        static_cast<std::size_t>(i)
                                    ];
                                if (input.isBogus()) {
                                    if (!raw_output)
                                        output.set_na(i);
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
                                store_converted(
                                    i, raw_output, output_data, output_size,
                                    target_mark, raw_results, output
                                );
                                continue;
                            }

                            const ByteRecord input = character_input
                                ? reader_record(character_views[i])
                                : explicit_records[
                                    static_cast<std::size_t>(i)
                                ];
                            if (input.missing) {
                                if (!raw_output)
                                    output.set_na(i);
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
                            store_converted(
                                i, raw_output, output_data, output_size,
                                target_mark, raw_results, output
                            );
                        }
                    }
                }
                catch (...) {
                    pending_error = std::current_exception();
                }

                release_conversion_state(
                    reader, source_converter,
                    target_converter, native_converter
                );
                warnings.emit_r();
                if (pending_error)
                    std::rethrow_exception(pending_error);

                if (raw_output) {
                    result = entry_protections.reprotect_one(
                        assemble_raw_output_r(
                            raw_results, callback_protections
                        ),
                        result_index
                    );
                }
                else {
                    result = entry_protections.reprotect_one(
                        output.to_sexp(), result_index
                    );
                }
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}

} } // namespace charr::altrep_backend
