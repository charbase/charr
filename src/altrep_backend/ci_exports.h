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

// compare.cpp:
SEXP ci_cmp(SEXP e1, SEXP e2, SEXP opts_collator=R_NilValue);
SEXP ci_cmp_le(SEXP e1, SEXP e2, SEXP opts_collator=R_NilValue);
SEXP ci_cmp_lt(SEXP e1, SEXP e2, SEXP opts_collator=R_NilValue);
SEXP ci_cmp_ge(SEXP e1, SEXP e2, SEXP opts_collator=R_NilValue);
SEXP ci_cmp_gt(SEXP e1, SEXP e2, SEXP opts_collator=R_NilValue);
SEXP ci_cmp_equiv(SEXP e1, SEXP e2, SEXP opts_collator=R_NilValue);
SEXP ci_cmp_nequiv(SEXP e1, SEXP e2, SEXP opts_collator=R_NilValue);
SEXP ci_cmp_eq(SEXP e1, SEXP e2);
SEXP ci_cmp_neq(SEXP e1, SEXP e2);

// sort.cpp
SEXP ci_sort(SEXP str, SEXP decreasing=Rf_ScalarLogical(FALSE),
    SEXP na_last=Rf_ScalarLogical(NA_LOGICAL), SEXP opts_collator=R_NilValue);
SEXP ci_rank(SEXP str, SEXP opts_collator=R_NilValue);
SEXP ci_order(SEXP str, SEXP decreasing=Rf_ScalarLogical(FALSE),
    SEXP na_last=Rf_ScalarLogical(TRUE), SEXP opts_collator=R_NilValue);
SEXP ci_sort_key(SEXP str, SEXP opts_collator=R_NilValue);

SEXP ci_unique(SEXP str, SEXP opts_collator=R_NilValue);
SEXP ci_duplicated(SEXP str, SEXP fromLast=Rf_ScalarLogical(FALSE),
    SEXP opts_collator=R_NilValue);
SEXP ci_duplicated_any(SEXP str, SEXP fromLast=Rf_ScalarLogical(FALSE),
    SEXP opts_collator=R_NilValue);


// escape.cpp
SEXP ci_escape_unicode(SEXP str);
SEXP ci_unescape_unicode(SEXP str);

// join.cpp:
SEXP ci_flatten(SEXP str, SEXP collapse=Rf_mkString(""),
    SEXP na_empty=Rf_ScalarLogical(FALSE), SEXP omit_empty=Rf_ScalarLogical(FALSE));
SEXP ci_join(SEXP strlist, SEXP sep=Rf_mkString(""),
    SEXP collapse=R_NilValue, SEXP ignore_null=Rf_ScalarLogical(FALSE));
SEXP ci_join_list(SEXP x, SEXP sep=Rf_mkString(""),
    SEXP collapse=R_NilValue);
SEXP ci_join2(SEXP e1, SEXP e2);
SEXP ci_dup(SEXP str, SEXP times);

// length.cpp
SEXP ci_length(SEXP str);
SEXP ci_width(SEXP str);

// reverse.cpp
SEXP ci_reverse(SEXP s);

// sub.cpp
SEXP ci_sub(SEXP str, SEXP from, SEXP to, SEXP length, SEXP use_matrix=Rf_ScalarLogical(TRUE), SEXP ignore_negative_length=Rf_ScalarLogical(FALSE));
SEXP ci_sub_replacement(SEXP str, SEXP from, SEXP to, SEXP length, SEXP omit_na, SEXP value, SEXP use_matrix=Rf_ScalarLogical(TRUE));
SEXP ci_sub_all(SEXP str, SEXP from, SEXP to, SEXP length, SEXP use_matrix=Rf_ScalarLogical(TRUE), SEXP ignore_negative_length=Rf_ScalarLogical(TRUE));
SEXP ci_sub_replacement_all(SEXP str, SEXP from, SEXP to, SEXP length, SEXP omit_na, SEXP value, SEXP use_matrix=Rf_ScalarLogical(TRUE));

// encoding_management.cpp:
SEXP ci_enc_info(SEXP enc=R_NilValue);
SEXP ci_enc_mark(SEXP str);


// trim.cpp:
SEXP ci_trim_both(SEXP str, SEXP pattern, SEXP negate=Rf_ScalarLogical(FALSE));
SEXP ci_trim_left(SEXP str, SEXP pattern, SEXP negate=Rf_ScalarLogical(FALSE));
SEXP ci_trim_right(SEXP str, SEXP pattern, SEXP negate=Rf_ScalarLogical(FALSE));


// utils.cpp
SEXP ci_list2matrix(SEXP x, SEXP byrow=Rf_ScalarLogical(FALSE),
    SEXP fill=Rf_ScalarString(NA_STRING), SEXP n_min=Rf_ScalarInteger(0));


// encoding_conversion.cpp:
SEXP ci_encode(SEXP str, SEXP from=R_NilValue, SEXP to=R_NilValue,
    SEXP to_raw=Rf_ScalarLogical(FALSE));
SEXP ci_enc_fromutf32(SEXP str);
SEXP ci_enc_toutf32(SEXP str);
SEXP ci_enc_toutf8(SEXP str, SEXP is_unknown_8bit=Rf_ScalarLogical(FALSE),
    SEXP validate=Rf_ScalarLogical(FALSE));
SEXP ci_enc_toascii(SEXP str);


// pad.cpp
SEXP ci_pad(SEXP str, SEXP width, SEXP side=Rf_mkString("left"),
    SEXP pad=Rf_mkString(" "), SEXP use_length=Rf_ScalarLogical(FALSE));


// wrap.cpp
SEXP ci_wrap(SEXP str, SEXP width, SEXP cost_exponent=Rf_ScalarInteger(2),
    SEXP indent=Rf_ScalarInteger(0), SEXP exdent=Rf_ScalarInteger(0),
    SEXP prefix=Rf_mkString(""), SEXP initial=Rf_mkString(""),
    SEXP whitespace_only=Rf_ScalarLogical(FALSE),
    SEXP use_length=Rf_ScalarLogical(FALSE), SEXP locale=R_NilValue,
    SEXP normalize=Rf_ScalarLogical(TRUE),
    SEXP output_mode=Rf_ScalarInteger(0));


// trans_casemap.cpp:
SEXP ci_trans_totitle(SEXP str, SEXP opts_brkiter=R_NilValue);
SEXP ci_trans_tolower(SEXP str, SEXP locale=R_NilValue);
SEXP ci_trans_toupper(SEXP str, SEXP locale=R_NilValue);
SEXP ci_trans_casefold(SEXP str);

// trans_normalization.cpp:
SEXP ci_trans_nfc(SEXP s);

// search
SEXP ci_read_lines(SEXP path, SEXP encoding);
SEXP ci_split_lines(SEXP str, SEXP omit_empty=Rf_ScalarLogical(FALSE));
SEXP ci_split_lines1(SEXP str);

SEXP ci_replace_na(SEXP str, SEXP replacement=Rf_mkString("NA"));

SEXP ci_detect_coll(SEXP str, SEXP pattern,
    SEXP negate=Rf_ScalarLogical(FALSE), SEXP max_count=Rf_ScalarInteger(-1),
    SEXP opts_collator=R_NilValue);
SEXP ci_count_coll(SEXP str, SEXP pattern, SEXP opts_collator=R_NilValue);
SEXP ci_locate_all_coll(SEXP str, SEXP pattern,
    SEXP omit_no_match=Rf_ScalarLogical(FALSE),
    SEXP opts_collator=R_NilValue,
    SEXP get_length=Rf_ScalarLogical(FALSE)
);
SEXP ci_locate_first_coll(
    SEXP str, SEXP pattern, SEXP opts_collator=R_NilValue,
    SEXP get_length=Rf_ScalarLogical(FALSE)
);
SEXP ci_locate_last_coll(
    SEXP str, SEXP pattern, SEXP opts_collator=R_NilValue,
    SEXP get_length=Rf_ScalarLogical(FALSE)
);
SEXP ci_extract_first_coll(SEXP str, SEXP pattern, SEXP opts_collator=R_NilValue);
SEXP ci_extract_last_coll(SEXP str, SEXP pattern, SEXP opts_collator=R_NilValue);
SEXP ci_extract_all_coll(SEXP str, SEXP pattern,
    SEXP simplify=Rf_ScalarLogical(FALSE),
    SEXP omit_no_match=Rf_ScalarLogical(FALSE), SEXP opts_collator=R_NilValue);
SEXP ci_replace_all_coll(SEXP str, SEXP pattern, SEXP replacement,
    SEXP vectorize_all=Rf_ScalarLogical(TRUE), SEXP opts_collator=R_NilValue);
SEXP ci_replace_first_coll(SEXP str, SEXP pattern, SEXP replacement,
    SEXP opts_collator=R_NilValue);
SEXP ci_replace_last_coll(SEXP str, SEXP pattern, SEXP replacement,
    SEXP opts_collator=R_NilValue);
SEXP ci_split_coll(SEXP str, SEXP split, SEXP n=Rf_ScalarInteger(-1),
    SEXP omit_empty=Rf_ScalarLogical(FALSE), SEXP tokens_only=Rf_ScalarLogical(FALSE),
    SEXP simplify=Rf_ScalarLogical(FALSE), SEXP opts_collator=R_NilValue);
SEXP ci_endswith_coll(SEXP str, SEXP pattern, SEXP to=Rf_ScalarInteger(-1),
    SEXP negate=Rf_ScalarLogical(FALSE),
    SEXP opts_collator=R_NilValue);
SEXP ci_startswith_coll(SEXP str, SEXP pattern, SEXP from=Rf_ScalarInteger(1),
    SEXP negate=Rf_ScalarLogical(FALSE),
    SEXP opts_collator=R_NilValue);
SEXP ci_subset_coll(SEXP str, SEXP pattern,
    SEXP omit_na=Rf_ScalarLogical(FALSE), SEXP negate=Rf_ScalarLogical(FALSE), SEXP opts_collator=R_NilValue);
SEXP ci_subset_coll_replacement(SEXP str, SEXP pattern, SEXP negate, SEXP opts_collator, SEXP value);

SEXP ci_detect_fixed(SEXP str, SEXP pattern,
    SEXP negate=Rf_ScalarLogical(FALSE), SEXP max_count=Rf_ScalarInteger(-1),
    SEXP opts_fixed=R_NilValue);
SEXP ci_count_fixed(SEXP str, SEXP pattern, SEXP opts_fixed=R_NilValue);
SEXP ci_locate_all_fixed(
    SEXP str, SEXP pattern,
    SEXP omit_no_match=Rf_ScalarLogical(FALSE), SEXP opts_fixed=R_NilValue,
    SEXP get_length=Rf_ScalarLogical(FALSE)
);
SEXP ci_locate_first_fixed(
    SEXP str, SEXP pattern, SEXP opts_fixed=R_NilValue,
    SEXP get_length=Rf_ScalarLogical(FALSE)
);
SEXP ci_extract_first_fixed(
    SEXP str, SEXP pattern, SEXP opts_fixed=R_NilValue
);
SEXP ci_extract_last_fixed(
    SEXP str, SEXP pattern, SEXP opts_fixed=R_NilValue
);
SEXP ci_extract_all_fixed(
    SEXP str, SEXP pattern,
    SEXP simplify=Rf_ScalarLogical(FALSE),
    SEXP omit_no_match=Rf_ScalarLogical(FALSE), SEXP opts_fixed=R_NilValue
);
SEXP ci_replace_all_fixed(SEXP str, SEXP pattern, SEXP replacement,
    SEXP vectorize_all=Rf_ScalarLogical(TRUE), SEXP opts_fixed=R_NilValue);
SEXP ci_replace_first_fixed(SEXP str, SEXP pattern, SEXP replacement,
    SEXP opts_fixed=R_NilValue);
SEXP ci_replace_last_fixed(SEXP str, SEXP pattern, SEXP replacement,
    SEXP opts_fixed=R_NilValue);
SEXP ci_split_fixed(SEXP str, SEXP split, SEXP n=Rf_ScalarInteger(-1),
    SEXP omit_empty=Rf_ScalarLogical(FALSE), SEXP tokens_only=Rf_ScalarLogical(FALSE),
    SEXP simplify=Rf_ScalarLogical(FALSE), SEXP opts_fixed=R_NilValue);
SEXP ci_endswith_fixed(SEXP str, SEXP pattern, SEXP to=Rf_ScalarInteger(-1),
    SEXP negate=Rf_ScalarLogical(FALSE),
    SEXP opts_fixed=R_NilValue);
SEXP ci_startswith_fixed(SEXP str, SEXP pattern, SEXP from=Rf_ScalarInteger(1),
    SEXP negate=Rf_ScalarLogical(FALSE),
    SEXP opts_fixed=R_NilValue);

SEXP ci_detect_regex(
    SEXP str, SEXP pattern,
    SEXP negate=Rf_ScalarLogical(FALSE),
    SEXP max_count=Rf_ScalarInteger(-1),
    SEXP opts_regex=R_NilValue
);
SEXP ci_count_regex(SEXP str, SEXP pattern, SEXP opts_regex=R_NilValue);
SEXP ci_locate_all_regex(
    SEXP str, SEXP pattern,
    SEXP omit_no_match=Rf_ScalarLogical(FALSE),
    SEXP opts_regex=R_NilValue,
    SEXP capture_groups=Rf_ScalarLogical(FALSE),
    SEXP get_length=Rf_ScalarLogical(FALSE)
);
SEXP ci_locate_first_regex(
    SEXP str, SEXP pattern, SEXP opts_regex=R_NilValue,
    SEXP capture_groups=Rf_ScalarLogical(FALSE),
    SEXP get_length=Rf_ScalarLogical(FALSE)
);
SEXP ci_locate_last_regex(
    SEXP str, SEXP pattern, SEXP opts_regex=R_NilValue,
    SEXP capture_groups=Rf_ScalarLogical(FALSE),
    SEXP get_length=Rf_ScalarLogical(FALSE)
);
SEXP ci_replace_all_regex(
    SEXP str, SEXP pattern, SEXP replacement,
    SEXP vectorize_all=Rf_ScalarLogical(FALSE), SEXP opts_regex=R_NilValue
);
SEXP ci_replace_first_regex(
    SEXP str, SEXP pattern, SEXP replacement,
    SEXP opts_regex=R_NilValue
);
SEXP ci_replace_last_regex(
    SEXP str, SEXP pattern, SEXP replacement,
    SEXP opts_regex=R_NilValue
);
SEXP ci_split_regex(
    SEXP str, SEXP pattern, SEXP n=Rf_ScalarInteger(-1),
    SEXP omit_empty=Rf_ScalarLogical(FALSE), SEXP tokens_only=Rf_ScalarLogical(FALSE),
    SEXP simplify=Rf_ScalarLogical(FALSE), SEXP opts_regex=R_NilValue
);
SEXP ci_subset_regex(
    SEXP str, SEXP pattern, SEXP omit_na=Rf_ScalarLogical(FALSE),
    SEXP negate=Rf_ScalarLogical(FALSE), SEXP opts_regex=R_NilValue
);
SEXP ci_extract_first_regex(SEXP str, SEXP pattern, SEXP opts_regex=R_NilValue);
SEXP ci_extract_last_regex(SEXP str, SEXP pattern, SEXP opts_regex=R_NilValue);
SEXP ci_extract_all_regex(SEXP str, SEXP pattern,
    SEXP simplify=Rf_ScalarLogical(FALSE), SEXP omit_no_match=Rf_ScalarLogical(FALSE),
    SEXP opts_regex=R_NilValue);
SEXP ci_match_first_regex(SEXP str, SEXP pattern,
    SEXP cg_missing=Rf_ScalarString(NA_STRING), SEXP opts_regex=R_NilValue);
SEXP ci_match_last_regex(SEXP str, SEXP pattern,
    SEXP cg_missing=Rf_ScalarString(NA_STRING), SEXP opts_regex=R_NilValue);
SEXP ci_match_all_regex(SEXP str, SEXP pattern,
    SEXP omit_no_match=Rf_ScalarLogical(FALSE),
    SEXP cg_missing=Rf_ScalarString(NA_STRING), SEXP opts_regex=R_NilValue);
SEXP ci_subset_regex_replacement(SEXP str, SEXP pattern, SEXP negate, SEXP opts_regex, SEXP value);

SEXP ci_replace_all_charclass(SEXP str, SEXP pattern, SEXP replacement,
    SEXP merge=Rf_ScalarLogical(FALSE), SEXP vectorize_all=Rf_ScalarLogical(TRUE));

SEXP ci_extract_all_boundaries(SEXP str, SEXP simplify,
    SEXP omit_no_match=Rf_ScalarLogical(FALSE), SEXP opts_brkiter=R_NilValue);
SEXP ci_extract_first_boundaries(SEXP str, SEXP opts_brkiter=R_NilValue);
SEXP ci_extract_last_boundaries(SEXP str, SEXP opts_brkiter=R_NilValue);
SEXP ci_locate_all_boundaries(
    SEXP str, SEXP omit_no_match=Rf_ScalarLogical(FALSE),
    SEXP opts_brkiter=R_NilValue,
    SEXP get_length=Rf_ScalarLogical(FALSE)
);
SEXP ci_locate_first_boundaries(
    SEXP str,
    SEXP opts_brkiter=R_NilValue,
    SEXP get_length=Rf_ScalarLogical(FALSE)
);
SEXP ci_locate_last_boundaries(
    SEXP str,
    SEXP opts_brkiter=R_NilValue,
    SEXP get_length=Rf_ScalarLogical(FALSE)
);
SEXP ci_split_boundaries(SEXP str, SEXP n=Rf_ScalarInteger(-1),
    SEXP tokens_only=Rf_ScalarLogical(FALSE),
    SEXP simplify=Rf_ScalarLogical(FALSE), SEXP opts_brkiter=R_NilValue);
SEXP ci_count_boundaries(SEXP str, SEXP opts_brkiter=R_NilValue);


// SEXP ci_c_posixst(SEXP x);   // internal


// encoding_conversion.cpp:
// SEXP ci_encode_from_marked(SEXP str, SEXP to, SEXP to_raw);  // internal


// test.cpp /* internal, but in namespace: for testing */
void ci_test_init_erroring_altrep(DllInfo* dll);
SEXP ci_test_erroring_altrep(SEXP n);
SEXP ci_test_Rmark(SEXP str);
SEXP ci_test_UnicodeContainer16(SEXP str);
SEXP ci_test_UnicodeContainer16b(SEXP str);
SEXP ci_test_UnicodeContainer8(SEXP str);
SEXP ci_test_UnicodeContainer8b(SEXP str);
SEXP ci_test_UnicodeContainer8_alias(SEXP str);
SEXP ci_test_UnicodeContainer8_independent(SEXP str);
SEXP ci_test_ByteSearchContainer(SEXP pattern);
SEXP ci_test_ByteSearchContainer_error(SEXP pattern);
SEXP ci_test_ByteSearchMatcher(SEXP str, SEXP pattern, SEXP case_insensitive);
SEXP ci_test_Utf8Record_views();
SEXP ci_test_UTF8EncodingMarks();
SEXP ci_test_ListUTF8(SEXP str, SEXP nrecycle);
SEXP ci_test_returnasis(SEXP x);

#endif
