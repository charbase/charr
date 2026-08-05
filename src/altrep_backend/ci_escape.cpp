
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


#include "ci_parallel.h"
#include "ci_stringi.h"
#include "io/reader_utils.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/parallel.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/string_view.h"
#include "../shared/unwind.h"
#include "altrep_backend/io/utf8_output.h"

#include <cstdint>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <vector>

namespace charr { namespace altrep_backend {


namespace escape {

struct EscapeInput {
    const char* ptr;
    int length;
};


CHARR_NEUTRAL_HELPER bool has_utf8_bom(
    const char* ptr, int length
) noexcept
{
    return length >= 3 &&
        static_cast<uint8_t>(ptr[0]) == UTF8_BOM_BYTE1 &&
        static_cast<uint8_t>(ptr[1]) == UTF8_BOM_BYTE2 &&
        static_cast<uint8_t>(ptr[2]) == UTF8_BOM_BYTE3;
}


CHARR_CXX_HELPER EscapeInput normalize_escape_input(
    const charport::StrView& value,
    charr::shared::NativeToUtf8& converter
)
{
    if (value.ptr == nullptr || value.len < 0)
        throw std::runtime_error("Reader returned an invalid string view");
    if (value.enc == CETYPE_EXT_ASCII)
        return EscapeInput{value.ptr, value.len};
    if (value.enc == CETYPE_EXT_BYTES)
        throw StriException(MSG__BYTESENC);

    const char* ptr = value.ptr;
    int length = value.len;
    bool strip_bom = value.enc == CETYPE_EXT_UTF8 ||
        value.enc == CETYPE_EXT_ASCII_OR_UTF8;

    if (value.enc == CETYPE_EXT_LATIN1 ||
            value.enc == CETYPE_EXT_NATIVE) {
        const bool native = value.enc == CETYPE_EXT_NATIVE;
        const shared::ByteView converted = native
            ? converter.native(ptr, length)
            : converter.latin1(ptr, length);
        strip_bom = native && has_utf8_bom(ptr, length);
        ptr = converted.ptr;
        length = converted.len;
    }
    else if (value.enc != CETYPE_EXT_UTF8 &&
            value.enc != CETYPE_EXT_ASCII_OR_UTF8) {
        throw std::runtime_error("Reader returned an unknown string encoding");
    }

    if (strip_bom && has_utf8_bom(ptr, length)) {
        ptr += 3;
        length -= 3;
    }
    return EscapeInput{ptr, length};
}


CHARR_NEUTRAL_HELPER char short_escape(UChar32 code_point) noexcept
{
    switch (code_point) {
    case 0x07: return 'a';
    case 0x08: return 'b';
    case 0x09: return 't';
    case 0x0a: return 'n';
    case 0x0b: return 'v';
    case 0x0c: return 'f';
    case 0x0d: return 'r';
    case 0x22: return '"';
    case 0x27: return '\'';
    case 0x5c: return '\\';
    default:   return '\0';
    }
}


CHARR_NEUTRAL_HELPER std::size_t escaped_width(
    UChar32 code_point
) noexcept
{
    if (short_escape(code_point) != '\0')
        return 2;
    if (code_point >= 32 && code_point <= 126)
        return 1;
    return code_point <= 0xffff ? 6 : 10;
}


CHARR_CXX_HELPER std::size_t escaped_size(const EscapeInput& input)
{
    std::size_t output_size = 0;
    int32_t cursor = 0;
    while (cursor < input.length) {
        UChar32 code_point;
        const uint8_t lead = static_cast<uint8_t>(input.ptr[cursor]);
        if (lead <= 0x7f) {
            code_point = lead;
            ++cursor;
        }
        else {
            U8_NEXT(input.ptr, cursor, input.length, code_point);
            if (code_point < 0)
                throw StriException(MSG__INVALID_UTF8);
        }

        const std::size_t width = escaped_width(code_point);
        if (output_size >
                static_cast<std::size_t>(R_LEN_T_MAX) - width) {
            throw std::length_error(
                "escaped string exceeds R's string length limit"
            );
        }
        output_size += width;
    }
    return output_size;
}


CHARR_NEUTRAL_HELPER char* write_hex(
    char* output, UChar32 value, int digits
) noexcept
{
    static const char hex[] = "0123456789abcdef";
    for (int shift = (digits-1)*4; shift >= 0; shift -= 4)
        *output++ = hex[(static_cast<uint32_t>(value) >> shift) & 0x0fU];
    return output;
}


CHARR_NEUTRAL_HELPER void write_escape(
    const EscapeInput& input, char* output
) noexcept
{
    int32_t cursor = 0;
    while (cursor < input.length) {
        UChar32 code_point;
        const uint8_t lead = static_cast<uint8_t>(input.ptr[cursor]);
        if (lead <= 0x7f) {
            code_point = lead;
            ++cursor;
        }
        else {
            U8_NEXT_UNSAFE(input.ptr, cursor, code_point);
        }

        const char escaped = short_escape(code_point);
        if (escaped != '\0') {
            *output++ = '\\';
            *output++ = escaped;
        }
        else if (code_point >= 32 && code_point <= 126) {
            *output++ = static_cast<char>(code_point);
        }
        else if (code_point <= 0xffff) {
            *output++ = '\\';
            *output++ = 'u';
            output = write_hex(output, code_point, 4);
        }
        else {
            *output++ = '\\';
            *output++ = 'U';
            output = write_hex(output, code_point, 8);
        }
    }
}


CHARR_NEUTRAL_HELPER std::size_t no_slot() noexcept
{
    return static_cast<std::size_t>(-1);
}


CHARR_CXX_HELPER shared::StringView stable_copy(
    const EscapeInput& input, shared::SliceArena& storage
)
{
    if (input.length <= 0)
        return shared::StringView{
            "", 0, shared::StringEncoding::utf8
        };
    char* output = storage.allocate(static_cast<std::size_t>(input.length));
    std::memcpy(output, input.ptr, static_cast<std::size_t>(input.length));
    return shared::StringView{
        output, input.length, shared::StringEncoding::utf8
    };
}


CHARR_CXX_HELPER EscapeInput direct_escape_input(
    const charport::StrView& value
)
{
    if (value.ptr == nullptr || value.len < 0)
        throw std::runtime_error("Reader returned an invalid string view");
    if (value.enc == CETYPE_EXT_ASCII)
        return EscapeInput{value.ptr, value.len};
    if (value.enc == CETYPE_EXT_BYTES)
        throw StriException(MSG__BYTESENC);
    if (value.enc != CETYPE_EXT_UTF8 &&
            value.enc != CETYPE_EXT_ASCII_OR_UTF8) {
        throw std::runtime_error("Reader returned an unknown string encoding");
    }

    const bool strip_bom = has_utf8_bom(value.ptr, value.len);
    return strip_bom
        ? EscapeInput{value.ptr+3, value.len-3}
        : EscapeInput{value.ptr, value.len};
}


/*
 * Prepare the records a worker may not prepare for itself. Latin-1 and
 * native input goes through R's converter, which stays on this thread, so
 * it is converted here and copied into storage that outlives the region.
 *
 * A record needs nothing else. The worker reaches every remaining check
 * itself through input_at() and escaped_size(), so sizing an ASCII or UTF-8
 * record here would run the whole sizing pass twice, once serially. The
 * encoding tags are still classified: that is one comparison per record and
 * it keeps a rejected encoding raising from the thread a serial run raises
 * it from.
 */
CHARR_CXX_HELPER void preflight_inputs(
    const charport::StrViews& values,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    std::vector<std::size_t>& converted_slots,
    std::vector<shared::StringView>& converted_values
)
{
    const R_xlen_t size = values.size();
    for (R_xlen_t i = 0; i < size; ++i) {
        const charport::StrView value = values[i];
        if (value.is_na())
            continue;
        if (value.ptr == nullptr || value.len < 0)
            throw std::runtime_error("Reader returned an invalid string view");

        switch (value.enc.value) {
        case CETYPE_EXT_ASCII.value:
        case CETYPE_EXT_UTF8.value:
        case CETYPE_EXT_ASCII_OR_UTF8.value:
            break;
        case CETYPE_EXT_BYTES.value:
            throw StriException(MSG__BYTESENC);
        case CETYPE_EXT_LATIN1.value:
        case CETYPE_EXT_NATIVE.value:
            if (converted_slots.empty()) {
                converted_slots.assign(
                    static_cast<std::size_t>(size), no_slot()
                );
            }
            converted_slots[static_cast<std::size_t>(i)] =
                converted_values.size();
            converted_values.push_back(stable_copy(
                normalize_escape_input(value, converter), storage
            ));
            break;
        default:
            throw std::runtime_error(
                "Reader returned an unknown string encoding"
            );
        }
    }
}


CHARR_CXX_HELPER EscapeInput input_at(
    const charport::StrViews& values,
    const std::vector<std::size_t>& converted_slots,
    const std::vector<shared::StringView>& converted_values,
    R_xlen_t index
)
{
    if (!converted_slots.empty()) {
        const std::size_t slot = converted_slots[
            static_cast<std::size_t>(index)
        ];
        if (slot != no_slot()) {
            const shared::StringView& value = converted_values[slot];
            return EscapeInput{value.ptr, value.len};
        }
    }
    return direct_escape_input(values[index]);
}


class Body final : public ParallelBody {
public:
    CHARR_CXX_HELPER Body(
        const charport::StrViews& values,
        const std::vector<std::size_t>& converted_slots,
        const std::vector<shared::StringView>& converted_values,
        io::ParallelOutputBuilder& builder
    ) noexcept
        : values_(values), converted_slots_(converted_slots),
          converted_values_(converted_values), builder_(builder)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        while (context.next_chunk()) {
            for (R_xlen_t i = context.begin; i < context.end; ++i) {
                const charport::StrView source = values_[i];
                if (source.is_na()) {
                    builder_.set_na(context.worker, i);
                    continue;
                }

                const EscapeInput input = input_at(
                    values_, converted_slots_, converted_values_, i
                );
                const std::size_t output_size = escaped_size(input);
                char* output = builder_.reserve(
                    context.worker, i, output_size, CETYPE_EXT_ASCII
                );
                if (output_size > 0)
                    write_escape(input, output);
            }
        }
    }

private:
    const charport::StrViews& values_;
    const std::vector<std::size_t>& converted_slots_;
    const std::vector<shared::StringView>& converted_values_;
    io::ParallelOutputBuilder& builder_;
};

} // namespace escape

using namespace escape;

/**
 *  Escape Unicode code points
 *
 *  @param str character vector
 *  @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-17)
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-01)
 *          fail on incorrect utf8 byte seqs;
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.1.6 (Steve Grubb, 2017-07-20)
 *          if ((char)c >= 32 || (char)c <= 126) should be &&
*/
CHARR_ENTRYPOINT SEXP ci_escape_unicode(SEXP str) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );

    try {
        charport::Reader reader;
        charport::StrViews values;
        io::OutputBuilder builder(0);
        io::ParallelOutputBuilder parallel_builder;
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        std::vector<std::size_t> converted_slots;
        std::vector<shared::StringView> converted_values;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                reader.reset(str);
                const R_xlen_t str_length = reader.size();
                values.resize(str_length);
                if (str_length > 0) {
                    reader.views(
                        0, str_length,
                        values.ptrs(), values.lengths(), values.encodings()
                    );
                }
                const shared::ParallelPlan plan = shared::parallel_plan(
                    true, str_length
                );

                if (plan.workers > 1) {
                    preflight_inputs(
                        values, converter, storage,
                        converted_slots, converted_values
                    );
                    parallel_builder.reset(str_length, plan.workers);
                    Body body(
                        values, converted_slots, converted_values,
                        parallel_builder
                    );
                    shared::run_parallel(plan, str_length, body);
                }
                else {
                    builder.reset(str_length);

                    for (R_xlen_t i = 0; i < str_length; ++i) {
                        const charport::StrView source = values[i];
                        if (source.is_na()) {
                            builder.set_na(i);
                            continue;
                        }

                        const EscapeInput input = normalize_escape_input(
                            source, converter
                        );
                        const std::size_t output_size = escaped_size(input);
                        char* output = builder.reserve(
                            i, output_size, CETYPE_EXT_ASCII
                        );
                        if (output_size > 0)
                            write_escape(input, output);
                    }
                }

                result = entry_protections.reprotect_one(
                    plan.workers > 1
                        ? parallel_builder.to_sexp()
                        : builder.to_sexp(),
                    result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}

} } // namespace charr::altrep_backend
