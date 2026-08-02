
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
#include "../shared/nfc_normalizer.h"
#include "../shared/protect.h"
#include "../shared/unwind.h"
#include "altrep_backend/io/string_view.h"
#include "altrep_backend/io/utf8_output.h"

#include <charport.h>

#include <exception>
#include <stdexcept>

namespace charr { namespace altrep_backend {


/**
 * Perform Unicode NFC normalization
 *
 * @param str character vector
 * @return character vector
 */
CHARR_ENTRYPOINT SEXP ci_trans_nfc(SEXP str) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );

    try {
        shared::NfcNormalizer normalizer;
        charport::Reader reader;
        charport::StrViews values;
        io::OutputBuilder builder(0);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t str_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );

                UErrorCode status = normalizer.reset();
                if (U_FAILURE(status))
                    throw StriException(status);

                if (str_length > 0) {
                    reader.reset(str);
                    if (reader.size() != str_length) {
                        throw std::runtime_error(
                            "Reader length changed during NFC normalization"
                        );
                    }
                    values.resize(str_length);
                    reader.views(
                        0, str_length,
                        values.ptrs(), values.lengths(), values.encodings()
                    );
                }
                builder.reset(str_length);

                for (R_len_t i = 0; i < str_length; ++i) {
                    status = U_ZERO_ERROR;
                    const shared::StringView normalized =
                        normalizer.normalize(
                            io::as_shared_view(values[i]), status
                        );
                    if (U_FAILURE(status))
                        throw StriException(status);

                    if (normalized.is_na())
                        builder.set_na(i);
                    else
                        builder.set(i, io::as_charport_view(normalized));
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
