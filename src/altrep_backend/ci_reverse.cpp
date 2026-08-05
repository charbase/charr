
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
#include "ci_parallel.h"
#include "io/reader_utils.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "altrep_backend/io/utf8_output.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace charr { namespace altrep_backend {


namespace reverse {

struct ReverseInput {
    const char* ptr;
    int len;
    bool ascii;
};

CHARR_NEUTRAL_HELPER bool has_utf8_bom(
    const char* ptr, int len
) noexcept
{
    return len >= 3 &&
        static_cast<uint8_t>(ptr[0]) == UTF8_BOM_BYTE1 &&
        static_cast<uint8_t>(ptr[1]) == UTF8_BOM_BYTE2 &&
        static_cast<uint8_t>(ptr[2]) == UTF8_BOM_BYTE3;
}

CHARR_CXX_HELPER ReverseInput normalize_input(
    const charport::StrView& value,
    charr::shared::NativeToUtf8& converter
)
{
    if (value.enc == CETYPE_EXT_ASCII)
        return ReverseInput{value.ptr, value.len, true};
    if (value.enc == CETYPE_EXT_BYTES)
        throw StriException(MSG__BYTESENC);

    const char* ptr = value.ptr;
    int len = value.len;
    bool ambiguous = value.enc == CETYPE_EXT_ASCII_OR_UTF8;
    if (value.enc == CETYPE_EXT_LATIN1 ||
            value.enc == CETYPE_EXT_NATIVE) {
        const shared::ByteView converted =
            value.enc == CETYPE_EXT_LATIN1
            ? converter.latin1(ptr, len)
            : converter.native(ptr, len);
        ptr = converted.ptr;
        len = converted.len;
        // Conversion establishes valid UTF-8, but not whether the payload is
        // ASCII. Resolve that mark before reserving native charvec storage.
        ambiguous = true;
    }
    else if (value.enc != CETYPE_EXT_UTF8 && !ambiguous) {
        throw StriException("unknown charport string encoding");
    }

    if (has_utf8_bom(ptr, len)) {
        ptr += 3;
        len -= 3;
        // Removing the BOM may leave an otherwise ASCII payload.
        ambiguous = true;
    }
    return ReverseInput{
        ptr, len, ambiguous && io::is_ascii(ptr, static_cast<std::size_t>(len))
    };
}

CHARR_CXX_HELPER void reverse_utf8(
    const ReverseInput& value, char* output
)
{
    int32_t source_index = value.len;
    int32_t output_index = 0;
    while (source_index > 0) {
        const uint8_t last_byte = static_cast<uint8_t>(
            value.ptr[source_index - 1]
        );
        if (last_byte < 0x80) {
            output[output_index++] = static_cast<char>(last_byte);
            --source_index;
            continue;
        }

        const int32_t code_point_end = source_index;
        UChar32 code_point;
        U8_PREV(value.ptr, 0, source_index, code_point);
        if (code_point < 0)
            throw StriException(MSG__INVALID_UTF8);

        // U8_PREV has validated this sequence. Copying its original bytes is
        // cheaper than reconstructing the same UTF-8 code point.
        const int32_t code_point_width = code_point_end - source_index;
        switch (code_point_width) {
        case 4:
            output[output_index + 3] = value.ptr[source_index + 3];
            [[fallthrough]];
        case 3:
            output[output_index + 2] = value.ptr[source_index + 2];
            [[fallthrough]];
        case 2:
            output[output_index + 1] = value.ptr[source_index + 1];
            [[fallthrough]];
        default:
            output[output_index] = value.ptr[source_index];
        }
        output_index += code_point_width;
    }
}

CHARR_CXX_HELPER void reverse_input(
    const ReverseInput& value, char* output
)
{
    if (value.ascii) {
        std::reverse_copy(
            value.ptr, value.ptr + value.len, output
        );
    }
    else {
        reverse_utf8(value, output);
    }
}


CHARR_NEUTRAL_HELPER std::size_t no_slot() noexcept
{
    return static_cast<std::size_t>(-1);
}


CHARR_CXX_HELPER ReverseInput stable_copy(
    const ReverseInput& input, shared::SliceArena& storage
)
{
    if (input.len <= 0)
        return ReverseInput{"", 0, input.ascii};
    char* stable = storage.allocate(static_cast<std::size_t>(input.len));
    std::memcpy(stable, input.ptr, static_cast<std::size_t>(input.len));
    return ReverseInput{stable, input.len, input.ascii};
}


CHARR_CXX_HELPER ReverseInput direct_input(
    const charport::StrView& value
)
{
    if (value.enc == CETYPE_EXT_ASCII)
        return ReverseInput{value.ptr, value.len, true};
    if (value.enc == CETYPE_EXT_BYTES)
        throw StriException(MSG__BYTESENC);
    if (value.enc != CETYPE_EXT_UTF8 &&
            value.enc != CETYPE_EXT_ASCII_OR_UTF8) {
        throw StriException("unknown charport string encoding");
    }

    const bool ambiguous = value.enc == CETYPE_EXT_ASCII_OR_UTF8;
    const bool strip_bom = has_utf8_bom(value.ptr, value.len);
    const char* ptr = strip_bom ? value.ptr+3 : value.ptr;
    const int len = strip_bom ? value.len-3 : value.len;
    return ReverseInput{
        ptr, len,
        (ambiguous || strip_bom) &&
            io::is_ascii(ptr, static_cast<std::size_t>(len))
    };
}


/**
 * Prepare the records a worker may not prepare for itself. R's converter
 * stays on this thread, so a latin1 or native record is converted here and
 * copied into storage that outlives the region.
 *
 * A record needs nothing else. `reverse_utf8()` walks the payload with
 * `U8_PREV` and raises on a negative code point, so it validates the same
 * bytes the worker is about to read; validating here as well would run the
 * whole pass twice, once serially. The encoding tags are still classified:
 * that is one comparison per record and it keeps a rejected encoding raising
 * from the pass a serial run raises it from.
 */
CHARR_CXX_HELPER void preflight_inputs(
    const charport::StrViews& values,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    std::vector<std::size_t>& converted_slots,
    std::vector<ReverseInput>& converted_values
)
{
    const R_xlen_t size = values.size();
    for (R_xlen_t i = 0; i < size; ++i) {
        const charport::StrView source = values[i];
        if (source.is_na())
            continue;

        switch (source.enc.value) {
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
            converted_values.push_back(
                stable_copy(normalize_input(source, converter), storage)
            );
            break;
        default:
            throw StriException("unknown charport string encoding");
        }
    }
}


CHARR_CXX_HELPER ReverseInput input_at(
    const charport::StrViews& values,
    const std::vector<std::size_t>& converted_slots,
    const std::vector<ReverseInput>& converted_values,
    R_xlen_t index
)
{
    if (!converted_slots.empty()) {
        const std::size_t slot = converted_slots[
            static_cast<std::size_t>(index)
        ];
        if (slot != no_slot())
            return converted_values[slot];
    }
    return direct_input(values[index]);
}


class Body final : public ParallelBody {
public:
    CHARR_CXX_HELPER Body(
        const charport::StrViews& values,
        const std::vector<std::size_t>& converted_slots,
        const std::vector<ReverseInput>& converted_values,
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
                if (values_[i].is_na()) {
                    builder_.set_na(context.worker, i);
                    continue;
                }
                const ReverseInput value = input_at(
                    values_, converted_slots_, converted_values_, i
                );
                char* output = builder_.reserve(
                    context.worker, i, static_cast<std::size_t>(value.len),
                    value.ascii ? CETYPE_EXT_ASCII : CETYPE_EXT_UTF8
                );
                reverse_input(value, output);
            }
        }
    }

private:
    const charport::StrViews& values_;
    const std::vector<std::size_t>& converted_slots_;
    const std::vector<ReverseInput>& converted_values_;
    io::ParallelOutputBuilder& builder_;
};

} // namespace reverse

using namespace reverse;


/**
 * Reverse Each String
 * @param str character vector
 * @return character vector with every string reversed
 *
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          use io::Utf16Input
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-01)
 *          detect incorrect utf8 byte stream
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
CHARR_ENTRYPOINT SEXP ci_reverse(SEXP str) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();


    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    const R_xlen_t str_len = XLENGTH(str);
    const shared::ParallelPlan plan = shared::parallel_plan(true, str_len);

    try {
        charport::Reader reader;
        charport::StrViews values;
        io::OutputBuilder builder(0);
        io::ParallelOutputBuilder parallel_builder;
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        std::vector<std::size_t> converted_slots;
        std::vector<ReverseInput> converted_values;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                reader.reset(str);
                if (reader.size() != str_len) {
                    throw std::runtime_error(
                        "Reader length changed during string reversal"
                    );
                }
                values.resize(str_len);
                if (str_len > 0) {
                    reader.views(
                        0, str_len,
                        values.ptrs(), values.lengths(), values.encodings()
                    );
                }
                if (plan.workers > 1) {
                    preflight_inputs(
                        values, converter, storage,
                        converted_slots, converted_values
                    );
                    parallel_builder.reset(str_len, plan.workers);
                    Body body(
                        values, converted_slots, converted_values,
                        parallel_builder
                    );
                    shared::run_parallel(plan, str_len, body);
                    result = entry_protections.reprotect_one(
                        parallel_builder.to_sexp(), result_index
                    );
                }
                else {
                    builder.reset(str_len);

                    for (R_xlen_t i = 0; i < str_len; ++i) {
                        const charport::StrView source = values[i];
                        if (source.is_na()) {
                            builder.set_na(i);
                            continue;
                        }

                        const ReverseInput value = normalize_input(
                            source, converter
                        );
                        char* output = builder.reserve(
                            i, static_cast<std::size_t>(value.len),
                            value.ascii
                                ? CETYPE_EXT_ASCII
                                : CETYPE_EXT_UTF8
                        );
                        reverse_input(value, output);
                    }

                    result = entry_protections.reprotect_one(
                        builder.to_sexp(), result_index
                    );
                }
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}

} } // namespace charr::altrep_backend
