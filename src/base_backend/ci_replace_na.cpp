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
#include "io/string_view.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/string_view.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <exception>
#include <vector>


namespace charr { namespace base_backend {

namespace replace_na {

struct SourceState {
    bool direct;
    bool has_na;
    bool identity;
};


CHARR_NEUTRAL_HELPER bool has_utf8_bom(
    const char* data, int length
) noexcept
{
    return length >= 3 &&
        static_cast<unsigned char>(data[0]) == 0xef &&
        static_cast<unsigned char>(data[1]) == 0xbb &&
        static_cast<unsigned char>(data[2]) == 0xbf;
}


CHARR_R_HELPER SourceState inspect_source(
    SEXP source, const SEXP* values, R_len_t length
) noexcept
{
    bool direct = !ALTREP(source);
    bool has_na = false;

    if (direct) {
        for (R_len_t i = 0; i < length; ++i) {
            const SEXP value = values[i];
            if (value == NA_STRING) {
                has_na = true;
                continue;
            }
            if (!IS_ASCII(value) &&
                    (!IS_UTF8(value) ||
                     has_utf8_bom(CHAR(value), LENGTH(value)))) {
                direct = false;
                break;
            }
        }
    }

    return SourceState{
        direct,
        has_na,
        direct && !has_na && NO_ATTRIB(source)
    };
}


CHARR_NEUTRAL_HELPER cetype_t output_encoding(
    const shared::StringView& value
) noexcept
{
    return value.enc == shared::StringEncoding::ascii
        ? CE_NATIVE
        : CE_UTF8;
}


CHARR_R_HELPER SEXP make_charsxp(
    const shared::StringView& value, SEXP original,
    bool reuse_original
) noexcept
{
    if (value.is_na())
        return NA_STRING;
    if (reuse_original && value.ptr == CHAR(original) &&
            value.len == LENGTH(original)) {
        return original;
    }
    return Rf_mkCharLenCE(
        value.ptr, value.len, output_encoding(value)
    );
}

} // namespace replace_na

using namespace replace_na;


/**
 * Replace NAs with a given string.
 *
 * @param str character vector
 * @param replacement single string
 * @return character vector
 */
CHARR_ENTRYPOINT SEXP ci_replace_na(
    SEXP str, SEXP replacement
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    replacement = entry_protections.protect_one(ci__prepare_arg_string_1_r(
        replacement, "replacement"
    ));

    const R_len_t str_length = LENGTH(str);

    try {
        shared::NativeToUtf8 source_converter;
        shared::NativeToUtf8 replacement_converter;
        shared::SliceArena source_storage;
        shared::SliceArena replacement_storage;
        std::vector<shared::StringView> source_values;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const bool source_is_altrep = ALTREP(str) != 0;
                const SEXP* source_sexps =
                    str_length > 0 && !source_is_altrep
                    ? STRING_PTR_RO(str)
                    : nullptr;
                const SourceState source_state = inspect_source(
                    str, source_sexps, str_length
                );

                if (!source_state.direct) {
                    source_values.resize(
                        static_cast<std::size_t>(str_length)
                    );
                    for (R_len_t i = 0; i < str_length; ++i) {
                        const SEXP source_charsxp = source_sexps != nullptr
                            ? source_sexps[i]
                            : STRING_ELT(str, i);
                        source_values[static_cast<std::size_t>(i)] =
                            shared::normalize_utf8(
                                io::as_shared_view(source_charsxp),
                                source_converter, source_storage
                            );
                    }
                }

                const shared::StringView replacement_value =
                    shared::normalize_utf8(
                        io::as_shared_view(STRING_ELT(replacement, 0)),
                        replacement_converter, replacement_storage
                    );
                const SEXP replacement_charsxp = callback_protections.protect_one(
                    make_charsxp(
                        replacement_value, R_NilValue, false
                    )
                );

                if (source_state.identity) {
                    result = entry_protections.reprotect_one(str, result_index);
                }
                else {
                    result = entry_protections.reprotect_one(
                        Rf_allocVector(STRSXP, str_length), result_index
                    );
                    for (R_len_t i = 0; i < str_length; ++i) {
                        if (source_state.direct) {
                            SET_STRING_ELT(
                                result, i,
                                source_sexps[i] == NA_STRING
                                    ? replacement_charsxp
                                    : source_sexps[i]
                            );
                        }
                        else {
                            const shared::StringView& value = source_values[
                                static_cast<std::size_t>(i)
                            ];
                            SET_STRING_ELT(
                                result, i,
                                value.is_na()
                                    ? replacement_charsxp
                                    : make_charsxp(
                                        value,
                                        source_sexps != nullptr
                                            ? source_sexps[i]
                                            : R_NilValue,
                                        source_sexps != nullptr
                                    )
                            );
                        }
                    }
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}

} } // namespace charr::base_backend
