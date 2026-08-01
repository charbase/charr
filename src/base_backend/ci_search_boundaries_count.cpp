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
#include "boundary/options_r.h"
#include "io/string_view.h"
#include "../shared/boundary_iterator.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <cstddef>
#include <exception>
#include <vector>

namespace charr { namespace base_backend {

namespace search_boundaries_count {

struct InputScan {
    bool direct;
    bool has_nonmissing;
};


CHARR_R_HELPER InputScan scan_direct_input_r(
    const SEXP* values, R_len_t length
) noexcept {
    bool has_nonmissing = false;
    for (R_len_t i = 0; i < length; ++i) {
        const SEXP value = values[i];
        if (value == NA_STRING)
            continue;
        has_nonmissing = true;
        if (IS_BYTES(value))
            Rf_error(MSG__BYTESENC);
        if (!IS_ASCII(value) && !IS_UTF8(value))
            return InputScan{false, has_nonmissing};
    }
    return InputScan{true, has_nonmissing};
}


CHARR_CXX_HELPER void require_icu_success(UErrorCode status)
{
    if (U_FAILURE(status))
        throw StriException(status);
}


CHARR_R_HELPER void emit_fallback_warning_r() noexcept
{
    Rf_warning(
        "%s", ICUError::getICUerrorName(U_USING_DEFAULT_WARNING)
    );
}

} // namespace search_boundaries_count

using namespace search_boundaries_count;


/** Count the number of BreakIterator boundaries
 *
 * @param str character vector
 * @param opts_brkiter identifier
 * @return integer vector
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-10-30)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-02)
 *          use boundary::Utf8Iterator
 */
CHARR_ENTRYPOINT SEXP ci_count_boundaries(
    SEXP str, SEXP opts_brkiter
) noexcept {
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    const shared::BoundaryOptions options =
        boundary::prepare_options_r(opts_brkiter, UBRK_LINE);

    bool root_fallback_warning = false;

    try {
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        std::vector<shared::StringView> normalized;
        shared::BoundaryIterator counter;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t length = LENGTH(str);
                result = entry_protections.reprotect_one(
                    Rf_allocVector(INTSXP, length), result_index
                );
                int* output = INTEGER(result);
                const SEXP* values = length > 0
                    ? STRING_PTR_RO(str)
                    : nullptr;
                const InputScan scan = scan_direct_input_r(values, length);

                if (scan.direct) {
                    if (scan.has_nonmissing) {
                        const shared::BoundaryOpenResult opened =
                            counter.reset(options);
                        root_fallback_warning = opened.root_fallback;
                        require_icu_success(opened.status);
                    }

                    for (R_len_t i = 0; i < length; ++i) {
                        const shared::StringView source =
                            io::as_shared_view(values[i]);
                        if (source.is_na()) {
                            output[i] = NA_INTEGER;
                            continue;
                        }

                        const shared::StringView value =
                            shared::normalize_utf8(
                                source, converter, storage
                            );
                        UErrorCode status = U_ZERO_ERROR;
                        output[i] = counter.count(value, status);
                        require_icu_success(status);
                    }
                }
                else {
                    normalized.resize(static_cast<std::size_t>(length));
                    bool has_nonmissing = false;
                    for (R_len_t i = 0; i < length; ++i) {
                        const shared::StringView source =
                            io::as_shared_view(values[i]);
                        if (source.enc == shared::StringEncoding::bytes)
                            throw StriException(MSG__BYTESENC);
                        normalized[static_cast<std::size_t>(i)] =
                            shared::normalize_utf8(
                                source, converter, storage
                            );
                        has_nonmissing = has_nonmissing || !source.is_na();
                    }

                    if (has_nonmissing) {
                        const shared::BoundaryOpenResult opened =
                            counter.reset(options);
                        root_fallback_warning = opened.root_fallback;
                        require_icu_success(opened.status);
                    }

                    for (R_len_t i = 0; i < length; ++i) {
                        const shared::StringView& value = normalized[
                            static_cast<std::size_t>(i)
                        ];
                        if (value.is_na()) {
                            output[i] = NA_INTEGER;
                            continue;
                        }
                        UErrorCode status = U_ZERO_ERROR;
                        output[i] = counter.count(value, status);
                        require_icu_success(status);
                    }
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        if (root_fallback_warning)
            emit_fallback_warning_r();
    );
}

} } // namespace charr::base_backend
