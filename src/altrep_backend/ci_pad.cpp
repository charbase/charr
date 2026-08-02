
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
#include "io/reader_utils.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "altrep_backend/io/utf8_output.h"

#include <cstdint>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <vector>

namespace charr { namespace altrep_backend {


namespace pad {

struct PadInput {
    const char* ptr;
    int len;
    bool ascii;
    bool missing;

    CHARR_NEUTRAL_HELPER PadInput& operator=(
        const PadInput& other
    ) noexcept = default;
};


CHARR_NEUTRAL_HELPER const char* empty_input() noexcept
{
    static const char value = '\0';
    return &value;
}


CHARR_NEUTRAL_HELPER PadInput missing_input() noexcept
{
    return PadInput{nullptr, NA_INTEGER, false, true};
}


CHARR_NEUTRAL_HELPER bool has_utf8_bom(
    const char* ptr, int len
) noexcept
{
    return len >= 3 &&
        static_cast<uint8_t>(ptr[0]) == UTF8_BOM_BYTE1 &&
        static_cast<uint8_t>(ptr[1]) == UTF8_BOM_BYTE2 &&
        static_cast<uint8_t>(ptr[2]) == UTF8_BOM_BYTE3;
}


CHARR_CXX_HELPER PadInput normalize_input(
    const charport::StrView& value,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage
)
{
    if (value.ptr == nullptr || value.len < 0)
        throw std::runtime_error("Reader returned an invalid string view");
    if (value.enc == CETYPE_EXT_ASCII)
        return PadInput{value.ptr, value.len, true, false};
    if (value.enc == CETYPE_EXT_BYTES)
        throw StriException(MSG__BYTESENC);

    const char* ptr = value.ptr;
    int len = value.len;
    bool ascii = false;
    bool strip_bom = false;
    bool converted_input = false;

    switch (value.enc.value) {
    case CETYPE_EXT_UTF8.value:
        strip_bom = has_utf8_bom(ptr, len);
        break;
    case CETYPE_EXT_ASCII_OR_UTF8.value:
        strip_bom = has_utf8_bom(ptr, len);
        ascii = !strip_bom && io::is_ascii(
            ptr, static_cast<std::size_t>(len)
        );
        break;
    case CETYPE_EXT_LATIN1.value:
    case CETYPE_EXT_NATIVE.value: {
        const bool native = value.enc == CETYPE_EXT_NATIVE;
        const bool native_bom = native && converter.native_is_utf8();
        const shared::ByteView converted = native
            ? converter.native(ptr, len)
            : converter.latin1(ptr, len);
        if (converted.len < 0 ||
                (converted.ptr == nullptr && converted.len > 0)) {
            throw std::runtime_error(
                "encoding conversion returned invalid bytes"
            );
        }
        ptr = converted.ptr == nullptr ? empty_input() : converted.ptr;
        len = converted.len;
        strip_bom = native_bom && has_utf8_bom(ptr, len);
        converted_input = true;
        break;
    }
    case CETYPE_EXT_NA.value:
        throw std::runtime_error("non-missing Reader record has NA encoding");
    default:
        throw std::runtime_error("Reader returned an unknown string encoding");
    }

    if (strip_bom) {
        ptr += 3;
        len -= 3;
    }
    if (converted_input && len > 0) {
        char* stable = storage.allocate(static_cast<std::size_t>(len));
        std::memcpy(stable, ptr, static_cast<std::size_t>(len));
        ptr = stable;
    }
    return PadInput{ptr, len, ascii, false};
}


CHARR_NEUTRAL_HELPER R_len_t recycling_length(
    R_len_t str_len, R_len_t width_len, R_len_t pad_len,
    bool& warning
) noexcept
{
    warning = false;
    if (str_len <= 0 || width_len <= 0 || pad_len <= 0)
        return 0;

    R_len_t result = str_len;
    if (width_len > result)
        result = width_len;
    if (pad_len > result)
        result = pad_len;
    warning = result % str_len != 0 ||
        result % width_len != 0 || result % pad_len != 0;
    return result;
}

// ASCII width is exact without an ICU property lookup, including C0 and DEL.
// Non-ASCII code points retain stringi's context-sensitive width rules.
CHARR_CXX_HELPER int ci__pad_width_string(const char* data, int length)
{
    int width = 0;
    UChar32 previous = 0;
    UChar32 current = 0;
    R_len_t i = 0;
    bool reset = true;

    while (i < length) {
        const unsigned char byte = static_cast<unsigned char>(data[i]);
        previous = current;
        if (byte < 0x80) {
            current = byte;
            ++i;
            if (reset)
                reset = false;
            if (byte >= 0x20 && byte != 0x7f)
                ++width;
        }
        else {
            U8_NEXT(data, i, length, current);
            if (current < 0)
                throw StriException(MSG__INVALID_UTF8);
            width += ci__width_char_with_context(current, previous, reset);
        }
    }

    return width;
}


CHARR_CXX_HELPER R_len_t count_code_points(const PadInput& value)
{
    return value.ascii
        ? value.len
        : ci__length_string(value.ptr, value.len);
}


CHARR_CXX_HELPER void validate_pad(
    const PadInput& value, bool use_length
)
{
    if (use_length) {
        R_len_t cursor = 0;
        UChar32 code_point = 0;
        U8_NEXT(value.ptr, cursor, value.len, code_point);
        if (code_point <= 0 || cursor < value.len)
            throw StriException(MSG__NOT_EQ_N_CODEPOINTS, "pad", 1);
    }
    else if (ci__pad_width_string(value.ptr, value.len) != 1) {
        throw StriException(MSG__NOT_EQ_N_WIDTH, "pad", 1);
    }
}


CHARR_NEUTRAL_HELPER char* ci__pad_repeat(
    char* output, const char* pattern, std::size_t pattern_length,
    R_len_t count
) noexcept
{
    if (count <= 0 || pattern_length == 0)
        return output;

    if (pattern_length == 1) {
        std::memset(output, static_cast<unsigned char>(pattern[0]), count);
        return output+count;
    }

    // Seed one copy, then double the initialized prefix instead of copying the
    // same multi-byte pad once per output code point.
    const std::size_t total = pattern_length*static_cast<std::size_t>(count);
    std::memcpy(output, pattern, pattern_length);
    std::size_t copied = pattern_length;
    while (copied < total) {
        const std::size_t remaining = total-copied;
        const std::size_t chunk = copied < remaining ? copied : remaining;
        std::memcpy(output+copied, output, chunk);
        copied += chunk;
    }
    return output+total;
}

} // namespace pad

using namespace pad;


/**
 * Pad a string
 *
 * vectorized over str, length and pad
 * if str or pad or length is NA the result will be NA
 *
 * @param str character vector
 * @param min_length integer vector
 * @param side [internal int]
 * @param pad character vector
 * @param use_length single logical value
 * @return character vector
 *
 * @version 0.1-?? (Bartlomiej Tartanus)
 *
 * @version 0.2-2 (Marek Gagolewski, 2014-04-20)
 *          use ci_error_handler, pad should be a single code point, not byte
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-04-22)
 *    `use_length` arg added,
 *    second argument renamed `width`
*/
CHARR_ENTRYPOINT SEXP ci_pad(
    SEXP str, SEXP width, SEXP side, SEXP pad, SEXP use_length
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    // this is an internal arg, check manually, error() allowed here
    if (!Rf_isInteger(side) || LENGTH(side) != 1)
        Rf_error(MSG__INCORRECT_INTERNAL_ARG);
    int _side = INTEGER(side)[0];
    if (_side < 0 || _side > 2)
        Rf_error(MSG__INCORRECT_INTERNAL_ARG);

    bool use_length_val = ci__prepare_arg_logical_1_notNA_r(
        use_length, "use_length"
    );
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    width = entry_protections.protect_one(
        ci__prepare_arg_integer_r(width, "width")
    );
    pad = entry_protections.protect_one(
        ci__prepare_arg_string_r(pad, "pad")
    );


    bool recycling_warning = false;

    try {
        charport::Reader str_reader;
        charport::Reader pad_reader;
        charport::StrViews str_views;
        charport::StrViews pad_views;
        io::OutputBuilder builder(0);
        shared::NativeToUtf8 str_converter;
        shared::NativeToUtf8 pad_converter;
        shared::SliceArena str_storage;
        shared::SliceArena pad_storage;
        std::vector<PadInput> str_inputs;
        std::vector<PadInput> pad_inputs;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t str_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                const R_len_t width_length = io::checked_r_len(
                    XLENGTH(width), "integer vectors"
                );
                const R_len_t pad_length = io::checked_r_len(
                    XLENGTH(pad), "character vectors"
                );
                const R_len_t vectorize_length = recycling_length(
                    str_length, width_length, pad_length,
                    recycling_warning
                );
                builder.reset(vectorize_length);

                const int* width_values = vectorize_length > 0
                    ? INTEGER(width)
                    : nullptr;
                if (vectorize_length > 0) {
                    str_reader.reset(str);
                    if (str_reader.size() != str_length) {
                        throw std::runtime_error(
                            "Reader length changed during pad"
                        );
                    }
                    str_views.resize(str_length);
                    str_reader.views(
                        0, str_length,
                        str_views.ptrs(), str_views.lengths(),
                        str_views.encodings()
                    );
                    str_inputs.resize(static_cast<std::size_t>(str_length));
                    for (R_len_t i = 0; i < str_length; ++i) {
                        const charport::StrView value = str_views[i];
                        str_inputs[static_cast<std::size_t>(i)] = value.is_na()
                            ? missing_input()
                            : normalize_input(
                                value, str_converter, str_storage
                            );
                    }

                    pad_reader.reset(pad);
                    if (pad_reader.size() != pad_length) {
                        throw std::runtime_error(
                            "Reader length changed during pad"
                        );
                    }
                    pad_views.resize(pad_length);
                    pad_reader.views(
                        0, pad_length,
                        pad_views.ptrs(), pad_views.lengths(),
                        pad_views.encodings()
                    );
                    pad_inputs.resize(static_cast<std::size_t>(pad_length));
                    for (R_len_t i = 0; i < pad_length; ++i) {
                        const charport::StrView value = pad_views[i];
                        pad_inputs[static_cast<std::size_t>(i)] = value.is_na()
                            ? missing_input()
                            : normalize_input(
                                value, pad_converter, pad_storage
                            );
                    }
                }

                const bool scalar_pad = pad_length == 1;
                bool scalar_pad_validated = false;

                for (R_len_t i = 0; i < vectorize_length; ++i) {
                    const PadInput& str_current = str_inputs[
                        static_cast<std::size_t>(i % str_length)
                    ];
                    const PadInput& pad_current = pad_inputs[
                        static_cast<std::size_t>(i % pad_length)
                    ];
                    const int width_value = width_values[i % width_length];
                    if (str_current.missing || pad_current.missing ||
                            width_value == NA_INTEGER) {
                        builder.set_na(i);
                        continue;
                    }

                    if (!scalar_pad || !scalar_pad_validated) {
                        validate_pad(pad_current, use_length_val);
                        if (scalar_pad)
                            scalar_pad_validated = true;
                    }

                    const R_len_t str_width = use_length_val
                        ? count_code_points(str_current)
                        : ci__pad_width_string(
                            str_current.ptr, str_current.len
                        );
                    if (str_width >= width_value) {
                        builder.set(
                            i, str_current.ptr,
                            static_cast<std::size_t>(str_current.len),
                            str_current.ascii
                                ? CETYPE_EXT_ASCII
                                : CETYPE_EXT_UTF8
                        );
                        continue;
                    }

                    const R_len_t pad_count = width_value - str_width;
                    const std::size_t pad_bytes =
                        static_cast<std::size_t>(pad_current.len);
                    if (pad_bytes > 0 &&
                            static_cast<std::size_t>(pad_count) >
                            (static_cast<std::size_t>(R_LEN_T_MAX) -
                                static_cast<std::size_t>(str_current.len)) /
                                pad_bytes) {
                        throw std::length_error(
                            "padded string exceeds R's string length limit"
                        );
                    }
                    const std::size_t output_length =
                        static_cast<std::size_t>(str_current.len) +
                        static_cast<std::size_t>(pad_count) * pad_bytes;
                    char* output = builder.reserve(
                        i, output_length,
                        str_current.ascii && pad_current.ascii
                            ? CETYPE_EXT_ASCII
                            : CETYPE_EXT_UTF8
                    );

                    R_len_t left_count = 0;
                    switch (_side) {
                    case 0: // left
                        left_count = pad_count;
                        break;
                    case 1: // right
                        break;
                    case 2: // both
                        left_count = pad_count / 2;
                        break;
                    }

                    output = ci__pad_repeat(
                        output, pad_current.ptr, pad_bytes,
                        left_count
                    );
                    if (str_current.len > 0) {
                        std::memcpy(
                            output, str_current.ptr,
                            static_cast<std::size_t>(str_current.len)
                        );
                        output += str_current.len;
                    }
                    ci__pad_repeat(
                        output, pad_current.ptr, pad_bytes,
                        pad_count - left_count
                    );
                }

                result = entry_protections.reprotect_one(
                    builder.to_sexp(), result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        if (recycling_warning)
            Rf_warning(MSG__WARN_RECYCLING_RULE);
    );
}


} } // namespace charr::altrep_backend
