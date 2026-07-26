#ifndef CHARR_BASE_FACADE_H
#define CHARR_BASE_FACADE_H

#include <Rinternals.h>

#define CHARR_BASE_NATIVE_METHODS(X) \
    X(ci_detect_fixed, 5) \
    X(ci_startswith_fixed, 5) \
    X(ci_endswith_fixed, 5) \
    X(ci_count_fixed, 3) \
    X(ci_locate_first_fixed, 4) \
    X(ci_locate_all_fixed, 5) \
    X(ci_extract_first_fixed, 3) \
    X(ci_extract_all_fixed, 5) \
    X(ci_replace_first_fixed, 4) \
    X(ci_replace_all_fixed, 5) \
    X(ci_split_fixed, 7) \
    X(ci_sub, 6) \
    X(ci_sub_replacement, 7) \
    X(ci_sub_all, 6) \
    X(ci_sub_replacement_all, 7) \
    X(ci_length, 1) \
    X(ci_join, 4) \
    X(ci_flatten, 4) \
    X(ci_dup, 2) \
    X(ci_reverse, 1) \
    X(ci_trim_left, 3) \
    X(ci_trim_right, 3) \
    X(ci_trim_both, 3) \
    X(ci_replace_all_charclass, 5) \
    X(ci_replace_na, 2) \
    X(ci_detect_regex, 5) \
    X(ci_count_regex, 3) \
    X(ci_locate_first_regex, 5) \
    X(ci_locate_all_regex, 6) \
    X(ci_extract_first_regex, 3) \
    X(ci_extract_all_regex, 5) \
    X(ci_replace_first_regex, 4) \
    X(ci_replace_all_regex, 5) \
    X(ci_split_regex, 7) \
    X(ci_match_first_regex, 4) \
    X(ci_match_all_regex, 5) \
    X(ci_detect_coll, 5) \
    X(ci_startswith_coll, 5) \
    X(ci_endswith_coll, 5) \
    X(ci_count_coll, 3) \
    X(ci_locate_first_coll, 4) \
    X(ci_locate_all_coll, 5) \
    X(ci_extract_first_coll, 3) \
    X(ci_extract_all_coll, 5) \
    X(ci_replace_first_coll, 4) \
    X(ci_replace_all_coll, 5) \
    X(ci_split_coll, 7) \
    X(ci_order, 4) \
    X(ci_rank, 2) \
    X(ci_cmp_equiv, 3) \
    X(ci_duplicated, 3) \
    X(ci_trans_tolower, 2) \
    X(ci_trans_toupper, 2) \
    X(ci_trans_totitle, 2) \
    X(ci_trans_nfc, 1) \
    X(ci_count_boundaries, 2) \
    X(ci_locate_first_boundaries, 3) \
    X(ci_locate_all_boundaries, 4) \
    X(ci_extract_first_boundaries, 2) \
    X(ci_extract_all_boundaries, 4) \
    X(ci_split_boundaries, 5) \
    X(ci_wrap, 12) \
    X(ci_pad, 5) \
    X(ci_width, 1) \
    X(ci_escape_unicode, 1) \
    X(ci_encode, 4) \
    X(ci_enc_info, 1) \
    X(ci_read_lines, 2) \
    X(ci_split_lines, 2) \
    X(ci_split_lines1, 1)

#define CHARR_BASE_FORMALS_1 SEXP a1
#define CHARR_BASE_FORMALS_2 SEXP a1, SEXP a2
#define CHARR_BASE_FORMALS_3 SEXP a1, SEXP a2, SEXP a3
#define CHARR_BASE_FORMALS_4 SEXP a1, SEXP a2, SEXP a3, SEXP a4
#define CHARR_BASE_FORMALS_5 SEXP a1, SEXP a2, SEXP a3, SEXP a4, SEXP a5
#define CHARR_BASE_FORMALS_6 \
    SEXP a1, SEXP a2, SEXP a3, SEXP a4, SEXP a5, SEXP a6
#define CHARR_BASE_FORMALS_7 \
    SEXP a1, SEXP a2, SEXP a3, SEXP a4, SEXP a5, SEXP a6, SEXP a7
#define CHARR_BASE_FORMALS_8 \
    SEXP a1, SEXP a2, SEXP a3, SEXP a4, SEXP a5, SEXP a6, SEXP a7, SEXP a8
#define CHARR_BASE_FORMALS_9 \
    SEXP a1, SEXP a2, SEXP a3, SEXP a4, SEXP a5, SEXP a6, SEXP a7, SEXP a8, \
    SEXP a9
#define CHARR_BASE_FORMALS_10 \
    SEXP a1, SEXP a2, SEXP a3, SEXP a4, SEXP a5, SEXP a6, SEXP a7, SEXP a8, \
    SEXP a9, SEXP a10
#define CHARR_BASE_FORMALS_11 \
    SEXP a1, SEXP a2, SEXP a3, SEXP a4, SEXP a5, SEXP a6, SEXP a7, SEXP a8, \
    SEXP a9, SEXP a10, SEXP a11
#define CHARR_BASE_FORMALS_12 \
    SEXP a1, SEXP a2, SEXP a3, SEXP a4, SEXP a5, SEXP a6, SEXP a7, SEXP a8, \
    SEXP a9, SEXP a10, SEXP a11, SEXP a12

#define CHARR_BASE_DECLARE(name, arity) \
    extern "C" SEXP C_charr_base_##name(CHARR_BASE_FORMALS_##arity);
CHARR_BASE_NATIVE_METHODS(CHARR_BASE_DECLARE)
#undef CHARR_BASE_DECLARE

#endif
