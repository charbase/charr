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
#include "../shared/encoding_info.h"
#include "../shared/entrypoint.h"
#include "../shared/protect.h"
#include "../shared/unwind.h"

#include <charport.h>

#include <cstddef>
#include <exception>

namespace charr { namespace altrep_backend {


namespace encoding_management {

CHARR_CXX_HELPER void stage_character(
    charport::charvec::Builder& builder,
    const shared::EncodingInfoValue& value
)
{
    builder.reset(1);
    if (value.missing || value.data == nullptr) {
        builder.set_na(0);
        return;
    }
    const charport::StrView staged{
        value.data, value.length, cetype_ext_t::CE_ASCII
    };
    builder.set(0, staged);
}


CHARR_R_HELPER void emit_warnings_r(
    const shared::EncodingInfo& info
) noexcept
{
    for (int i = 0; i < info.diagnostic_count(); ++i) {
        const shared::EncodingInfoDiagnostic diagnostic =
            info.diagnostic(i);
        const char* converter = diagnostic.converter_name != nullptr
            ? diagnostic.converter_name
            : "<unknown>";

        switch (diagnostic.kind) {
        case shared::EncodingInfoDiagnosticKind::standard_name:
            Rf_warning(
                "could not get standard name (StriUcnv::getStandards())"
            );
            break;
        case shared::EncodingInfoDiagnosticKind::ascii_conversion:
            Rf_warning(
                "Cannot convert ASCII character 0x%02x (encoding=%s)",
                diagnostic.input_byte, converter
            );
            break;
        case shared::EncodingInfoDiagnosticKind::conversion:
            Rf_warning(
                "Cannot convert character 0x%02x (encoding=%s)",
                diagnostic.input_byte, converter
            );
            break;
        case shared::EncodingInfoDiagnosticKind::non_single_code_point:
            Rf_warning(
                "Problematic character 0x%02x -> \\u%08x (encoding=%s)",
                diagnostic.input_byte,
                static_cast<unsigned int>(diagnostic.code_point),
                converter
            );
            break;
        case shared::EncodingInfoDiagnosticKind::round_trip:
            Rf_warning(
                "Problematic character 0x%02x -> \\u%08x -> 0x%02x "
                "(encoding=%s)",
                diagnostic.input_byte,
                static_cast<unsigned int>(diagnostic.code_point),
                diagnostic.output_byte, converter
            );
            break;
        }
    }

    if (info.warn_get_name())
        Rf_warning(MSG__ENC_ERROR_GETNAME);
}

} // namespace encoding_management

using namespace encoding_management;


// The stringr surface does not expose stringi's mutable ICU-default converter.
// Charr resolves native encodings explicitly for each operation instead of
// changing process-global ICU state that may be shared with other packages.


/** Fetch information on an encoding
 *
 * @param enc either NULL or "" for default encoding,
 *        or one string with encoding name
 * @return R list object with many components (see R doc for details)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.2-1 (Marek Gagolewski)
 *          use StriUcnv; make StriException-friendly
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
CHARR_ENTRYPOINT SEXP ci_enc_info(SEXP enc) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const char* selected_enc = ci__prepare_arg_enc_r(
        enc, "enc", true
    );

    try {
        shared::EncodingInfo info;
        charport::charvec::Builder character_builder(1);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const UErrorCode status = info.reset(selected_enc);
                if (U_FAILURE(status))
                    throw StriException(status);
                info.inspect();

                const int field_count = info.size();
                result = entry_protections.reprotect_one(
                    Rf_allocVector(VECSXP, field_count), result_index
                );
                SEXP names = callback_protections.protect_one(
                    Rf_allocVector(STRSXP, field_count)
                );

                for (int i = 0; i < field_count; ++i) {
                    if (info.has_name(i)) {
                        SET_STRING_ELT(
                            names, i,
                            Rf_mkCharLenCE(
                                info.name_data(i), info.name_length(i),
                                CE_UTF8
                            )
                        );
                    }

                    const shared::EncodingInfoValue value = info.value(i);
                    SEXP child = R_NilValue;
                    switch (value.kind) {
                    case shared::EncodingInfoValueKind::unset:
                        continue;
                    case shared::EncodingInfoValueKind::character:
                        stage_character(character_builder, value);
                        child = callback_protections.protect_one(
                            character_builder.to_sexp()
                        );
                        break;
                    case shared::EncodingInfoValueKind::logical:
                        child = callback_protections.protect_one(
                            Rf_ScalarLogical(
                                value.missing ? NA_LOGICAL : value.scalar
                            )
                        );
                        break;
                    case shared::EncodingInfoValueKind::integer:
                        child = callback_protections.protect_one(
                            Rf_ScalarInteger(
                                value.missing ? NA_INTEGER : value.scalar
                            )
                        );
                        break;
                    }
                    SET_VECTOR_ELT(result, i, child);
                }

                Rf_setAttrib(result, R_NamesSymbol, names);
                emit_warnings_r(info);
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}

} } // namespace charr::altrep_backend
