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
#include "ci_string8buf.h"
#include "io/string_view.h"
#include "../shared/case_mapper.h"
#include "../shared/entrypoint.h"
#include "../shared/protect.h"
#include "../shared/string_view.h"
#include "../shared/unwind.h"

#include <cstddef>
#include <exception>


namespace charr { namespace base_backend {

namespace trans_casemap {

CHARR_CXX_HELPER void require_icu_success(UErrorCode status)
{
    if (U_FAILURE(status))
        throw StriException(status);
}

} // namespace trans_casemap

using namespace trans_casemap;


/**
 * Convert to lower case
 *
 * @param str character vector
 * @param locale single string identifying
 *        the locale ("" or NULL for default locale)
 * @return character vector
 */
CHARR_ENTRYPOINT SEXP ci_trans_tolower(SEXP str, SEXP locale) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const char* qloc = ci__prepare_arg_locale_r(locale, "locale");
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));

    try {
        shared::CaseMapper mapper;
        String8buf buffer(64);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                mapper.reset(qloc, shared::CaseMapMode::lower);

                const R_xlen_t str_length = XLENGTH(str);
                result = entry_protections.reprotect_one(
                    Rf_allocVector(STRSXP, str_length), result_index
                );

                for (R_xlen_t i = 0; i < str_length; ++i) {
                    const SEXP value = STRING_ELT(str, i);
                    if (value == NA_STRING) {
                        SET_STRING_ELT(result, i, NA_STRING);
                        continue;
                    }

                    const shared::StringView source =
                        io::as_shared_view(value);
                    const shared::CaseMapInput input = mapper.prepare(source);
                    if (mapper.has_ascii_fast_path(input)) {
                        buffer.resize(
                            static_cast<std::size_t>(input.length), false
                        );
                        const bool changed = mapper.map_ascii(
                            input, buffer.data()
                        );
                        if (!changed && input.data == source.ptr &&
                                input.length == source.len) {
                            SET_STRING_ELT(result, i, value);
                        }
                        else {
                            SET_STRING_ELT(
                                result, i,
                                Rf_mkCharLenCE(
                                    buffer.data(), input.length, CE_UTF8
                                )
                            );
                        }
                        continue;
                    }

                    UErrorCode status = U_ZERO_ERROR;
                    const shared::StringView mapped = mapper.map_icu(
                        input, status
                    );
                    require_icu_success(status);
                    SET_STRING_ELT(
                        result, i,
                        Rf_mkCharLenCE(mapped.ptr, mapped.len, CE_UTF8)
                    );
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}


/**
 * Convert to upper case
 *
 * @param str character vector
 * @param locale single string identifying
 *        the locale ("" or NULL for default locale)
 * @return character vector
 */
CHARR_ENTRYPOINT SEXP ci_trans_toupper(SEXP str, SEXP locale) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const char* qloc = ci__prepare_arg_locale_r(locale, "locale");
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));

    try {
        shared::CaseMapper mapper;
        String8buf buffer(64);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                mapper.reset(qloc, shared::CaseMapMode::upper);

                const R_xlen_t str_length = XLENGTH(str);
                result = entry_protections.reprotect_one(
                    Rf_allocVector(STRSXP, str_length), result_index
                );

                for (R_xlen_t i = 0; i < str_length; ++i) {
                    const SEXP value = STRING_ELT(str, i);
                    if (value == NA_STRING) {
                        SET_STRING_ELT(result, i, NA_STRING);
                        continue;
                    }

                    const shared::StringView source =
                        io::as_shared_view(value);
                    const shared::CaseMapInput input = mapper.prepare(source);
                    if (mapper.has_ascii_fast_path(input)) {
                        buffer.resize(
                            static_cast<std::size_t>(input.length), false
                        );
                        const bool changed = mapper.map_ascii(
                            input, buffer.data()
                        );
                        if (!changed && input.data == source.ptr &&
                                input.length == source.len) {
                            SET_STRING_ELT(result, i, value);
                        }
                        else {
                            SET_STRING_ELT(
                                result, i,
                                Rf_mkCharLenCE(
                                    buffer.data(), input.length, CE_UTF8
                                )
                            );
                        }
                        continue;
                    }

                    UErrorCode status = U_ZERO_ERROR;
                    const shared::StringView mapped = mapper.map_icu(
                        input, status
                    );
                    require_icu_success(status);
                    SET_STRING_ELT(
                        result, i,
                        Rf_mkCharLenCE(mapped.ptr, mapped.len, CE_UTF8)
                    );
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}

} } // namespace charr::base_backend
