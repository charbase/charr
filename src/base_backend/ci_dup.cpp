
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
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
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
#include "io/string_view.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/repeat.h"
#include "../shared/slice_arena.h"
#include "../shared/string_view.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <cstddef>
#include <exception>
#include <vector>


namespace charr { namespace base_backend {

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


CHARR_R_HELPER shared::StringView direct_string_view(SEXP value) noexcept
{
    shared::StringView output = io::as_shared_view(value);
    if (output.enc == shared::StringEncoding::utf8 && output.len >= 3 &&
            static_cast<unsigned char>(output.ptr[0]) == 0xefU &&
            static_cast<unsigned char>(output.ptr[1]) == 0xbbU &&
            static_cast<unsigned char>(output.ptr[2]) == 0xbfU) {
        output.ptr += 3;
        output.len -= 3;
    }
    return output;
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

    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    times = entry_protections.protect_one(ci__prepare_arg_integer_r(times, "times"));

    const R_len_t str_length = LENGTH(str);
    const R_len_t times_length = LENGTH(times);
    bool recycling_warning = false;
    const R_len_t vectorize_length = recycling_length(
        str_length, times_length, recycling_warning
    );
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);

    try {
        shared::NativeToUtf8 converter;
        shared::SliceArena converted_storage;
        // Direct ASCII and UTF-8 records remain borrowed. These sparse tables
        // are populated only when native or Latin-1 input needs stable bytes.
        std::vector<std::size_t> converted_slots;
        std::vector<shared::StringView> converted_values;
        std::vector<char> output_buffer;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                if (vectorize_length <= 0) {
                    result = entry_protections.reprotect_one(
                        Rf_allocVector(STRSXP, 0), result_index
                    );
                }
                else {
                    const SEXP* values = STRING_PTR_RO(str);

                    for (R_len_t i = 0; i < str_length; ++i) {
                        const SEXP value = values[i];
                        if (value == NA_STRING || IS_ASCII(value) ||
                                IS_UTF8(value)) {
                            continue;
                        }
                        if (IS_BYTES(value))
                            throw StriException(MSG__BYTESENC);

                        if (converted_slots.empty()) {
                            converted_slots.assign(
                                static_cast<std::size_t>(str_length),
                                no_slot()
                            );
                        }
                        const shared::StringView converted =
                            shared::normalize_utf8(
                                io::as_shared_view(value),
                                converter, converted_storage
                            );
                        converted_slots[static_cast<std::size_t>(i)] =
                            converted_values.size();
                        converted_values.push_back(converted);
                    }

                    const int* counts = INTEGER_RO(times);

                    result = entry_protections.reprotect_one(
                        Rf_allocVector(STRSXP, vectorize_length), result_index
                    );

                    for (R_len_t i = 0; i < vectorize_length; ++i) {
                        const R_len_t source_index = i % str_length;
                        shared::StringView value;
                        const std::size_t slot = converted_slots.empty()
                            ? no_slot()
                            : converted_slots[
                                static_cast<std::size_t>(source_index)
                            ];
                        if (slot == no_slot()) {
                            value = direct_string_view(values[source_index]);
                        }
                        else {
                            value = converted_values[slot];
                        }

                        const int count = counts[i % times_length];
                        if (value.is_na() || count == NA_INTEGER ||
                                count < 0) {
                            SET_STRING_ELT(result, i, NA_STRING);
                            continue;
                        }

                        std::size_t total = 0;
                        if (!shared::checked_repeat_size(
                                static_cast<std::size_t>(value.len), count,
                                static_cast<std::size_t>(POW_2_31_M_1),
                                total)) {
                            throw StriException(MSG__CHARSXP_2147483647);
                        }
                        if (total == 0) {
                            SET_STRING_ELT(result, i, R_BlankString);
                            continue;
                        }

                        if (total > output_buffer.size())
                            output_buffer.resize(total);
                        shared::repeat_bytes(
                            output_buffer.data(), value.ptr,
                            static_cast<std::size_t>(value.len), total
                        );
                        SET_STRING_ELT(
                            result, i,
                            Rf_mkCharLenCE(
                                output_buffer.data(),
                                static_cast<int>(total), CE_UTF8
                            )
                        );
                    }
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}

} } // namespace charr::base_backend
