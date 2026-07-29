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
#include "ci_utf8.h"
#include "fixed/pattern_set.h"
#include <cstring>
#include <deque>
#include <utility>

namespace charr { namespace altrep_backend {
using namespace std;


namespace search_fixed_locate {

// A scalar ASCII byte can be searched in borrowed records without constructing
// a matcher. UTF-8 positions are recovered only for matches.
struct DirectFixedString {
    const char* data;
    R_len_t length;
    bool is_na;
    bool is_ascii;
};


bool ci__direct_fixed_string(
    const charport::StrView& value, DirectFixedString& output
) noexcept
{
    if (value.is_na()) {
        output.data = NULL;
        output.length = 0;
        output.is_na = true;
        output.is_ascii = false;
        return true;
    }

    if (value.enc != cetype_ext_t::CE_ASCII &&
            value.enc != cetype_ext_t::CE_UTF8 &&
            value.enc != cetype_ext_t::CE_ASCII_OR_UTF8)
        return false;

    output.data = value.ptr;
    output.length = value.len;
    output.is_na = false;
    output.is_ascii = value.enc == cetype_ext_t::CE_ASCII;

    if (!output.is_ascii &&
            STRI__ENC_HAS_BOM_UTF8(output.data, output.length)) {
        output.data += 3;
        output.length -= 3;
    }

    return true;
}


bool ci__direct_fixed_pattern(
    const charport::StrView& pattern, unsigned char& pattern_byte
) noexcept
{
    DirectFixedString value;
    if (!ci__direct_fixed_string(pattern, value) ||
            value.is_na || value.length != 1)
        return false;

    pattern_byte = static_cast<unsigned char>(value.data[0]);
    return pattern_byte <= 0x7f;
}


R_len_t ci__count_fixed_byte(
    const char* data, R_len_t length, unsigned char pattern_byte
) noexcept
{
    R_len_t count = 0;
    const unsigned char* current =
        reinterpret_cast<const unsigned char*>(data);
    const unsigned char* end = current + length;
    for (; current != end; ++current)
        count += (*current == pattern_byte);
    return count;
}


R_len_t ci__fixed_codepoint_position(
    const DirectFixedString& value, R_len_t byte_position
) noexcept
{
    if (value.is_ascii)
        return byte_position + 1;

    R_len_t current = 0;
    R_len_t codepoint = 1;
    while (current < byte_position) {
        U8_FWD_1(
            reinterpret_cast<const uint8_t*>(value.data),
            current, value.length
        );
        ++codepoint;
    }
    return codepoint;
}


void ci__fill_fixed_byte_occurrences(
    const DirectFixedString& value, unsigned char pattern_byte,
    int* start, int* end, R_len_t occurrence_count, bool get_length
) noexcept
{
    R_len_t occurrence = 0;
    if (value.is_ascii) {
        for (R_len_t i = 0; i < value.length; ++i) {
            if (static_cast<unsigned char>(value.data[i]) != pattern_byte)
                continue;
            start[occurrence] = i + 1;
            end[occurrence] = get_length ? 1 : i + 1;
            ++occurrence;
        }
        return;
    }

    R_len_t current = 0;
    R_len_t codepoint = 1;
    while (current < value.length && occurrence < occurrence_count) {
        if (static_cast<unsigned char>(value.data[current]) == pattern_byte) {
            start[occurrence] = codepoint;
            end[occurrence] = get_length ? 1 : codepoint;
            ++occurrence;
        }
        U8_FWD_1(
            reinterpret_cast<const uint8_t*>(value.data),
            current, value.length
        );
        ++codepoint;
    }
}


bool ci__locate_firstlast_fixed_byte(
    const charport::StrViews& strings,
    const charport::StrView& pattern, uint32_t pattern_flags,
    R_len_t vectorize_length, bool first, bool get_length,
    int* result, R_len_t& general_start
)
{
    if (pattern_flags != 0)
        return false;

    unsigned char pattern_byte;
    if (!ci__direct_fixed_pattern(pattern, pattern_byte))
        return false;

    for (R_len_t i = 0; i < vectorize_length; ++i) {
        int& start_result = result[i];
        int& end_result = result[i+vectorize_length];
        start_result = NA_INTEGER;
        end_result = NA_INTEGER;

        DirectFixedString value;
        if (!ci__direct_fixed_string(strings[i], value)) {
            general_start = i;
            return false;
        }
        if (value.is_na)
            continue;

        if (first && !value.is_ascii) {
            R_len_t current = 0;
            R_len_t codepoint = 1;
            while (current < value.length) {
                if (static_cast<unsigned char>(value.data[current]) ==
                        pattern_byte) {
                    start_result = codepoint;
                    end_result = get_length ? 1 : codepoint;
                    break;
                }
                U8_FWD_1(
                    reinterpret_cast<const uint8_t*>(value.data),
                    current, value.length
                );
                ++codepoint;
            }
            if (start_result == NA_INTEGER && get_length)
                start_result = end_result = -1;
            continue;
        }

        const char* found = NULL;
        if (first) {
            found = static_cast<const char*>(std::memchr(
                value.data, pattern_byte,
                static_cast<std::size_t>(value.length)
            ));
        }
        else {
            for (R_len_t j = value.length; j > 0; --j) {
                if (static_cast<unsigned char>(value.data[j-1]) ==
                        pattern_byte) {
                    found = value.data + j - 1;
                    break;
                }
            }
        }

        if (found == NULL) {
            if (get_length)
                start_result = end_result = -1;
            continue;
        }

        const R_len_t byte_position = static_cast<R_len_t>(
            found - value.data
        );
        start_result = ci__fixed_codepoint_position(value, byte_position);
        end_result = get_length ? 1 : start_result;
    }

    return true;
}


SEXP ci__fixed_no_match_matrix(
    bool argument_na, bool omit_no_match, bool get_length
)
{
    const R_len_t rows = argument_na ? 1 : (omit_no_match ? 0 : 1);
    SEXP result = Rf_allocMatrix(INTSXP, rows, 2);
    int* values = INTEGER(result);
    const int fill = !argument_na && get_length ? -1 : NA_INTEGER;
    for (R_len_t i = 0; i < rows*2; ++i)
        values[i] = fill;
    return result;
}


bool ci__locate_all_fixed_byte(
    const charport::StrViews& strings,
    const charport::StrView& pattern, uint32_t pattern_flags,
    R_len_t vectorize_length, bool omit_no_match, bool get_length,
    SEXP result, R_len_t& general_start
)
{
    if (pattern_flags != 0)
        return false;

    unsigned char pattern_byte;
    if (!ci__direct_fixed_pattern(pattern, pattern_byte))
        return false;

    // Count each record first, allocate its final matrix, and then fill it;
    // this replaces stringi's deque of temporary match pairs.
    for (R_len_t i = 0; i < vectorize_length; ++i) {
        DirectFixedString value;
        if (!ci__direct_fixed_string(strings[i], value)) {
            general_start = i;
            return false;
        }
        if (value.is_na) {
            SET_VECTOR_ELT(
                result, i,
                ci__fixed_no_match_matrix(true, omit_no_match, get_length)
            );
            continue;
        }

        const R_len_t occurrence_count = ci__count_fixed_byte(
            value.data, value.length, pattern_byte
        );
        if (occurrence_count == 0) {
            SET_VECTOR_ELT(
                result, i,
                ci__fixed_no_match_matrix(false, omit_no_match, get_length)
            );
            continue;
        }

        SEXP answer = Rf_allocMatrix(INTSXP, occurrence_count, 2);
        int* answer_data = INTEGER(answer);
        ci__fill_fixed_byte_occurrences(
            value, pattern_byte, answer_data,
            answer_data+occurrence_count, occurrence_count, get_length
        );
        SET_VECTOR_ELT(result, i, answer);
    }

    return true;
}

} // namespace search_fixed_locate

using namespace search_fixed_locate;


/**
 * Locate first or last occurrences of a pattern in a string
 *
 * @param str character vector
 * @param pattern character vector
 * @param first looking for first or last match?
 * @return integer matrix (2 columns)
 *
 * @version 0.1-?? (Bartlomiej Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-23)
 *          StriException friendly, use fixed::PatternSet
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-20)
 *          Use io::IndexedUtf8Input
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          ci_locate_fixed now uses byte search only
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-07)
 *    FR #110, #23: opts_fixed arg added
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-02-14)
 *    use shared::ByteSearchMatcher
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29)
 *     get_length
 */
SEXP ci__locate_firstlast_fixed(SEXP str, SEXP pattern, SEXP opts_fixed, bool first, bool get_length1)
{
    uint32_t pattern_flags = fixed::PatternSet::getByteSearchFlags(opts_fixed);
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));

    STRI__ERROR_HANDLER_BEGIN(2)
    SEXP ret;
    {
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    R_len_t str_n = ci::checked_r_len(
        context.size(str), "character vectors"
    );
    R_len_t pattern_n = ci::checked_r_len(
        context.size(pattern), "character vectors"
    );
    R_len_t vectorize_length = 0;
    ci::unwind_protect([&]() -> SEXP {
        vectorize_length = ci__recycling_rule(
            STRI__DEFERRED_WARNINGS, 2, str_n, pattern_n
        );
        return R_NilValue;
    });

    STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
        return Rf_allocMatrix(INTSXP, vectorize_length, 2);
    }));
    int* ret_tab = INTEGER(ret);

    {
        bool direct = false;
        R_len_t general_start = 0;
        std::shared_ptr<ci::ReaderBorrow> str_borrow;
        std::shared_ptr<ci::ReaderBorrow> pattern_borrow;
        if (pattern_flags == 0 && pattern_n == 1) {
            str_borrow = context.acquire(str);
            pattern_borrow = context.acquire(pattern);
            direct = ci__locate_firstlast_fixed_byte(
                str_borrow->views(), pattern_borrow->views()[0],
                pattern_flags, vectorize_length,
                first, get_length1, ret_tab, general_start
            );
        }

        if (!direct) {
            io::IndexedUtf8Input str_cont(
                context, str, vectorize_length
            );
            fixed::PatternSet pattern_cont(
                context, pattern, vectorize_length, pattern_flags
            );

            for (R_len_t i = general_start > 0 ?
                        general_start : pattern_cont.vectorize_init();
                    i != pattern_cont.vectorize_end();
                    i = pattern_cont.vectorize_next(i))
            {
                ret_tab[i]                  = NA_INTEGER;
                ret_tab[i+vectorize_length] = NA_INTEGER;
                STRI__CONTINUE_ON_EMPTY_OR_NA_STR_PATTERN(
                    str_cont, pattern_cont,
                    ;/*nothing on NA - keep NA_INTEGER*/,
                    { if (get_length1) ret_tab[i] = ret_tab[i+vectorize_length] = -1; }
                )

                shared::ByteSearchMatcher* matcher = pattern_cont.getMatcher(i);
                matcher->reset(str_cont.get(i).data(), str_cont.get(i).length());
                int start;
                if (first) {
                    start = matcher->find_first();
                } else {
                    start = matcher->find_last();
                }

                if (start != shared::ByteSearchMatcher::not_found) {  // there is a match
                    ret_tab[i]                  = start;
                    ret_tab[i+vectorize_length] = start+matcher->matched_length();

                    // Adjust UTF8 byte index -> UChar32 index
                    str_cont.UTF8_to_UChar32_index(i,
                                                   ret_tab+i, ret_tab+i+vectorize_length, 1,
                                                   1, // 0-based index -> 1-based
                                                   0  // end returns position of next character after match
                                                  );

                    if (get_length1) ret_tab[i+vectorize_length] -= ret_tab[i] - 1;  // to->length
                }
                else if (get_length1) {
                    // not found
                    ret_tab[i+vectorize_length] = ret_tab[i] = -1;
                }
                // else NA_INTEGER already
            }
        }
    }

    }
    ci::unwind_protect([&]() -> SEXP {
        ci__locate_set_dimnames_matrix(ret, get_length1);
        return R_NilValue;
    });
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END( ;/* do nothing special on error */ )
}


/**
 * Locate first occurrences of pattern in a string [fixed pattern]
 *
 * @param str character vector
 * @param pattern character vector
 * @return integer matrix (2 columns)
 *
 * @version 0.1-?? (Bartlomiej Tartanus)
 *
 * @version 0.1-?? (Bartlomiej Tartanus, 2013-06-09)
 *          io::Utf16Input & collator
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-23)
 *          use ci_locate_firstlast_fixed
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          ci_locate_fixed now uses byte search only
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-07)
 *    FR #110, #23: opts_fixed arg added
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29)
 *     get_length
 */
SEXP ci_locate_first_fixed(SEXP str, SEXP pattern, SEXP opts_fixed, SEXP get_length)
{
    bool get_length1 = ci__prepare_arg_logical_1_notNA(get_length, "get_length");
    return ci__locate_firstlast_fixed(str, pattern, opts_fixed, true, get_length1);
}


/** Locate all occurrences of fixed-byte pattern
 *
 * @param str character vector
 * @param pattern character vector
 * @return list of integer matrices (2 columns)
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-23)
 *          StriException friendly, use fixed::PatternSet
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-20)
 *          Use io::IndexedUtf8Input
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          ci_locate_fixed now uses byte search only
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-27)
 *    #117: omit_no_match arg added
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-07)
 *    #110, #23: opts_fixed arg added
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-02-14)
 *    use shared::ByteSearchMatcher
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29)
 *     get_length
 */
SEXP ci_locate_all_fixed(SEXP str, SEXP pattern, SEXP omit_no_match, SEXP opts_fixed, SEXP get_length)
{
    uint32_t pattern_flags = fixed::PatternSet::getByteSearchFlags(opts_fixed, /*allow_overlap*/true);
    bool omit_no_match1 = ci__prepare_arg_logical_1_notNA(omit_no_match, "omit_no_match");
    bool get_length1 = ci__prepare_arg_logical_1_notNA(get_length, "get_length");
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));

    STRI__ERROR_HANDLER_BEGIN(2)
    SEXP ret;
    {
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    R_len_t str_n = ci::checked_r_len(
        context.size(str), "character vectors"
    );
    R_len_t pattern_n = ci::checked_r_len(
        context.size(pattern), "character vectors"
    );
    R_len_t vectorize_length = 0;
    ci::unwind_protect([&]() -> SEXP {
        vectorize_length = ci__recycling_rule(
            STRI__DEFERRED_WARNINGS, 2, str_n, pattern_n
        );
        return R_NilValue;
    });

    STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
        return Rf_allocVector(VECSXP, vectorize_length);
    }));

    {
        bool direct = false;
        R_len_t general_start = 0;
        std::shared_ptr<ci::ReaderBorrow> str_borrow;
        std::shared_ptr<ci::ReaderBorrow> pattern_borrow;
        if (pattern_flags == 0 && pattern_n == 1) {
            str_borrow = context.acquire(str);
            pattern_borrow = context.acquire(pattern);
            ci::unwind_protect([&]() -> SEXP {
                direct = ci__locate_all_fixed_byte(
                    str_borrow->views(), pattern_borrow->views()[0],
                    pattern_flags, vectorize_length,
                    omit_no_match1, get_length1, ret, general_start
                );
                return R_NilValue;
            });
        }

        if (!direct) {
            io::IndexedUtf8Input str_cont(
                context, str, vectorize_length
            );
            fixed::PatternSet pattern_cont(
                context, pattern, vectorize_length, pattern_flags
            );
            deque< pair<R_len_t, R_len_t> > occurrences;

            // Deviation from stringi: one loop-level unwind bridge protects every
            // child allocation and list write while the Reader and search owners
            // are live. The reusable scratch deque lives outside the callback, so
            // it is destroyed on an R error without paying one bridge per child.
            ci::unwind_protect([&]() -> SEXP {
              for (R_len_t i = general_start > 0 ?
                          general_start : pattern_cont.vectorize_init();
                      i != pattern_cont.vectorize_end();
                      i = pattern_cont.vectorize_next(i))
              {
                ci::UnwindCallbackProtector protector;
                occurrences.clear();
                STRI__CONTINUE_ON_EMPTY_OR_NA_STR_PATTERN(
                    str_cont, pattern_cont,
                    {
                        SEXP ans;
                        ans = protector.hold(ci__matrix_NA_INTEGER(1, 2));
                        SET_VECTOR_ELT(ret, i, ans);
                    },
                    {
                        SEXP ans;
                        ans = protector.hold(ci__matrix_NA_INTEGER(
                            omit_no_match1?0:1, 2,
                            get_length1?-1:NA_INTEGER
                        ));
                        SET_VECTOR_ELT(ret, i, ans);
                    }
                )

                shared::ByteSearchMatcher* matcher = pattern_cont.getMatcher(i);
                matcher->reset(str_cont.get(i).data(), str_cont.get(i).length());

                int start = matcher->find_first();
                if (start == shared::ByteSearchMatcher::not_found) { // no matches at all
                    SEXP ans;
                    ans = protector.hold(ci__matrix_NA_INTEGER(
                        omit_no_match1?0:1, 2,
                        get_length1?-1:NA_INTEGER
                    ));
                    SET_VECTOR_ELT(ret, i, ans);
                    continue;
                }

                while (start != shared::ByteSearchMatcher::not_found) {
                    occurrences.push_back(pair<R_len_t, R_len_t>(
                        start, start+matcher->matched_length()
                    ));
                    start = matcher->find_next();
                }

                R_len_t noccurrences = static_cast<R_len_t>(
                    occurrences.size()
                );
                SEXP ans;
                ans = protector.hold(Rf_allocMatrix(
                    INTSXP, noccurrences, 2
                ));
                int* ans_tab = INTEGER(ans);
                for (R_len_t j=0; j < noccurrences; ++j) {
                    ans_tab[j] = occurrences[j].first;
                    ans_tab[j+noccurrences] = occurrences[j].second;
                }

                // Adjust UChar index -> UChar32 index (1-2 byte UTF16 to 1 byte UTF32-code points)
                str_cont.UTF8_to_UChar32_index(i, ans_tab,
                                               ans_tab+noccurrences, noccurrences,
                                               1, // 0-based index -> 1-based
                                               0  // end returns position of next character after match
                                              );

                if (get_length1) {
                    for (R_len_t j=0; j < noccurrences; ++j)
                        ans_tab[j+noccurrences] -= ans_tab[j] - 1;  // to->length
                }

                SET_VECTOR_ELT(ret, i, ans);
              }

              return R_NilValue;
            });
        }
    }
    }
    ci::unwind_protect([&]() -> SEXP {
        ci__locate_set_dimnames_list(ret, get_length1);
        return R_NilValue;
    });
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END( ;/* do nothing special on error */ )
}

} } // namespace charr::altrep_backend
