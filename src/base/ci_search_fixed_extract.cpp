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
#include "ci_container_bytesearch.h"
#include "utf8_input.h"

#include <cstddef>
#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>
namespace charr { namespace base {

using namespace std;


namespace {

struct DirectExtractString {
    const char* data;
    R_len_t length;
    bool is_na;
    bool modified;
};


bool ci__direct_extract_string(SEXP value, DirectExtractString& output)
{
    if (value == NA_STRING) {
        output = DirectExtractString{NULL, 0, true, false};
        return true;
    }
    if (!IS_ASCII(value) && !IS_UTF8(value))
        return false;

    output = DirectExtractString{CHAR(value), LENGTH(value), false, false};
    if (!IS_ASCII(value) &&
            STRI__ENC_HAS_BOM_UTF8(output.data, output.length)) {
        output.data += 3;
        output.length -= 3;
        output.modified = true;
    }
    return true;
}


const char* ci__find_extract_first(
    const char* data, R_len_t length,
    const char* pattern, R_len_t pattern_length
)
{
    if (pattern_length <= 0 || length < pattern_length)
        return NULL;
    if (pattern_length == 1) {
        return static_cast<const char*>(std::memchr(
            data, static_cast<unsigned char>(pattern[0]),
            static_cast<size_t>(length)
        ));
    }

    const char* current = data;
    const char* const last = data + length - pattern_length;
    while (current <= last) {
        const size_t available = static_cast<size_t>(last-current+1);
        current = static_cast<const char*>(std::memchr(
            current, static_cast<unsigned char>(pattern[0]), available
        ));
        if (current == NULL)
            return NULL;
        if (std::memcmp(current, pattern, static_cast<size_t>(pattern_length)) == 0)
            return current;
        ++current;
    }
    return NULL;
}


const char* ci__find_extract_last(
    const char* data, R_len_t length,
    const char* pattern, R_len_t pattern_length
)
{
    if (pattern_length <= 0 || length < pattern_length)
        return NULL;
    for (R_len_t i = length-pattern_length; i >= 0; --i) {
        if (data[i] == pattern[0] &&
                std::memcmp(
                    data+i, pattern, static_cast<size_t>(pattern_length)
                ) == 0) {
            return data+i;
        }
    }
    return NULL;
}


R_len_t ci__count_extract_matches(
    const char* data, R_len_t length,
    const char* pattern, R_len_t pattern_length
)
{
    R_len_t count = 0;
    R_len_t offset = 0;
    while (offset <= length-pattern_length) {
        const char* match = ci__find_extract_first(
            data+offset, length-offset, pattern, pattern_length
        );
        if (match == NULL)
            break;
        ++count;
        offset = static_cast<R_len_t>(match-data) + pattern_length;
    }
    return count;
}


bool ci__extract_firstlast_fixed_plain(
    SEXP str, SEXP pattern, uint32_t pattern_flags,
    R_len_t vectorize_length, bool first, SEXP result
)
{
    if (pattern_flags != 0 || vectorize_length <= 0)
        return false;

    const R_len_t str_n = LENGTH(str);
    const R_len_t pattern_n = LENGTH(pattern);
    if (str_n <= 0 || pattern_n <= 0)
        return false;
    const bool direct_str_length = str_n == vectorize_length;
    const bool direct_pattern_length = pattern_n == vectorize_length;
    const SEXP* str_values = STRING_PTR_RO(str);
    const SEXP* pattern_values = STRING_PTR_RO(pattern);

    for (R_len_t i = 0; i < vectorize_length; ++i) {
        const SEXP pattern_sexp = pattern_values[
            direct_pattern_length ? i : i % pattern_n
        ];
        DirectExtractString pattern_value;
        if (!ci__direct_extract_string(pattern_sexp, pattern_value) ||
                pattern_value.modified ||
                (!pattern_value.is_na && pattern_value.length <= 0)) {
            return false;
        }

        DirectExtractString value;
        if (!ci__direct_extract_string(
                str_values[direct_str_length ? i : i % str_n], value
        )) {
            return false;
        }
        if (value.is_na || pattern_value.is_na) {
            SET_STRING_ELT(result, i, NA_STRING);
            continue;
        }

        const char* match = first
            ? ci__find_extract_first(
                value.data, value.length,
                pattern_value.data, pattern_value.length
            )
            : ci__find_extract_last(
                value.data, value.length,
                pattern_value.data, pattern_value.length
            );
        SET_STRING_ELT(
            result, i, match == NULL ? NA_STRING : pattern_sexp
        );
    }
    return true;
}


struct FixedExtractAllPlan {
    vector<SEXP> patterns;
    vector<R_len_t> counts;
    R_len_t max_columns;
};


bool ci__plan_extract_all_fixed_plain(
    SEXP str, SEXP pattern, uint32_t pattern_flags,
    R_len_t vectorize_length, bool omit_no_match,
    FixedExtractAllPlan& plan, R_len_t& general_start
)
{
    if (pattern_flags != 0 || vectorize_length <= 0)
        return false;

    const R_len_t str_n = LENGTH(str);
    const R_len_t pattern_n = LENGTH(pattern);
    if (str_n <= 0 || pattern_n <= 0)
        return false;
    const bool direct_str_length = str_n == vectorize_length;
    const bool direct_pattern_length = pattern_n == vectorize_length;
    const SEXP* str_values = STRING_PTR_RO(str);
    const SEXP* pattern_values = STRING_PTR_RO(pattern);

    plan.patterns.assign(
        static_cast<size_t>(vectorize_length), R_NilValue
    );
    plan.counts.assign(static_cast<size_t>(vectorize_length), 0);
    plan.max_columns = 0;
    for (R_len_t i = 0; i < vectorize_length; ++i) {
        const SEXP pattern_sexp = pattern_values[
            direct_pattern_length ? i : i % pattern_n
        ];
        DirectExtractString pattern_value;
        if (!ci__direct_extract_string(pattern_sexp, pattern_value) ||
                pattern_value.modified ||
                (!pattern_value.is_na && pattern_value.length <= 0)) {
            return false;
        }

        DirectExtractString value;
        if (!ci__direct_extract_string(
                str_values[direct_str_length ? i : i % str_n], value
        )) {
            if (pattern_n == 1 && i > 0)
                general_start = i;
            return false;
        }

        const R_len_t count = value.is_na || pattern_value.is_na
            ? NA_INTEGER
            : ci__count_extract_matches(
                value.data, value.length,
                pattern_value.data, pattern_value.length
            );
        plan.patterns[static_cast<size_t>(i)] = pattern_sexp;
        plan.counts[static_cast<size_t>(i)] = count;
        const R_len_t width = count == NA_INTEGER ||
                (count == 0 && !omit_no_match)
            ? 1
            : count;
        if (width > plan.max_columns)
            plan.max_columns = width;
    }
    return true;
}


struct FixedExtractChildKey {
    SEXP pattern;
    R_len_t count;

    bool operator==(const FixedExtractChildKey& other) const noexcept
    {
        return pattern == other.pattern && count == other.count;
    }
};


struct FixedExtractChildHash {
    size_t operator()(const FixedExtractChildKey& value) const noexcept
    {
        const size_t pointer = reinterpret_cast<size_t>(value.pattern);
        return (pointer >> 3) ^
            (static_cast<size_t>(value.count) * static_cast<size_t>(0x9e3779b1U));
    }
};


SEXP ci__build_extract_all_fixed_plain(
    const FixedExtractAllPlan& plan, R_len_t vectorize_length,
    int simplify, bool omit_no_match
)
{
    const vector<R_len_t>& counts = plan.counts;
    const R_len_t max_columns = plan.max_columns;

    if (simplify != NA_LOGICAL && !simplify) {
        // Exact fixed matches reproduce the direct pattern bytes. Equal
        // (pattern, count) signatures therefore produce byte-for-byte
        // identical immutable children, which may be shared safely under R's
        // copy-on-modify rules.
        unordered_map<FixedExtractChildKey, SEXP, FixedExtractChildHash>
            children;
        children.reserve(static_cast<size_t>(
            vectorize_length < 1024 ? vectorize_length : 1024
        ));
        return unwind_protect([&]() -> SEXP {
            SEXP result = PROTECT(
                Rf_allocVector(VECSXP, vectorize_length)
            );
            try {
                SEXP missing_child = R_NilValue;
                SEXP empty_child = R_NilValue;
                for (R_len_t i = 0; i < vectorize_length; ++i) {
                    const R_len_t count = counts[static_cast<size_t>(i)];
                    const bool forced_na = count == NA_INTEGER ||
                        (count == 0 && !omit_no_match);
                    if (forced_na) {
                        if (missing_child == R_NilValue) {
                            missing_child = Rf_allocVector(STRSXP, 1);
                            SET_STRING_ELT(
                                missing_child, 0, NA_STRING
                            );
                        }
                        SET_VECTOR_ELT(result, i, missing_child);
                    }
                    else if (count == 0) {
                        if (empty_child == R_NilValue)
                            empty_child = Rf_allocVector(STRSXP, 0);
                        SET_VECTOR_ELT(result, i, empty_child);
                    }
                    else {
                        const FixedExtractChildKey key{
                            plan.patterns[static_cast<size_t>(i)], count
                        };
                        const auto found = children.find(key);
                        SEXP child;
                        if (found == children.end()) {
                            child = Rf_allocVector(STRSXP, count);
                            for (R_len_t j = 0; j < count; ++j)
                                SET_STRING_ELT(child, j, key.pattern);
                            SET_VECTOR_ELT(result, i, child);
                            children.emplace(key, child);
                            continue;
                        }
                        SET_VECTOR_ELT(result, i, found->second);
                    }
                }
            }
            catch (...) {
                UNPROTECT(1);
                throw;
            }
            UNPROTECT(1);
            return result;
        });
    }

    return unwind_protect([&]() -> SEXP {
        SEXP result = Rf_allocMatrix(
            STRSXP, vectorize_length, max_columns
        );
        for (R_len_t i = 0; i < vectorize_length; ++i) {
            const R_len_t count = counts[static_cast<size_t>(i)];
            const bool forced_na = count == NA_INTEGER ||
                (count == 0 && !omit_no_match);
            R_len_t j = 0;
            if (count > 0) {
                for (; j < count; ++j) {
                    SET_STRING_ELT(
                        result, i + j * vectorize_length,
                        plan.patterns[static_cast<size_t>(i)]
                    );
                }
            }
            for (; j < max_columns; ++j) {
                SET_STRING_ELT(
                    result, i + j * vectorize_length,
                    simplify == NA_LOGICAL || (forced_na && j == 0)
                        ? NA_STRING
                        : R_BlankString
                );
            }
        }
        return result;
    });
}

} // namespace


/**
 * Extract first or last occurrences of pattern in a string [exact byte search]
 *
 * @param str character vector
 * @param pattern character vector
 * @param first looking for first or last match?
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-24)
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          ci_extract_fixed now uses byte search only
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-08)
 *          new args: opts_fixed
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-02-14)
 *    use StriByteSearchMatcher
 */
SEXP ci__extract_firstlast_fixed(SEXP str, SEXP pattern, SEXP opts_fixed, bool first)
{
    uint32_t pattern_flags = StriContainerByteSearch::getByteSearchFlags(opts_fixed);
    PROTECT(str = ci__prepare_arg_string(str, "str")); // prepare string argument
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern")); // prepare string argument

    STRI__ERROR_HANDLER_BEGIN(2)
    int vectorize_length = ci__recycling_rule(true, 2, LENGTH(str), LENGTH(pattern));

    SEXP ret;
    // The direct path installs the existing pattern CHARSXP. Allocate the
    // final R vector before either path so fallback matches are interned as
    // soon as they are ready too.
    STRI__PROTECT(ret = Rf_allocVector(STRSXP, vectorize_length));

    if (!ci__extract_firstlast_fixed_plain(
            str, pattern, pattern_flags, vectorize_length, first, ret
    )) {
        Utf8Input str_cont(str, vectorize_length);
        StriContainerByteSearch pattern_cont(
            pattern, vectorize_length, pattern_flags
        );
        unwind_protect([&]() -> SEXP {
            for (R_len_t i = pattern_cont.vectorize_init();
                    i != pattern_cont.vectorize_end();
                    i = pattern_cont.vectorize_next(i))
            {
                STRI__CONTINUE_ON_EMPTY_OR_NA_STR_PATTERN(
                    str_cont, pattern_cont,
                    SET_STRING_ELT(ret, i, NA_STRING);,
                    SET_STRING_ELT(ret, i, NA_STRING);
                )

                StriByteSearchMatcher* matcher =
                    pattern_cont.getMatcher(i);
                matcher->reset(
                    str_cont.get(i).data(), str_cont.get(i).length()
                );
                const int start = first
                    ? matcher->findFirst()
                    : matcher->findLast();
                if (start == USEARCH_DONE) {
                    SET_STRING_ELT(ret, i, NA_STRING);
                    continue;
                }

                const int length = matcher->getMatchedLength();
                SET_STRING_ELT(
                    ret, i,
                    Rf_mkCharLenCE(
                        str_cont.get(i).data()+start, length, CE_UTF8
                    )
                );
            }
            return R_NilValue;
        });
    }

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END({ /* no-op */ })
}


/**
 * Extract first occurrence of a fixed pattern in each string
 *
 * @param str character vector
 * @param pattern character vector
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-24)
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          ci_extract_fixed now uses byte search only
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-08)
 *          new args: opts_fixed
 */
SEXP ci_extract_first_fixed(SEXP str, SEXP pattern, SEXP opts_fixed)
{
    return ci__extract_firstlast_fixed(str, pattern, opts_fixed, true);
}


/**
 * Extract last occurrence of a fixed pattern in each string
 *
 * @param str character vector
 * @param pattern character vector
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-24)
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          ci_extract_fixed now uses byte search only
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-08)
 *          new args: opts_fixed
 */
SEXP ci_extract_last_fixed(SEXP str, SEXP pattern, SEXP opts_fixed)
{
    return ci__extract_firstlast_fixed(str, pattern, opts_fixed, false);
}


/**
 * Extract all occurrences of pattern in a string [exact byte search]
 *
 * @param str character vector
 * @param pattern character vector
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-24)
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          ci_extract_fixed now uses byte search only
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-08)
 *          new args: opts_fixed, omit_no_match, simplify
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-02-14)
 *    use StriByteSearchMatcher
 */
SEXP ci_extract_all_fixed(SEXP str, SEXP pattern, SEXP simplify, SEXP omit_no_match, SEXP opts_fixed)
{
    uint32_t pattern_flags = StriContainerByteSearch::getByteSearchFlags(opts_fixed, /*allow_overlap*/true);
    bool omit_no_match1 = ci__prepare_arg_logical_1_notNA(omit_no_match, "omit_no_match");
    PROTECT(simplify = ci__prepare_arg_logical_1(simplify, "simplify"));
    PROTECT(str = ci__prepare_arg_string(str, "str")); // prepare string argument
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern")); // prepare string argument

    STRI__ERROR_HANDLER_BEGIN(3)
    SEXP ret;
    try {
        const int simplify1 = LOGICAL(simplify)[0];
        const R_len_t vectorize_length = ci__recycling_rule(
            true, 2, LENGTH(str), LENGTH(pattern)
        );

        FixedExtractAllPlan direct_plan;
        R_len_t general_start = 0;
        if (ci__plan_extract_all_fixed_plain(
                str, pattern, pattern_flags, vectorize_length,
                omit_no_match1, direct_plan, general_start
        )) {
            STRI__PROTECT(
                ret = ci__build_extract_all_fixed_plain(
                    direct_plan, vectorize_length, simplify1,
                    omit_no_match1
                )
            );
            STRI__UNPROTECT_ALL
            return ret;
        }

        struct MatchSlice {
            const char* data;
            R_len_t length;
        };
        struct RowResult {
            size_t begin;
            size_t count;
            bool forced_na;
        };

        vector<MatchSlice> matches;
        vector<RowResult> rows(
            static_cast<size_t>(vectorize_length),
            RowResult{0, 0, false}
        );
        unique_ptr<Utf8Input> str_input;

        if (vectorize_length > 0) {
            str_input = make_unique<Utf8Input>(str, vectorize_length);
            StriContainerByteSearch pattern_cont(
                pattern, vectorize_length, pattern_flags
            );

            for (R_len_t i = 0; i < general_start; ++i) {
                RowResult& row = rows[static_cast<size_t>(i)];
                row.begin = matches.size();
                const R_len_t count = direct_plan.counts[
                    static_cast<size_t>(i)
                ];
                row.forced_na = count == NA_INTEGER ||
                    (count == 0 && !omit_no_match1);
                if (count > 0) {
                    // The planner already extracted this pattern to reach a
                    // positive count, so the repeat cannot fail; the result
                    // is a view into the pattern CHARSXP, which `pattern`
                    // keeps protected for the whole call.
                    DirectExtractString pattern_value;
                    const bool extracted = ci__direct_extract_string(
                        direct_plan.patterns[static_cast<size_t>(i)],
                        pattern_value
                    );
                    if (!extracted)
                        throw logic_error("extract plan pattern is unusable");
                    for (R_len_t j = 0; j < count; ++j) {
                        matches.push_back(MatchSlice{
                            pattern_value.data,
                            pattern_value.length
                        });
                    }
                }
                row.count = matches.size() - row.begin;
            }

            // general_start is set only for a scalar pattern, so the pattern
            // container advances with a unit stride from that index.
            for (R_len_t i = general_start > 0
                        ? general_start
                        : pattern_cont.vectorize_init();
                    i != pattern_cont.vectorize_end();
                    i = pattern_cont.vectorize_next(i))
            {
                RowResult& row = rows[static_cast<size_t>(i)];
                row.begin = matches.size();

                if (str_input->is_na(i) || pattern_cont.isNA(i) ||
                        pattern_cont.get(i).length() <= 0) {
                    row.forced_na = true;
                    continue;
                }
                const Utf8Record subject = str_input->text(i);
                if (subject.len <= 0) {
                    row.forced_na = !omit_no_match1;
                    continue;
                }

                StriByteSearchMatcher* matcher = pattern_cont.getMatcher(i);
                matcher->reset(subject.ptr, subject.len);
                for (int start = matcher->findFirst();
                        start != USEARCH_DONE;
                        start = matcher->findNext()) {
                    matches.push_back(MatchSlice{
                        subject.ptr + start,
                        matcher->getMatchedLength()
                    });
                }
                row.count = matches.size() - row.begin;
                if (row.count == 0)
                    row.forced_na = !omit_no_match1;
            }
        }

        if (simplify1 != NA_LOGICAL && !simplify1) {
            STRI__PROTECT(ret = unwind_protect([&]() -> SEXP {
                SEXP output = PROTECT(
                    Rf_allocVector(VECSXP, vectorize_length)
                );
                for (R_len_t i = 0; i < vectorize_length; ++i) {
                    const RowResult& row = rows[static_cast<size_t>(i)];
                    const R_xlen_t current_size = row.forced_na
                        ? 1
                        : static_cast<R_xlen_t>(row.count);
                    SEXP current = PROTECT(
                        Rf_allocVector(STRSXP, current_size)
                    );
                    if (row.forced_na)
                        SET_STRING_ELT(current, 0, NA_STRING);
                    else {
                        for (size_t j = 0; j < row.count; ++j) {
                            const MatchSlice& match =
                                matches[row.begin + j];
                            SET_STRING_ELT(
                                current, static_cast<R_xlen_t>(j),
                                Rf_mkCharLenCE(
                                    match.data, match.length, CE_UTF8
                                )
                            );
                        }
                    }
                    SET_VECTOR_ELT(output, i, current);
                    UNPROTECT(1);
                }
                UNPROTECT(1);
                return output;
            }));
        }
        else {
            size_t max_columns = 0;
            for (R_len_t i = 0; i < vectorize_length; ++i) {
                const RowResult& row = rows[static_cast<size_t>(i)];
                const size_t width = row.forced_na ? 1 : row.count;
                if (width > max_columns)
                    max_columns = width;
            }
            if (max_columns > static_cast<size_t>(R_LEN_T_MAX))
                throw length_error("matrix columns exceed R's integer limit");

            const R_xlen_t matrix_rows = vectorize_length;
            const R_xlen_t matrix_columns =
                static_cast<R_xlen_t>(max_columns);
            if (matrix_rows > 0 &&
                    matrix_columns > R_XLEN_T_MAX / matrix_rows) {
                throw length_error("matrix length exceeds R's vector limit");
            }

            STRI__PROTECT(ret = unwind_protect([&]() -> SEXP {
                SEXP output = PROTECT(Rf_allocVector(
                    STRSXP, matrix_rows * matrix_columns
                ));
                for (R_xlen_t i = 0; i < matrix_rows; ++i) {
                    const RowResult& row = rows[static_cast<size_t>(i)];
                    R_xlen_t j = 0;
                    if (row.forced_na) {
                        SET_STRING_ELT(output, i, NA_STRING);
                        j = 1;
                    }
                    else {
                        for (; j < static_cast<R_xlen_t>(row.count); ++j) {
                            const MatchSlice& match = matches[
                                row.begin + static_cast<size_t>(j)
                            ];
                            SET_STRING_ELT(
                                output, i + j * matrix_rows,
                                Rf_mkCharLenCE(
                                    match.data, match.length, CE_UTF8
                                )
                            );
                        }
                    }
                    for (; j < matrix_columns; ++j) {
                        SET_STRING_ELT(
                            output, i + j * matrix_rows,
                            simplify1 == NA_LOGICAL
                                ? NA_STRING
                                : R_BlankString
                        );
                    }
                }

                SEXP dim = PROTECT(Rf_allocVector(INTSXP, 2));
                INTEGER(dim)[0] = vectorize_length;
                INTEGER(dim)[1] =
                    static_cast<R_len_t>(max_columns);
                Rf_setAttrib(output, R_DimSymbol, dim);
                UNPROTECT(2);
                return output;
            }));
        }
    }
    catch (const StriException&) {
        throw;
    }
    catch (const std::exception& e) {
        throw StriException("%s", e.what());
    }

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END({/* no-op */})
}

} } // namespace charr::base
