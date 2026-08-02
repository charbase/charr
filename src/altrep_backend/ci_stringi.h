
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


#ifndef __ci_stringi_h
#define __ci_stringi_h

#include "ci_external.h"
#include "ci_messages.h"
#include "ci_macros.h"
#include "ci_exception.h"
#include "ci_exports.h"
#include <unicode/ucol.h>

namespace charr { namespace altrep_backend {


// length.cpp
CHARR_NEUTRAL_HELPER int ci__width_char(UChar32 c) noexcept;
CHARR_NEUTRAL_HELPER int ci__width_char_with_context(
    UChar32 c, UChar32 p, bool& reset
) noexcept;
CHARR_CXX_HELPER int ci__width_string(
    const char* s, int n, int max_width=NA_INTEGER
);
CHARR_CXX_HELPER int ci__length_string(
    const char* s, int n, int max_length=NA_INTEGER
);

// prepare_arg.cpp:
CHARR_R_HELPER SEXP ci__prepare_arg_string_1_r(
    SEXP x, const char* argname
) noexcept;
CHARR_R_HELPER SEXP ci__prepare_arg_logical_1_r(
    SEXP x, const char* argname
) noexcept;
CHARR_R_HELPER double ci__prepare_arg_double_1_notNA_r(
    SEXP x, const char* argname
) noexcept;
CHARR_R_HELPER int ci__prepare_arg_integer_1_notNA_r(
    SEXP x, const char* argname
) noexcept;
CHARR_R_HELPER bool ci__prepare_arg_logical_1_notNA_r(
    SEXP x, const char* argname
) noexcept;
CHARR_R_HELPER int ci__prepare_arg_logical_1_NA_r(
    SEXP x, const char* argname
) noexcept;
CHARR_R_HELPER const char* ci__prepare_arg_locale_r(
    SEXP loc, const char* argname,
    bool allowdefault=true, bool allownull=true
) noexcept;
CHARR_R_HELPER const char* ci__prepare_arg_enc_r(
    SEXP enc, const char* argname,
    bool allowdefault
) noexcept;
CHARR_R_HELPER SEXP ci__prepare_arg_list_r(
    SEXP x, const char* argname
) noexcept;
CHARR_R_HELPER SEXP ci__prepare_arg_list_string_r(
    SEXP x, const char* argname
) noexcept;
CHARR_R_HELPER SEXP ci__prepare_arg_list_raw_r(
    SEXP x, const char* argname
) noexcept;
CHARR_R_HELPER SEXP ci__prepare_arg_string_r(
    SEXP x, const char* argname, bool allow_error=true
) noexcept;
CHARR_R_HELPER SEXP ci__prepare_arg_logical_r(
    SEXP x, const char* argname, bool allow_error=true
) noexcept;
CHARR_R_HELPER SEXP ci__prepare_arg_integer_r(
    SEXP x, const char* argname, bool factors_as_strings=true,
    bool allow_error=true
) noexcept;



// search
CHARR_R_HELPER void ci__locate_set_dimnames_list(
    SEXP list,
    bool get_length=false
) noexcept;
CHARR_R_HELPER void ci__locate_set_dimnames_matrix(
    SEXP matrix,
    bool get_length=false
) noexcept;

// ------------------------------------------------------------------------


} } // namespace charr::altrep_backend

#endif
