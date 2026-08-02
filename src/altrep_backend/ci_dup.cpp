
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
#include "io/string_view.h"
#include "io/utf8_output.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/repeat.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <charport.h>

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <vector>

namespace charr { namespace altrep_backend {


namespace dup {


CHARR_NEUTRAL_HELPER R_len_t recycling_length(
    R_len_t first, R_len_t second, bool& warning
) noexcept
{
    warning = false;
    if (first <= 0 || second <= 0)
        return 0;

    const R_len_t result = first > second ? first : second;
    warning = result % first != 0 || result % second != 0;
    return result;
}


CHARR_NEUTRAL_HELPER std::size_t no_slot() noexcept
{
    return static_cast<std::size_t>(-1);
}


CHARR_NEUTRAL_HELPER bool is_ascii(
    const char* data, int length
) noexcept
{
    for (int i = 0; i < length; ++i) {
        if (static_cast<unsigned char>(data[i]) > 0x7fU)
            return false;
    }
    return true;
}


CHARR_NEUTRAL_HELPER shared::StringView direct_input(
    const charport::StrView& value
) noexcept
{
    if (value.is_na()) {
        return shared::StringView{
            nullptr, shared::missing_string_length,
            shared::StringEncoding::missing
        };
    }

    const char* data = value.ptr;
    int length = value.len;
    if ((value.enc == CETYPE_EXT_UTF8 ||
            value.enc == CETYPE_EXT_ASCII_OR_UTF8) &&
            length >= 3 &&
            static_cast<unsigned char>(data[0]) == 0xefU &&
            static_cast<unsigned char>(data[1]) == 0xbbU &&
            static_cast<unsigned char>(data[2]) == 0xbfU) {
        data += 3;
        length -= 3;
    }

    if (value.enc == CETYPE_EXT_ASCII) {
        return shared::StringView{
            data, length, shared::StringEncoding::ascii
        };
    }
    if (value.enc == CETYPE_EXT_ASCII_OR_UTF8) {
        return shared::StringView{
            data, length,
            is_ascii(data, length)
                ? shared::StringEncoding::ascii
                : shared::StringEncoding::utf8
        };
    }
    return shared::StringView{
        data, length, shared::StringEncoding::utf8
    };
}


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

        switch (value.enc.value) {
        case CETYPE_EXT_ASCII.value:
        case CETYPE_EXT_UTF8.value:
        case CETYPE_EXT_ASCII_OR_UTF8.value:
            break;
        case CETYPE_EXT_NATIVE.value:
        case CETYPE_EXT_LATIN1.value:
            if (converted_slots.empty()) {
                converted_slots.assign(
                    static_cast<std::size_t>(size), no_slot()
                );
            }
            converted_slots[static_cast<std::size_t>(i)] =
                converted_values.size();
            converted_values.push_back(shared::normalize_utf8(
                io::as_shared_view(value), converter, storage
            ));
            break;
        case CETYPE_EXT_BYTES.value:
            throw StriException(MSG__BYTESENC);
        case CETYPE_EXT_NA.value:
        default:
            throw StriException("unknown charport string encoding");
        }
    }
}


CHARR_NEUTRAL_HELPER shared::StringView input_at(
    const charport::StrViews& values,
    const std::vector<std::size_t>& converted_slots,
    const std::vector<shared::StringView>& converted_values,
    R_len_t index
) noexcept
{
    if (!converted_slots.empty()) {
        const std::size_t slot =
            converted_slots[static_cast<std::size_t>(index)];
        if (slot != no_slot())
            return converted_values[slot];
    }
    return direct_input(values[index]);
}


CHARR_NEUTRAL_HELPER cetype_ext_t output_encoding(
    shared::StringEncoding encoding
) noexcept
{
    return encoding == shared::StringEncoding::ascii
        ? CETYPE_EXT_ASCII
        : CETYPE_EXT_UTF8;
}


} // namespace dup

using namespace dup;


/**
 * Duplicate each string by its corresponding repetition count.
 *
 * @param str character vector
 * @param times integer vector
 * @return character vector
 */
CHARR_ENTRYPOINT SEXP ci_dup(SEXP str, SEXP times) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    times = entry_protections.protect_one(
        ci__prepare_arg_integer_r(times, "times")
    );

    const R_len_t str_length = LENGTH(str);
    const R_len_t times_length = LENGTH(times);
    bool recycling_warning = false;
    const R_len_t vectorize_length = recycling_length(
        str_length, times_length, recycling_warning
    );
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);

    try {
        charport::Reader reader;
        charport::StrViews values;
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        // Direct records remain borrowed from Reader. These sparse tables are
        // populated only when native or Latin-1 input needs stable UTF-8.
        std::vector<std::size_t> converted_slots;
        std::vector<shared::StringView> converted_values;
        io::OutputBuilder builder(0);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                if (vectorize_length > 0) {
                    reader.reset(str);
                    if (reader.size() != str_length) {
                        throw std::runtime_error(
                            "Reader length changed during string duplication"
                        );
                    }
                    values.resize(str_length);
                    reader.views(
                        0, str_length,
                        values.ptrs(), values.lengths(), values.encodings()
                    );
                    preflight_inputs(
                        values, converter, storage,
                        converted_slots, converted_values
                    );

                    const int* repetitions = INTEGER_RO(times);
                    builder.reset(vectorize_length);

                    for (R_len_t i = 0; i < vectorize_length; ++i) {
                        const shared::StringView value = input_at(
                            values, converted_slots, converted_values,
                            i % str_length
                        );
                        const int current = repetitions[i % times_length];
                        if (value.is_na() || current == NA_INTEGER ||
                                current < 0) {
                            builder.set_na(i);
                            continue;
                        }

                        const std::size_t length =
                            static_cast<std::size_t>(value.len);
                        if (current == 0 || length == 0) {
                            builder.set(
                                i, "", 0, CETYPE_EXT_ASCII
                            );
                            continue;
                        }

                        std::size_t total;
                        if (!shared::checked_repeat_size(
                                length, current,
                                static_cast<std::size_t>(POW_2_31_M_1),
                                total
                            )) {
                            throw StriException(MSG__CHARSXP_2147483647);
                        }

                        char* destination = builder.reserve(
                            i, total, output_encoding(value.enc)
                        );
                        shared::repeat_bytes(
                            destination, value.ptr, length, total
                        );
                    }
                }
                else {
                    builder.reset(0);
                }

                result = entry_protections.reprotect_one(
                    builder.to_sexp(), result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}


} } // namespace charr::altrep_backend
