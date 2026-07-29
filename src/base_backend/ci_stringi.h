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


#ifndef __charr_base_ci_stringi_h
#define __charr_base_ci_stringi_h

#include "ci_external.h"
#include "ci_messages.h"
#include "ci_macros.h"
#include "ci_exception.h"
#include "ci_exports.h"
#include <unicode/ucol.h>



namespace charr { namespace base_backend {

// common.cpp
SEXP    ci__make_character_vector_char_ptr(R_len_t numnames, ...);
SEXP    ci__make_character_vector_UnicodeString_ptr(R_len_t numnames, ...);
R_len_t ci__recycling_rule(bool enableWarning, int n, ...);
SEXP    ci__vector_NA_integers(R_len_t howmany);
SEXP    ci__vector_NA_strings(R_len_t howmany);
SEXP    ci__vector_empty_strings(R_len_t howmany);
SEXP    ci__emptyList();
SEXP    ci__matrix_NA_INTEGER(R_len_t nrow, R_len_t ncol, int filler=NA_INTEGER);  // TODO: other ones can be generalised too
SEXP    ci__matrix_NA_STRING(R_len_t nrow, R_len_t ncol);
int     ci__match_arg(const char* option, const char** set);

// collator.cpp:
::UCollator* ci__ucol_open(SEXP opts_collator);

// length.cpp
R_len_t ci__numbytes_max(SEXP str);
int     ci__width_char(UChar32 c);
int     ci__width_char_with_context(UChar32 c, UChar32 p, bool& reset);
int     ci__width_string(const char* s, int n, int max_width=NA_INTEGER);
int     ci__length_string(const char* s, int n, int max_length=NA_INTEGER);

// prepare_arg.cpp:
SEXP ci__prepare_arg_string_1(SEXP x,  const char* argname);
SEXP ci__prepare_arg_double_1(SEXP x,  const char* argname, bool factors_as_strings=true);
SEXP ci__prepare_arg_integer_1(SEXP x, const char* argname, bool factors_as_strings=true);
SEXP ci__prepare_arg_logical_1(SEXP x, const char* argname);

const char* ci__copy_string_Ralloc(SEXP, const char* argname);
double      ci__prepare_arg_double_1_notNA(SEXP x,  const char* argname);
int         ci__prepare_arg_integer_1_notNA(SEXP x, const char* argname);
bool        ci__prepare_arg_logical_1_notNA(SEXP x, const char* argname);

double      ci__prepare_arg_double_1_NA(SEXP x, const char* argname);
int ci__prepare_arg_logical_1_NA(SEXP x, const char* argname);
int ci__prepare_arg_integer_1_NA(SEXP x, const char* argname);

bool ci__is_C_locale(const char* str);
const char* ci__prepare_arg_locale(
    SEXP loc, const char* argname,
    bool allowdefault=true, bool allownull=true
);
const char* ci__prepare_arg_enc(
    SEXP loc, const char* argname,
    bool allowdefault
);
TimeZone* ci__prepare_arg_timezone(SEXP tz, const char* argname, bool allowdefault);

SEXP ci__prepare_arg_list(SEXP x,         const char* argname);
SEXP ci__prepare_arg_list_string(SEXP x,  const char* argname);
SEXP ci__prepare_arg_list_integer(SEXP x, const char* argname);
SEXP ci__prepare_arg_list_raw(SEXP x,     const char* argname);

SEXP ci__prepare_arg_string(SEXP x,       const char* argname, bool allow_error=true);
SEXP ci__prepare_arg_logical(SEXP x,      const char* argname, bool allow_error=true);
SEXP ci__prepare_arg_double(SEXP x,       const char* argname, bool factors_as_strings=true, bool allow_error=true);
SEXP ci__prepare_arg_integer(SEXP x,      const char* argname, bool factors_as_strings=true, bool allow_error=true);
SEXP ci__prepare_arg_raw(SEXP x,          const char* argname, bool factors_as_strings=true, bool allow_error=true);

SEXP ci__prepare_arg_POSIXct(SEXP x,      const char* argname);



// search
void ci__locate_set_dimnames_list(
    SEXP list,
    bool get_length=false
);
void ci__locate_set_dimnames_matrix(
    SEXP matrix,
    bool get_length=false
);


// date/time
void ci__set_class_POSIXct(SEXP x);
Calendar* ci__get_calendar(const char* locale_val);

// ------------------------------------------------------------------------


} } // namespace charr::base_backend

#endif
