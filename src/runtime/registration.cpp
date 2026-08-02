
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

#include <R.h>
#include <Rconfig.h>
#include <Rversion.h>
#include <R_ext/Rdynload.h>

#include "../altrep_backend/entrypoints.h"
#include "../base_backend/entrypoints.h"
#include "icu.h"

#define CHARR_CALL(symbol, function, arity) \
    {symbol, (DL_FUNC)(void (*) (void))(&function), arity}

namespace charr {
namespace runtime {

const R_CallMethodDef call_methods[] = {
#define CHARR_ALTREP_REGISTER(name, arity) \
    CHARR_CALL( \
        "C_" #name, \
        charr::altrep_backend::entrypoints::C_charr_altrep_##name, \
        arity \
    ),
    CHARR_BACKEND_METHODS(CHARR_ALTREP_REGISTER)
#undef CHARR_ALTREP_REGISTER

#define CHARR_BASE_REGISTER(name, arity) \
    CHARR_CALL( \
        "C_charr_base_" #name, \
        charr::base_backend::entrypoints::C_charr_base_##name, \
        arity \
    ),
    CHARR_BACKEND_METHODS(CHARR_BASE_REGISTER)
#undef CHARR_BASE_REGISTER

    CHARR_CALL("C_charr_abi_ok", icu::C_charr_abi_ok, 0),
    CHARR_CALL("C_charr_icu_init", icu::C_charr_icu_init, 1),
    CHARR_CALL("C_charr_icu_info", icu::C_charr_icu_info, 0),
    CHARR_CALL("C_charr_icu_bundled", icu::C_charr_icu_bundled, 0),
    {NULL, NULL, 0}
};

extern "C" CHARR_R_HELPER void R_init_charr(DllInfo* dll) noexcept
{
    R_registerRoutines(dll, NULL, call_methods, NULL, NULL);
    R_useDynamicSymbols(dll, (Rboolean)FALSE);
#if defined(R_VERSION) && R_VERSION >= R_Version(3, 0, 0)
    R_forceSymbols(dll, (Rboolean)TRUE);
#endif

    if (!SUPPORT_UTF8) {
        // Rconfig.h states that all R platforms support UTF-8.
        Rf_error("R does not support UTF-8 encoding.");
    }
}

} // namespace runtime
} // namespace charr

// There is no package-unload cleanup hook. A system ICU is process-global and
// may be in use by R or another package, so charr must not call u_cleanup().
