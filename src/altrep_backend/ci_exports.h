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


#ifndef __ci_exports_h
#define __ci_exports_h

#include <R.h>
#include <Rdefines.h>

#include "../shared/lint.h"

namespace charr { namespace altrep_backend {

// compare.cpp:
CHARR_ENTRYPOINT SEXP ci_cmp_equiv(
    SEXP e1, SEXP e2, SEXP opts_collator=R_NilValue
) noexcept;

// order_rank.cpp
CHARR_ENTRYPOINT SEXP ci_rank(
    SEXP str, SEXP opts_collator=R_NilValue
) noexcept;
CHARR_ENTRYPOINT SEXP ci_order(
    SEXP str, SEXP decreasing=Rf_ScalarLogical(FALSE),
    SEXP na_last=Rf_ScalarLogical(TRUE), SEXP opts_collator=R_NilValue
) noexcept;
CHARR_ENTRYPOINT SEXP ci_duplicated(
    SEXP str, SEXP fromLast=Rf_ScalarLogical(FALSE),
    SEXP opts_collator=R_NilValue
) noexcept;


// escape.cpp
CHARR_ENTRYPOINT SEXP ci_escape_unicode(SEXP str) noexcept;

// join.cpp:
CHARR_ENTRYPOINT SEXP ci_flatten(
    SEXP str, SEXP collapse=Rf_mkString(""),
    SEXP na_empty=Rf_ScalarLogical(FALSE),
    SEXP omit_empty=Rf_ScalarLogical(FALSE)
) noexcept;
CHARR_ENTRYPOINT SEXP ci_join(
    SEXP strlist, SEXP sep=Rf_mkString(""),
    SEXP collapse=R_NilValue,
    SEXP ignore_null=Rf_ScalarLogical(FALSE)
) noexcept;
CHARR_ENTRYPOINT SEXP ci_dup(SEXP str, SEXP times) noexcept;

// length.cpp
CHARR_ENTRYPOINT SEXP ci_length(SEXP str) noexcept;
CHARR_ENTRYPOINT SEXP ci_width(SEXP str) noexcept;

// reverse.cpp
CHARR_ENTRYPOINT SEXP ci_reverse(SEXP s) noexcept;

// sub.cpp
CHARR_ENTRYPOINT SEXP ci_sub(
    SEXP str, SEXP from, SEXP to, SEXP length,
    SEXP use_matrix=Rf_ScalarLogical(TRUE),
    SEXP ignore_negative_length=Rf_ScalarLogical(FALSE)
) noexcept;
CHARR_ENTRYPOINT SEXP ci_sub_replacement(
    SEXP str, SEXP from, SEXP to, SEXP length, SEXP omit_na, SEXP value,
    SEXP use_matrix=Rf_ScalarLogical(TRUE)
) noexcept;
CHARR_ENTRYPOINT SEXP ci_sub_all(
    SEXP str, SEXP from, SEXP to, SEXP length,
    SEXP use_matrix=Rf_ScalarLogical(TRUE),
    SEXP ignore_negative_length=Rf_ScalarLogical(TRUE)
) noexcept;
CHARR_ENTRYPOINT SEXP ci_sub_replacement_all(
    SEXP str, SEXP from, SEXP to, SEXP length,
    SEXP omit_na, SEXP value,
    SEXP use_matrix=Rf_ScalarLogical(TRUE)
) noexcept;

// encoding_management.cpp:
CHARR_ENTRYPOINT SEXP ci_enc_info(SEXP enc=R_NilValue) noexcept;


// trim.cpp:
CHARR_ENTRYPOINT SEXP ci_trim_both(
    SEXP str, SEXP pattern, SEXP negate=Rf_ScalarLogical(FALSE)
) noexcept;
CHARR_ENTRYPOINT SEXP ci_trim_left(
    SEXP str, SEXP pattern, SEXP negate=Rf_ScalarLogical(FALSE)
) noexcept;
CHARR_ENTRYPOINT SEXP ci_trim_right(
    SEXP str, SEXP pattern, SEXP negate=Rf_ScalarLogical(FALSE)
) noexcept;


// encoding_conversion.cpp:
CHARR_ENTRYPOINT SEXP ci_encode(SEXP str, SEXP from=R_NilValue, SEXP to=R_NilValue,
    SEXP to_raw=Rf_ScalarLogical(FALSE)) noexcept;


// pad.cpp
CHARR_ENTRYPOINT SEXP ci_pad(
    SEXP str, SEXP width, SEXP side=Rf_mkString("left"),
    SEXP pad=Rf_mkString(" "), SEXP use_length=Rf_ScalarLogical(FALSE)
) noexcept;


// wrap.cpp
CHARR_ENTRYPOINT SEXP ci_wrap(SEXP str, SEXP width, SEXP cost_exponent=Rf_ScalarInteger(2),
    SEXP indent=Rf_ScalarInteger(0), SEXP exdent=Rf_ScalarInteger(0),
    SEXP prefix=Rf_mkString(""), SEXP initial=Rf_mkString(""),
    SEXP whitespace_only=Rf_ScalarLogical(FALSE),
    SEXP use_length=Rf_ScalarLogical(FALSE), SEXP locale=R_NilValue,
    SEXP normalize=Rf_ScalarLogical(TRUE),
    SEXP output_mode=Rf_ScalarInteger(0)) noexcept;


// trans_title.cpp:
CHARR_ENTRYPOINT SEXP ci_trans_totitle(
    SEXP str, SEXP opts_brkiter=R_NilValue
) noexcept;

// trans_casemap.cpp:
CHARR_ENTRYPOINT SEXP ci_trans_tolower(
    SEXP str, SEXP locale=R_NilValue
) noexcept;
CHARR_ENTRYPOINT SEXP ci_trans_toupper(
    SEXP str, SEXP locale=R_NilValue
) noexcept;

// trans_normalization.cpp:
CHARR_ENTRYPOINT SEXP ci_trans_nfc(SEXP s) noexcept;

// search
CHARR_ENTRYPOINT SEXP ci_read_lines(
    SEXP path, SEXP encoding
) noexcept;
CHARR_ENTRYPOINT SEXP ci_split_lines(
    SEXP str, SEXP omit_empty=Rf_ScalarLogical(FALSE)
) noexcept;
CHARR_ENTRYPOINT SEXP ci_split_lines1(SEXP str) noexcept;

CHARR_ENTRYPOINT SEXP ci_replace_na(
    SEXP str, SEXP replacement=Rf_mkString("NA")
) noexcept;

CHARR_ENTRYPOINT SEXP ci_detect_coll(SEXP str, SEXP pattern,
    SEXP negate=Rf_ScalarLogical(FALSE), SEXP max_count=Rf_ScalarInteger(-1),
    SEXP opts_collator=R_NilValue) noexcept;
CHARR_ENTRYPOINT SEXP ci_count_coll(
    SEXP str, SEXP pattern, SEXP opts_collator=R_NilValue
) noexcept;
CHARR_ENTRYPOINT SEXP ci_locate_all_coll(SEXP str, SEXP pattern,
    SEXP omit_no_match=Rf_ScalarLogical(FALSE),
    SEXP opts_collator=R_NilValue,
    SEXP get_length=Rf_ScalarLogical(FALSE)
) noexcept;
CHARR_ENTRYPOINT SEXP ci_locate_first_coll(
    SEXP str, SEXP pattern, SEXP opts_collator=R_NilValue,
    SEXP get_length=Rf_ScalarLogical(FALSE)
) noexcept;
CHARR_ENTRYPOINT SEXP ci_extract_first_coll(
    SEXP str, SEXP pattern, SEXP opts_collator=R_NilValue
) noexcept;
CHARR_ENTRYPOINT SEXP ci_extract_all_coll(SEXP str, SEXP pattern,
    SEXP simplify=Rf_ScalarLogical(FALSE),
    SEXP omit_no_match=Rf_ScalarLogical(FALSE),
    SEXP opts_collator=R_NilValue) noexcept;
CHARR_ENTRYPOINT SEXP ci_replace_all_coll(
    SEXP str, SEXP pattern, SEXP replacement,
    SEXP vectorize_all=Rf_ScalarLogical(TRUE),
    SEXP opts_collator=R_NilValue
) noexcept;
CHARR_ENTRYPOINT SEXP ci_replace_first_coll(
    SEXP str, SEXP pattern, SEXP replacement,
    SEXP opts_collator=R_NilValue
) noexcept;
CHARR_ENTRYPOINT SEXP ci_split_coll(SEXP str, SEXP split, SEXP n=Rf_ScalarInteger(-1),
    SEXP omit_empty=Rf_ScalarLogical(FALSE), SEXP tokens_only=Rf_ScalarLogical(FALSE),
    SEXP simplify=Rf_ScalarLogical(FALSE),
    SEXP opts_collator=R_NilValue) noexcept;
CHARR_ENTRYPOINT SEXP ci_endswith_coll(SEXP str, SEXP pattern, SEXP to=Rf_ScalarInteger(-1),
    SEXP negate=Rf_ScalarLogical(FALSE),
    SEXP opts_collator=R_NilValue) noexcept;
CHARR_ENTRYPOINT SEXP ci_startswith_coll(SEXP str, SEXP pattern, SEXP from=Rf_ScalarInteger(1),
    SEXP negate=Rf_ScalarLogical(FALSE),
    SEXP opts_collator=R_NilValue) noexcept;

CHARR_ENTRYPOINT SEXP ci_detect_fixed(SEXP str, SEXP pattern,
    SEXP negate=Rf_ScalarLogical(FALSE), SEXP max_count=Rf_ScalarInteger(-1),
    SEXP opts_fixed=R_NilValue) noexcept;
CHARR_ENTRYPOINT SEXP ci_count_fixed(
    SEXP str, SEXP pattern, SEXP opts_fixed=R_NilValue
) noexcept;
CHARR_ENTRYPOINT SEXP ci_locate_all_fixed(
    SEXP str, SEXP pattern,
    SEXP omit_no_match=Rf_ScalarLogical(FALSE), SEXP opts_fixed=R_NilValue,
    SEXP get_length=Rf_ScalarLogical(FALSE)
) noexcept;
CHARR_ENTRYPOINT SEXP ci_locate_first_fixed(
    SEXP str, SEXP pattern, SEXP opts_fixed=R_NilValue,
    SEXP get_length=Rf_ScalarLogical(FALSE)
) noexcept;
CHARR_ENTRYPOINT SEXP ci_extract_first_fixed(
    SEXP str, SEXP pattern, SEXP opts_fixed=R_NilValue
) noexcept;
CHARR_ENTRYPOINT SEXP ci_extract_all_fixed(
    SEXP str, SEXP pattern,
    SEXP simplify=Rf_ScalarLogical(FALSE),
    SEXP omit_no_match=Rf_ScalarLogical(FALSE), SEXP opts_fixed=R_NilValue
) noexcept;
CHARR_ENTRYPOINT SEXP ci_replace_all_fixed(
    SEXP str, SEXP pattern, SEXP replacement,
    SEXP vectorize_all=Rf_ScalarLogical(TRUE),
    SEXP opts_fixed=R_NilValue
) noexcept;
CHARR_ENTRYPOINT SEXP ci_replace_first_fixed(
    SEXP str, SEXP pattern, SEXP replacement,
    SEXP opts_fixed=R_NilValue
) noexcept;
CHARR_ENTRYPOINT SEXP ci_split_fixed(
    SEXP str, SEXP split, SEXP n=Rf_ScalarInteger(-1),
    SEXP omit_empty=Rf_ScalarLogical(FALSE),
    SEXP tokens_only=Rf_ScalarLogical(FALSE),
    SEXP simplify=Rf_ScalarLogical(FALSE), SEXP opts_fixed=R_NilValue
) noexcept;
CHARR_ENTRYPOINT SEXP ci_endswith_fixed(
    SEXP str, SEXP pattern, SEXP to=Rf_ScalarInteger(-1),
    SEXP negate=Rf_ScalarLogical(FALSE),
    SEXP opts_fixed=R_NilValue
) noexcept;
CHARR_ENTRYPOINT SEXP ci_startswith_fixed(
    SEXP str, SEXP pattern, SEXP from=Rf_ScalarInteger(1),
    SEXP negate=Rf_ScalarLogical(FALSE),
    SEXP opts_fixed=R_NilValue
) noexcept;

CHARR_ENTRYPOINT SEXP ci_detect_regex(
    SEXP str, SEXP pattern,
    SEXP negate=Rf_ScalarLogical(FALSE),
    SEXP max_count=Rf_ScalarInteger(-1),
    SEXP opts_regex=R_NilValue
) noexcept;
CHARR_ENTRYPOINT SEXP ci_count_regex(
    SEXP str, SEXP pattern, SEXP opts_regex=R_NilValue
) noexcept;
CHARR_ENTRYPOINT SEXP ci_locate_all_regex(
    SEXP str, SEXP pattern,
    SEXP omit_no_match=Rf_ScalarLogical(FALSE),
    SEXP opts_regex=R_NilValue,
    SEXP capture_groups=Rf_ScalarLogical(FALSE),
    SEXP get_length=Rf_ScalarLogical(FALSE)
) noexcept;
CHARR_ENTRYPOINT SEXP ci_locate_first_regex(
    SEXP str, SEXP pattern, SEXP opts_regex=R_NilValue,
    SEXP capture_groups=Rf_ScalarLogical(FALSE),
    SEXP get_length=Rf_ScalarLogical(FALSE)
) noexcept;
CHARR_ENTRYPOINT SEXP ci_replace_all_regex(
    SEXP str, SEXP pattern, SEXP replacement,
    SEXP vectorize_all=Rf_ScalarLogical(FALSE), SEXP opts_regex=R_NilValue
) noexcept;
CHARR_ENTRYPOINT SEXP ci_replace_first_regex(
    SEXP str, SEXP pattern, SEXP replacement,
    SEXP opts_regex=R_NilValue
) noexcept;
CHARR_ENTRYPOINT SEXP ci_split_regex(
    SEXP str, SEXP pattern, SEXP n=Rf_ScalarInteger(-1),
    SEXP omit_empty=Rf_ScalarLogical(FALSE), SEXP tokens_only=Rf_ScalarLogical(FALSE),
    SEXP simplify=Rf_ScalarLogical(FALSE), SEXP opts_regex=R_NilValue
) noexcept;
CHARR_ENTRYPOINT SEXP ci_extract_first_regex(
    SEXP str, SEXP pattern, SEXP opts_regex=R_NilValue
) noexcept;
CHARR_ENTRYPOINT SEXP ci_extract_all_regex(SEXP str, SEXP pattern,
    SEXP simplify=Rf_ScalarLogical(FALSE), SEXP omit_no_match=Rf_ScalarLogical(FALSE),
    SEXP opts_regex=R_NilValue) noexcept;
CHARR_ENTRYPOINT SEXP ci_match_first_regex(SEXP str, SEXP pattern,
    SEXP cg_missing=Rf_ScalarString(NA_STRING),
    SEXP opts_regex=R_NilValue) noexcept;
CHARR_ENTRYPOINT SEXP ci_match_all_regex(SEXP str, SEXP pattern,
    SEXP omit_no_match=Rf_ScalarLogical(FALSE),
    SEXP cg_missing=Rf_ScalarString(NA_STRING),
    SEXP opts_regex=R_NilValue) noexcept;
CHARR_ENTRYPOINT SEXP ci_replace_all_charclass(
    SEXP str, SEXP pattern, SEXP replacement,
    SEXP merge=Rf_ScalarLogical(FALSE),
    SEXP vectorize_all=Rf_ScalarLogical(TRUE)
) noexcept;

CHARR_ENTRYPOINT SEXP ci_extract_all_boundaries(
    SEXP str, SEXP simplify,
    SEXP omit_no_match=Rf_ScalarLogical(FALSE),
    SEXP opts_brkiter=R_NilValue
) noexcept;
CHARR_ENTRYPOINT SEXP ci_extract_first_boundaries(
    SEXP str, SEXP opts_brkiter=R_NilValue
) noexcept;
CHARR_ENTRYPOINT SEXP ci_locate_all_boundaries(
    SEXP str, SEXP omit_no_match=Rf_ScalarLogical(FALSE),
    SEXP opts_brkiter=R_NilValue,
    SEXP get_length=Rf_ScalarLogical(FALSE)
) noexcept;
CHARR_ENTRYPOINT SEXP ci_locate_first_boundaries(
    SEXP str,
    SEXP opts_brkiter=R_NilValue,
    SEXP get_length=Rf_ScalarLogical(FALSE)
) noexcept;
CHARR_ENTRYPOINT SEXP ci_split_boundaries(
    SEXP str, SEXP n=Rf_ScalarInteger(-1),
    SEXP tokens_only=Rf_ScalarLogical(FALSE),
    SEXP simplify=Rf_ScalarLogical(FALSE), SEXP opts_brkiter=R_NilValue
) noexcept;
CHARR_ENTRYPOINT SEXP ci_count_boundaries(
    SEXP str, SEXP opts_brkiter=R_NilValue
) noexcept;


// SEXP ci_c_posixst(SEXP x);   // internal


// encoding_conversion.cpp:
// SEXP ci_encode_from_marked(SEXP str, SEXP to, SEXP to_raw);  // internal



} } // namespace charr::altrep_backend

#endif
