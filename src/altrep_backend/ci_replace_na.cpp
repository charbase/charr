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
#include "io/reader_utils.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"
#include "altrep_backend/io/string_view.h"

#include <charport.h>

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <vector>

namespace charr { namespace altrep_backend {


namespace replace_na {

CHARR_NEUTRAL_HELPER bool has_utf8_bom(
    const char* ptr, int len
) noexcept
{
    return len >= 3 &&
        static_cast<unsigned char>(ptr[0]) == 0xef &&
        static_cast<unsigned char>(ptr[1]) == 0xbb &&
        static_cast<unsigned char>(ptr[2]) == 0xbf;
}


CHARR_NEUTRAL_HELPER bool is_direct_source(
    const charport::StrView& source
) noexcept
{
    if (source.is_na() || source.enc == cetype_ext_t::CE_ASCII)
        return true;
    return source.enc == cetype_ext_t::CE_UTF8 &&
        !has_utf8_bom(source.ptr, source.len);
}


CHARR_NEUTRAL_HELPER shared::StringView resolve_output_encoding(
    shared::StringView value
) noexcept
{
    if (!value.is_na() &&
            value.enc == shared::StringEncoding::ascii_or_utf8) {
        value.enc = io::is_ascii(
            value.ptr, static_cast<std::size_t>(value.len)
        ) ? shared::StringEncoding::ascii : shared::StringEncoding::utf8;
    }
    return value;
}

} // namespace replace_na

using namespace replace_na;


/**
* Replace NAs with a given string
*
*
* @param str character vector
* @param replacement single string
* @return character vector
*
* @version 0.2-1 (Bartek Tartanus, 2014-03-15)
*
* @version 0.3-1 (Marek Gagolewski, 2014-11-05)
*    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
*/
CHARR_ENTRYPOINT SEXP ci_replace_na(
    SEXP str, SEXP replacement
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    replacement = entry_protections.protect_one(
        ci__prepare_arg_string_1_r(
            replacement, "replacement"
        )
    );
    const bool source_is_result_shaped = NO_ATTRIB(str) != 0;

    try {
        charport::Reader source_reader;
        charport::Reader replacement_reader;
        charport::StrViews source_views;
        charport::StrViews replacement_views;
        shared::NativeToUtf8 source_converter;
        shared::NativeToUtf8 replacement_converter;
        shared::SliceArena source_storage;
        shared::SliceArena replacement_storage;
        std::vector<shared::StringView> source_inputs;
        charport::charvec::Builder builder(0);
        charport::StrView replacement_input{
            nullptr, NA_INTEGER, cetype_ext_t::CE_NA
        };

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t source_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );

                source_reader.reset(str);
                if (source_reader.size() != source_length) {
                    throw std::runtime_error(
                        "Reader length changed during NA replacement"
                    );
                }
                source_views.resize(source_length);
                if (source_length > 0) {
                    source_reader.views(
                        0, source_length,
                        source_views.ptrs(), source_views.lengths(),
                        source_views.encodings()
                    );
                }

                bool direct = true;
                bool has_na = false;
                for (R_len_t i = 0; i < source_length; ++i) {
                    const charport::StrView source = source_views[i];
                    if (source.is_na()) {
                        has_na = true;
                        continue;
                    }
                    if (source.enc == cetype_ext_t::CE_BYTES) {
                        shared::normalize_utf8(
                            io::as_shared_view(source),
                            source_converter, source_storage
                        );
                    }
                    if (!is_direct_source(source)) {
                        direct = false;
                        break;
                    }
                }

                if (!direct) {
                    source_inputs.resize(
                        static_cast<std::size_t>(source_length)
                    );
                    for (R_len_t i = 0; i < source_length; ++i) {
                        source_inputs[static_cast<std::size_t>(i)] =
                            resolve_output_encoding(shared::normalize_utf8(
                                io::as_shared_view(source_views[i]),
                                source_converter, source_storage
                            ));
                    }
                }

                replacement_reader.reset(replacement);
                if (replacement_reader.size() != 1) {
                    throw std::runtime_error(
                        "replacement Reader length changed"
                    );
                }
                replacement_views.resize(1);
                replacement_reader.views(
                    0, 1,
                    replacement_views.ptrs(), replacement_views.lengths(),
                    replacement_views.encodings()
                );
                replacement_input = io::as_charport_view(
                    resolve_output_encoding(shared::normalize_utf8(
                        io::as_shared_view(replacement_views[0]),
                        replacement_converter, replacement_storage
                    ))
                );

                if (direct && !has_na && source_is_result_shaped) {
                    result = entry_protections.reprotect_one(str, result_index);
                }
                else {
                    builder.reset(source_length);
                    for (R_len_t i = 0; i < source_length; ++i) {
                        if (direct) {
                            const charport::StrView source = source_views[i];
                            builder.set(
                                i,
                                source.is_na() ? replacement_input : source
                            );
                        }
                        else {
                            const shared::StringView& source = source_inputs[
                                static_cast<std::size_t>(i)
                            ];
                            builder.set(
                                i,
                                source.is_na()
                                    ? replacement_input
                                    : io::as_charport_view(source)
                            );
                        }
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
