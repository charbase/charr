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
#include "ci_builder.h"
#include "ci_utf8.h"
#include "ci_container_bytesearch.h"
#include "altrep/utf8_input.h"
#include "altrep/utf8_output.h"

#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
using namespace std;


namespace {

bool ci__direct_extract_string(const charport::StrView& value) noexcept
{
    if (value.is_na())
        return true;
    return value.len >= 0 && value.ptr != NULL &&
        (value.enc == cetype_ext_t::CE_ASCII ||
         value.enc == cetype_ext_t::CE_UTF8 ||
         value.enc == cetype_ext_t::CE_ASCII_OR_UTF8);
}


bool ci__direct_extract_view(
    const charport::StrView& value,
    charport::StrView& output, bool& modified
) noexcept
{
    if (!ci__direct_extract_string(value))
        return false;
    output = value;
    modified = false;
    if (!output.is_na() && output.enc != cetype_ext_t::CE_ASCII &&
            STRI__ENC_HAS_BOM_UTF8(output.ptr, output.len)) {
        output.ptr += 3;
        output.len -= 3;
        modified = true;
    }
    return true;
}


bool ci__direct_extract_pattern(
    const charport::StrView& pattern, unsigned char& pattern_byte
) noexcept
{
    if (!ci__direct_extract_string(pattern) ||
            pattern.is_na() || pattern.len != 1)
        return false;
    pattern_byte = static_cast<unsigned char>(pattern.ptr[0]);
    return pattern_byte <= 0x7fU;
}


R_len_t ci__count_extract_byte(
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


charr::altrep::OutputStore ci__repeated_extract_store(
    R_len_t size, unsigned char pattern_byte
)
{
    charr::altrep::OutputStore output(
        static_cast<size_t>(size), static_cast<size_t>(size)
    );
    if (size <= 0)
        return output;

    char* payload = output.slices.front_data();
    for (R_len_t i = 0; i < size; ++i) {
        payload[i] = static_cast<char>(pattern_byte);
        output.records.set(
            static_cast<size_t>(i), payload + i, 1,
            cetype_ext_t::CE_ASCII
        );
    }
    return output;
}


bool ci__extract_firstlast_fixed_plain(
    const charport::StrViews& strings,
    const charport::StrViews& patterns, uint32_t pattern_flags,
    R_len_t vectorize_length, bool first,
    charport::charvec::Builder& result
)
{
    if (pattern_flags != 0 || vectorize_length <= 0 ||
            strings.size() <= 0 || patterns.size() <= 0)
        return false;
    const bool direct_string_length = strings.size() == vectorize_length;
    const bool direct_pattern_length = patterns.size() == vectorize_length;

    for (R_len_t i = 0; i < vectorize_length; ++i) {
        charport::StrView pattern;
        bool pattern_modified;
        if (!ci__direct_extract_view(
                patterns[direct_pattern_length
                    ? static_cast<R_xlen_t>(i)
                    : static_cast<R_xlen_t>(i) % patterns.size()],
                pattern, pattern_modified
        ) || pattern_modified ||
                (!pattern.is_na() && pattern.len <= 0)) {
            return false;
        }

        charport::StrView value;
        bool value_modified;
        if (!ci__direct_extract_view(
                strings[direct_string_length
                    ? static_cast<R_xlen_t>(i)
                    : static_cast<R_xlen_t>(i) % strings.size()],
                value, value_modified
        )) {
            return false;
        }
        const char* match = NULL;
        if (!value.is_na() && !pattern.is_na()) {
            match = first
                ? ci__find_extract_first(
                    value.ptr, value.len, pattern.ptr, pattern.len
                )
                : ci__find_extract_last(
                    value.ptr, value.len, pattern.ptr, pattern.len
                );
        }

        if (match == NULL) {
            result.set_na(i);
        }
        else {
            ci::builder_set(
                result, i, pattern.ptr,
                static_cast<size_t>(pattern.len),
                cetype_ext_t::CE_ASCII_OR_UTF8
            );
        }
    }

    return true;
}


struct FixedExtractAllPlan {
    unsigned char pattern_byte;
    vector<R_len_t> counts;
    R_len_t max_columns;
};


struct FixedExtractListPlan {
    vector<R_len_t> counts;
    vector<size_t> pattern_ids;
    vector<string> patterns;
    vector<cetype_ext_t> encodings;
};


cetype_ext_t ci__direct_extract_encoding(
    const charport::StrView& value
) noexcept
{
    if (value.enc != cetype_ext_t::CE_ASCII_OR_UTF8)
        return value.enc;
    for (R_len_t i = 0; i < value.len; ++i) {
        if (static_cast<unsigned char>(value.ptr[i]) > 0x7fU)
            return cetype_ext_t::CE_UTF8;
    }
    return cetype_ext_t::CE_ASCII;
}


bool ci__plan_extract_all_fixed_list(
    const charport::StrViews& strings,
    const charport::StrViews& patterns,
    uint32_t pattern_flags, R_len_t vectorize_length,
    FixedExtractListPlan& plan
)
{
    if (pattern_flags != 0 || vectorize_length <= 0 ||
            strings.size() <= 0 || patterns.size() <= 0)
        return false;
    const bool direct_string_length = strings.size() == vectorize_length;
    const bool direct_pattern_length = patterns.size() == vectorize_length;

    plan.counts.assign(static_cast<size_t>(vectorize_length), 0);
    plan.pattern_ids.assign(static_cast<size_t>(vectorize_length), 0);
    plan.patterns.clear();
    plan.encodings.clear();
    unordered_map<string, size_t> ids;
    ids.reserve(64);

    for (R_len_t i = 0; i < vectorize_length; ++i) {
        charport::StrView pattern;
        bool pattern_modified;
        if (!ci__direct_extract_view(
                patterns[direct_pattern_length
                    ? static_cast<R_xlen_t>(i)
                    : static_cast<R_xlen_t>(i) % patterns.size()],
                pattern, pattern_modified
        ) || pattern_modified ||
                (!pattern.is_na() && pattern.len <= 0)) {
            return false;
        }

        charport::StrView value;
        bool value_modified;
        if (!ci__direct_extract_view(
                strings[direct_string_length
                    ? static_cast<R_xlen_t>(i)
                    : static_cast<R_xlen_t>(i) % strings.size()],
                value, value_modified
        )) {
            return false;
        }

        const size_t index = static_cast<size_t>(i);
        if (value.is_na() || pattern.is_na()) {
            plan.counts[index] = NA_INTEGER;
            continue;
        }

        string key(pattern.ptr, static_cast<size_t>(pattern.len));
        auto inserted = ids.emplace(key, plan.patterns.size());
        if (inserted.second) {
            plan.patterns.push_back(std::move(key));
            plan.encodings.push_back(ci__direct_extract_encoding(pattern));
        }
        plan.pattern_ids[index] = inserted.first->second;
        plan.counts[index] = ci__count_extract_matches(
            value.ptr, value.len, pattern.ptr, pattern.len
        );
    }
    return true;
}


struct FixedExtractListKey {
    size_t pattern;
    R_len_t count;

    bool operator==(const FixedExtractListKey& other) const noexcept
    {
        return pattern == other.pattern && count == other.count;
    }
};


struct FixedExtractListHash {
    size_t operator()(const FixedExtractListKey& value) const noexcept
    {
        return value.pattern ^
            (static_cast<size_t>(value.count) * static_cast<size_t>(0x9e3779b1U));
    }
};


charr::altrep::OutputStore ci__repeated_extract_store(
    const string& pattern, cetype_ext_t encoding, R_len_t size
)
{
    charr::altrep::OutputStore output(
        static_cast<size_t>(size), pattern.size()
    );
    if (size <= 0)
        return output;

    char* payload = output.slices.front_data();
    std::memcpy(payload, pattern.data(), pattern.size());
    for (R_len_t i = 0; i < size; ++i) {
        output.records.set(
            static_cast<size_t>(i), payload,
            static_cast<int>(pattern.size()),
            encoding
        );
    }
    return output;
}


void ci__build_extract_all_fixed_list(
    const FixedExtractListPlan& plan, R_len_t vectorize_length,
    bool omit_no_match, SEXP& result
)
{
    PROTECT(result = charport::unwind_protect([&]() -> SEXP {
        return Rf_allocVector(VECSXP, vectorize_length);
    }));

    unordered_map<FixedExtractListKey, SEXP, FixedExtractListHash> children;
    // Exact fixed matches reproduce the normalized pattern bytes. Equal
    // (pattern, count) signatures therefore produce byte-for-byte identical
    // immutable children, which may be shared safely under R's copy-on-modify
    // rules.
    children.reserve(static_cast<size_t>(
        vectorize_length < 1024 ? vectorize_length : 1024
    ));
    SEXP missing_child = R_NilValue;
    SEXP empty_child = R_NilValue;
    for (R_len_t i = 0; i < vectorize_length; ++i) {
        const size_t index = static_cast<size_t>(i);
        const R_len_t count = plan.counts[index];
        const bool forced_na = count == NA_INTEGER ||
            (count == 0 && !omit_no_match);
        if (forced_na) {
            if (missing_child == R_NilValue) {
                charr::altrep::OutputStore store =
                    charr::altrep::scalar_store(
                        charr::altrep::missing_output_record()
                    );
                PROTECT(missing_child = charr::altrep::finalize(
                    std::move(store)
                ));
                SET_VECTOR_ELT(result, i, missing_child);
                UNPROTECT(1);
            }
            else {
                SET_VECTOR_ELT(result, i, missing_child);
            }
            continue;
        }

        if (count == 0) {
            if (empty_child == R_NilValue) {
                charr::altrep::OutputStore store(0, 0);
                PROTECT(empty_child = charr::altrep::finalize(
                    std::move(store)
                ));
                SET_VECTOR_ELT(result, i, empty_child);
                UNPROTECT(1);
            }
            else {
                SET_VECTOR_ELT(result, i, empty_child);
            }
            continue;
        }

        const FixedExtractListKey key{plan.pattern_ids[index], count};
        const auto found = children.find(key);
        if (found != children.end()) {
            SET_VECTOR_ELT(result, i, found->second);
            continue;
        }

        charr::altrep::OutputStore store = ci__repeated_extract_store(
            plan.patterns[key.pattern], plan.encodings[key.pattern], count
        );
        SEXP child;
        PROTECT(child = charr::altrep::finalize(std::move(store)));
        SET_VECTOR_ELT(result, i, child);
        UNPROTECT(1);
        children.emplace(key, child);
    }
    UNPROTECT(1);
}


bool ci__plan_extract_all_fixed_byte(
    const charport::StrViews& strings,
    const charport::StrView& pattern, uint32_t pattern_flags,
    R_len_t vectorize_length, bool omit_no_match,
    FixedExtractAllPlan& plan, R_len_t& general_start
)
{
    if (pattern_flags != 0 || vectorize_length <= 0 ||
            strings.size() != vectorize_length)
        return false;

    if (!ci__direct_extract_pattern(pattern, plan.pattern_byte))
        return false;

    plan.counts.assign(static_cast<size_t>(vectorize_length), 0);
    plan.max_columns = 0;
    for (R_len_t i = 0; i < vectorize_length; ++i) {
        const charport::StrView value = strings[i];
        if (!ci__direct_extract_string(value)) {
            general_start = i;
            return false;
        }

        const R_len_t count = value.is_na()
            ? NA_INTEGER
            : ci__count_extract_byte(
                value.ptr, value.len, plan.pattern_byte
            );
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


void ci__build_extract_all_fixed_byte(
    const FixedExtractAllPlan& plan, R_len_t vectorize_length,
    int simplify, bool omit_no_match, SEXP& result
)
{
    const unsigned char pattern_byte = plan.pattern_byte;
    const vector<R_len_t>& counts = plan.counts;
    const R_len_t max_columns = plan.max_columns;

    if (simplify != NA_LOGICAL && !simplify) {
        PROTECT(result = charport::unwind_protect([&]() -> SEXP {
            return Rf_allocVector(VECSXP, vectorize_length);
        }));
        // Equal result signatures produce byte-for-byte identical immutable
        // children. Cache one child per signature instead of allocating an
        // equivalent Store for every list element. R's copy-on-modify rules
        // preserve the observable independence of shared children. Match
        // count is the complete signature for this literal-byte path.
        vector<SEXP> children(
            static_cast<size_t>(max_columns) + 1, R_NilValue
        );
        SEXP missing_child = R_NilValue;
        for (R_len_t i = 0; i < vectorize_length; ++i) {
            const R_len_t count = counts[static_cast<size_t>(i)];
            const bool forced_na = count == NA_INTEGER ||
                (count == 0 && !omit_no_match);
            SEXP child = R_NilValue;
            if (forced_na) {
                child = missing_child;
                if (child == R_NilValue) {
                    charr::altrep::OutputStore store =
                        charr::altrep::scalar_store(
                            charr::altrep::missing_output_record()
                        );
                    PROTECT(child = charr::altrep::finalize(
                        std::move(store)
                    ));
                    SET_VECTOR_ELT(result, i, child);
                    UNPROTECT(1);
                    missing_child = child;
                    continue;
                }
            }
            else {
                SEXP& cached = children[static_cast<size_t>(count)];
                child = cached;
                if (child == R_NilValue) {
                    charr::altrep::OutputStore store =
                        ci__repeated_extract_store(count, pattern_byte);
                    PROTECT(child = charr::altrep::finalize(
                        std::move(store)
                    ));
                    SET_VECTOR_ELT(result, i, child);
                    UNPROTECT(1);
                    cached = child;
                    continue;
                }
            }
            SET_VECTOR_ELT(result, i, child);
        }
        UNPROTECT(1);
        return;
    }

    const R_xlen_t rows = vectorize_length;
    const R_xlen_t columns = max_columns;
    if (rows > 0 && columns > R_XLEN_T_MAX / rows)
        throw length_error("matrix length exceeds R's vector limit");
    const R_xlen_t output_size = rows * columns;
    if (static_cast<unsigned long long>(output_size) >
            static_cast<unsigned long long>(
                std::numeric_limits<size_t>::max()
            )) {
        throw length_error("matrix length exceeds C++ limits");
    }
    charr::altrep::OutputStore output(
        static_cast<size_t>(output_size),
        static_cast<size_t>(output_size)
    );
    char* payload = output_size > 0 ? output.slices.front_data() : NULL;

    for (R_xlen_t i = 0; i < rows; ++i) {
        const R_len_t count = counts[static_cast<size_t>(i)];
        const bool forced_na = count == NA_INTEGER ||
            (count == 0 && !omit_no_match);
        R_xlen_t j = 0;
        if (count > 0) {
            for (; j < count; ++j) {
                const size_t index = static_cast<size_t>(i + j * rows);
                payload[index] = static_cast<char>(pattern_byte);
                output.records.set(
                    index, payload + index, 1,
                    cetype_ext_t::CE_ASCII
                );
            }
        }
        for (; j < columns; ++j) {
            const size_t index = static_cast<size_t>(i + j * rows);
            if (simplify == NA_LOGICAL || (forced_na && j == 0)) {
                output.records.set_na(index);
            }
            else {
                output.records.set(
                    index, "", 0, cetype_ext_t::CE_ASCII
                );
            }
        }
    }

    PROTECT(result = charr::altrep::finalize(std::move(output)));
    charport::unwind_protect([&]() -> SEXP {
        SEXP dim;
        PROTECT(dim = Rf_allocVector(INTSXP, 2));
        INTEGER(dim)[0] = vectorize_length;
        INTEGER(dim)[1] = max_columns;
        Rf_setAttrib(result, R_DimSymbol, dim);
        UNPROTECT(1);
        return R_NilValue;
    });
    UNPROTECT(1);
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
    charport::unwind_protect([&]() -> SEXP {
        vectorize_length = ci__recycling_rule(
            STRI__DEFERRED_WARNINGS, 2, str_n, pattern_n
        );
        return R_NilValue;
    });

    charport::charvec::Builder builder(vectorize_length);
    bool direct = false;
    std::shared_ptr<ci::ReaderBorrow> str_borrow;
    std::shared_ptr<ci::ReaderBorrow> pattern_borrow;
    if (pattern_flags == 0 && vectorize_length > 0) {
        str_borrow = context.acquire(str);
        pattern_borrow = context.acquire(pattern);
        direct = ci__extract_firstlast_fixed_plain(
            str_borrow->views(), pattern_borrow->views(),
            pattern_flags, vectorize_length, first, builder
        );
    }

    if (!direct) {
        builder.reset(vectorize_length);
        {
            Utf8Input str_cont(context, str, vectorize_length);
            StriContainerByteSearch pattern_cont(
                context, pattern, vectorize_length, pattern_flags
            );

            for (R_len_t i = pattern_cont.vectorize_init();
                    i != pattern_cont.vectorize_end();
                    i = pattern_cont.vectorize_next(i))
            {
                STRI__CONTINUE_ON_EMPTY_OR_NA_STR_PATTERN(str_cont, pattern_cont,
                        builder.set_na(i);, builder.set_na(i);)

                StriByteSearchMatcher* matcher = pattern_cont.getMatcher(i);
                matcher->reset(str_cont.get(i).data(), str_cont.get(i).length());
                int start, len;
                if (first) {
                    start = matcher->findFirst();
                } else {
                    start = matcher->findLast();
                }
                if (start == USEARCH_DONE) {
                    builder.set_na(i);
                    continue;
                }

                len = matcher->getMatchedLength();

                ci::builder_set(
                    builder, i, str_cont.get(i).data()+start, len,
                    cetype_ext_t::CE_ASCII_OR_UTF8
                );
            }
        }
    }

    pattern_borrow.reset();
    str_borrow.reset();
    STRI__PROTECT(ret = builder.to_sexp());
    }
    STRI__DEFERRED_WARNINGS.emit();
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
    const int simplify1 = LOGICAL_RO(simplify)[0];

    STRI__ERROR_HANDLER_BEGIN(3)
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
    charport::unwind_protect([&]() -> SEXP {
        vectorize_length = ci__recycling_rule(
            STRI__DEFERRED_WARNINGS, 2, str_n, pattern_n
        );
        return R_NilValue;
    });

    bool direct = false;
    bool direct_list = false;
    FixedExtractAllPlan direct_plan;
    FixedExtractListPlan direct_list_plan;
    R_len_t general_start = 0;
    std::shared_ptr<ci::ReaderBorrow> str_borrow;
    std::shared_ptr<ci::ReaderBorrow> pattern_borrow;
    if (pattern_flags == 0 && vectorize_length > 0) {
        str_borrow = context.acquire(str);
        pattern_borrow = context.acquire(pattern);
        if (simplify1 != NA_LOGICAL && !simplify1) {
            direct_list = ci__plan_extract_all_fixed_list(
                str_borrow->views(), pattern_borrow->views(),
                pattern_flags, vectorize_length, direct_list_plan
            );
        }
        if (!direct_list && pattern_n == 1) {
            direct = ci__plan_extract_all_fixed_byte(
                str_borrow->views(), pattern_borrow->views()[0],
                pattern_flags, vectorize_length, omit_no_match1,
                direct_plan, general_start
            );
        }
    }

    if (direct_list) {
        pattern_borrow.reset();
        str_borrow.reset();
        ci__build_extract_all_fixed_list(
            direct_list_plan, vectorize_length, omit_no_match1, ret
        );
        STRI__PROTECT(ret);
    }
    else if (direct) {
        pattern_borrow.reset();
        str_borrow.reset();
        ci__build_extract_all_fixed_byte(
            direct_plan, vectorize_length, simplify1,
            omit_no_match1, ret
        );
        STRI__PROTECT(ret);
    }
    else {
    vector<charr::altrep::OutputStore> stores;
    stores.reserve(static_cast<size_t>(vectorize_length));
    for (R_len_t i=0; i<vectorize_length; ++i)
        stores.push_back(charr::altrep::OutputStore(0, 0));

    if (vectorize_length > 0) {
        charr::altrep::Utf8Input str_input(
            context, str, vectorize_length
        );
        for (R_xlen_t i = 0; i < str_input.source_size(); ++i) {
            if (str_input.is_bytes(i))
                throw StriException(MSG__BYTESENC);
        }
        StriContainerByteSearch pattern_cont(
            context, pattern, vectorize_length, pattern_flags
        );
        // Each child's match count is unknown until its matcher is exhausted.
        // Reusing one growable builder preserves the single-pass search while
        // avoiding a fresh builder allocation for every input record.
        charr::altrep::GrowableOutputBuilder builder;

        for (R_len_t i = 0; i < general_start; ++i) {
            const R_len_t count = direct_plan.counts[
                static_cast<size_t>(i)
            ];
            charr::altrep::OutputStore& current = stores[
                static_cast<size_t>(i)
            ];
            if (count == NA_INTEGER ||
                    (count == 0 && !omit_no_match1)) {
                current = charr::altrep::scalar_store(
                    charr::altrep::missing_output_record()
                );
            }
            else if (count > 0) {
                current = ci__repeated_extract_store(
                    count, direct_plan.pattern_byte
                );
            }
        }

        for (R_len_t i = general_start > 0
                    ? general_start
                    : pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            charr::altrep::OutputStore& current = stores[
                static_cast<size_t>(i)
            ];
            if (str_input.is_na(i) || pattern_cont.isNA(i) ||
                    pattern_cont.get(i).length() <= 0) {
                current = charr::altrep::scalar_store(
                    charr::altrep::missing_output_record()
                );
                continue;
            }
            const charport::StrView subject = str_input.text(i);
            if (subject.len <= 0) {
                if (!omit_no_match1) {
                    current = charr::altrep::scalar_store(
                        charr::altrep::missing_output_record()
                    );
                }
                continue;
            }

            StriByteSearchMatcher* matcher = pattern_cont.getMatcher(i);
            matcher->reset(subject.ptr, subject.len);

            const int first_start = matcher->findFirst();
            if (first_start == USEARCH_DONE) {
                if (!omit_no_match1) {
                    current = charr::altrep::scalar_store(
                        charr::altrep::missing_output_record()
                    );
                }
                continue;
            }

            const size_t first_length = static_cast<size_t>(
                matcher->getMatchedLength()
            );
            int start = matcher->findNext();
            if (start == USEARCH_DONE) {
                current = charr::altrep::scalar_store(
                    subject.ptr + first_start, first_length,
                    cetype_ext_t::CE_ASCII_OR_UTF8
                );
                continue;
            }

            builder.reset();
            builder.append(
                subject.ptr + first_start, first_length,
                cetype_ext_t::CE_ASCII_OR_UTF8
            );
            do {
                builder.append(
                    subject.ptr + start,
                    static_cast<size_t>(matcher->getMatchedLength()),
                    cetype_ext_t::CE_ASCII_OR_UTF8
                );
                start = matcher->findNext();
            } while (start != USEARCH_DONE);
            current = builder.release_store();
        }
    }

    pattern_borrow.reset();
    str_borrow.reset();

    if (simplify1 != NA_LOGICAL && !simplify1) {
        STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
            return Rf_allocVector(VECSXP, vectorize_length);
        }));
        for (R_len_t i=0; i<vectorize_length; ++i) {
            SEXP current;
            STRI__PROTECT(current = charr::altrep::finalize(
                std::move(stores[i])
            ));
            SET_VECTOR_ELT(ret, i, current);
            STRI__UNPROTECT(1);
        }
    }
    else {
        size_t max_columns = 0;
        for (R_len_t i=0; i<vectorize_length; ++i) {
            if (stores[i].size() > max_columns)
                max_columns = stores[i].size();
        }

        // Deviation from stringi: the direct Store-to-Builder matrix path
        // checks its dimensions before narrowing or multiplying them.
        if (max_columns > static_cast<size_t>(R_LEN_T_MAX))
            throw length_error("matrix columns exceed R's integer limit");
        const R_xlen_t rows = vectorize_length;
        const R_xlen_t columns = static_cast<R_xlen_t>(max_columns);
        if (rows > 0 && columns > R_XLEN_T_MAX/rows)
            throw length_error("matrix length exceeds R's vector limit");

        charr::altrep::OutputBuilder matrix_builder(rows*columns);
        for (R_xlen_t i=0; i<rows; ++i) {
            const charr::altrep::OutputStore& current = stores[i];
            const R_xlen_t current_size = static_cast<R_xlen_t>(
                current.size()
            );
            R_xlen_t j = 0;
            for (; j<current_size; ++j)
                matrix_builder.set(i+j*rows, current.view(j));
            for (; j<columns; ++j) {
                if (simplify1 == NA_LOGICAL) {
                    matrix_builder.set_na(i+j*rows);
                }
                else {
                    matrix_builder.set(
                        i+j*rows, "", 0,
                        cetype_ext_t::CE_ASCII
                    );
                }
            }
        }

        STRI__PROTECT(ret = matrix_builder.to_sexp());
        charport::unwind_protect([&]() -> SEXP {
            SEXP dim;
            PROTECT(dim = Rf_allocVector(INTSXP, 2));
            INTEGER(dim)[0] = vectorize_length;
            INTEGER(dim)[1] = static_cast<R_len_t>(max_columns);
            Rf_setAttrib(ret, R_DimSymbol, dim);
            UNPROTECT(1);
            return R_NilValue;
        });
    }
    }

    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END({/* no-op */})
}
