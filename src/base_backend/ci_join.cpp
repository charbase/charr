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
#include "io/integer_input.h"
#include "io/utf8_list_input.h"
#include "ci_string8buf.h"
#include "../shared/native_to_utf8.h"
#include "io/string_output.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
namespace charr { namespace base_backend {

using namespace std;


namespace join {


struct DirectStringView {
    const char* data;
    R_len_t length;
    bool is_na;
    bool is_direct;
    bool is_unmodified;
};


class JoinStringNormalizer {
private:
    shared::NativeToUtf8 converter_;

public:
    DirectStringView get(SEXP value)
    {
        if (value == NA_STRING)
            return DirectStringView{NULL, 0, true, true, true};

        const char* data = CHAR(value);
        R_len_t length = LENGTH(value);
        if (IS_ASCII(value))
            return DirectStringView{data, length, false, true, true};
        if (IS_UTF8(value)) {
            if (STRI__ENC_HAS_BOM_UTF8(data, length)) {
                data += 3;
                length -= 3;
                return DirectStringView{data, length, false, true, false};
            }
            return DirectStringView{data, length, false, true, true};
        }
        if (IS_BYTES(value))
            throw StriException(MSG__BYTESENC);
        try {
            const bool native_has_bom = !IS_LATIN1(value) &&
                STRI__ENC_HAS_BOM_UTF8(data, length);
            const shared::ByteView converted = IS_LATIN1(value)
                ? converter_.latin1(data, length)
                : converter_.native(data, length);
            data = converted.ptr;
            length = converted.len;
            if (native_has_bom &&
                    STRI__ENC_HAS_BOM_UTF8(data, length)) {
                data += 3;
                length -= 3;
            }
        }
        catch (const std::exception& error) {
            throw StriException("%s", error.what());
        }
        return DirectStringView{data, length, false, false, false};
    }
};


DirectStringView ci__direct_string_view(SEXP value);


class JoinStringCache {
private:
    struct Entry {
        DirectStringView view;
        std::unique_ptr<char[]> owned;

        explicit Entry(const DirectStringView& value) :
            view(value), owned()
        {
            if (value.length > 0) {
                owned.reset(new char[static_cast<size_t>(value.length)]);
                memcpy(owned.get(), value.data, value.length);
                view.data = owned.get();
            }
            else {
                view.data = "";
            }
        }

        Entry(Entry&&) noexcept = default;
        Entry& operator=(Entry&&) noexcept = default;
        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;
    };

    const SEXP* values_;
    R_len_t size_;
    vector<size_t> slots_;
    vector<Entry> entries_;

    static size_t no_slot()
    {
        return static_cast<size_t>(-1);
    }

    void add(R_len_t i, const DirectStringView& value)
    {
        if (slots_.empty())
            slots_.assign(static_cast<size_t>(size_), no_slot());
        slots_[static_cast<size_t>(i)] = entries_.size();
        entries_.emplace_back(value);
    }

public:
    JoinStringCache(const SEXP* values, R_len_t size) :
        values_(values), size_(size), slots_(), entries_()
    {
        JoinStringNormalizer normalizer;
        for (R_len_t i=0; i<size; ++i) {
            SEXP value = values[i];
            if (value == NA_STRING || IS_ASCII(value) || IS_UTF8(value))
                continue;
            add(i, normalizer.get(value));
        }
    }

    DirectStringView get(R_len_t i) const
    {
        if (!slots_.empty()) {
            const size_t slot = slots_[static_cast<size_t>(i)];
            if (slot != no_slot())
                return entries_[slot].view;
        }
        return ci__direct_string_view(values_[i]);
    }

    DirectStringView get_bytes(R_len_t i) const
    {
        return get(i);
    }
};


struct FlattenPlan {
    size_t bytes;
    bool has_na;
    bool too_large;
};


void ci__plan_add(FlattenPlan& plan, size_t bytes)
{
    if (bytes > static_cast<size_t>(POW_2_31_M_1)-plan.bytes) {
        plan.too_large = true;
        return;
    }
    plan.bytes += bytes;
}


bool ci__direct_string_source(SEXP source)
{
    if (ALTREP(source))
        return false;

    // Joining needs only a pointer and length once every record is already
    // ASCII or UTF-8. Preflight the marks so the hot loop can skip the copied
    // record array; Latin-1 and native conversion stay in the general path.
    const SEXP* values = STRING_PTR_RO(source);
    const R_xlen_t size = XLENGTH(source);
    for (R_xlen_t i=0; i<size; ++i) {
        const SEXP value = values[i];
        if (value != NA_STRING && !IS_ASCII(value) && !IS_UTF8(value))
            return false;
    }
    return true;
}


DirectStringView ci__direct_string_view(SEXP value)
{
    if (value == NA_STRING)
        return DirectStringView{NULL, 0, true, true, true};

    const char* data = CHAR(value);
    R_len_t length = LENGTH(value);
    if (IS_UTF8(value) && STRI__ENC_HAS_BOM_UTF8(data, length)) {
        data += 3;
        length -= 3;
        return DirectStringView{data, length, false, true, false};
    }
    return DirectStringView{data, length, false, true, true};
}


void ci__repeat_bytes(
    char* destination, const char* source,
    size_t source_length, size_t total_length
)
{
    memcpy(destination, source, source_length);
    size_t written = source_length;
    while (written < total_length) {
        const size_t amount = std::min(written, total_length-written);
        memcpy(destination+written, destination, amount);
        written += amount;
    }
}


void ci__flatten_append(
    string& output, const char* data, size_t length, bool& too_large
)
{
    if (too_large || length == 0)
        return;
    if (length > static_cast<size_t>(POW_2_31_M_1)-output.size()) {
        too_large = true;
        return;
    }
    try {
        output.append(data, length);
    }
    catch (const std::exception& error) {
        throw StriException("%s", error.what());
    }
}


void ci__flatten_reserve(
    string& output, const SEXP* values, R_len_t size,
    size_t separator_length
)
{
    // Capacity is only a hint: sample enough records to avoid ordinary
    // reallocations, but do not reintroduce a vector-wide sizing pass or a
    // large speculative allocation.
    const size_t reserve_limit =
        static_cast<size_t>(64)*1024U*1024U;
    const R_len_t sample_size = std::min<R_len_t>(size, 1024);
    size_t sample_bytes = 0;
    for (R_len_t i=0; i<sample_size; ++i) {
        if (values[i] == NA_STRING)
            continue;
        const size_t length = static_cast<size_t>(LENGTH(values[i]));
        if (length > reserve_limit-sample_bytes) {
            sample_bytes = reserve_limit;
            break;
        }
        sample_bytes += length;
    }

    const double mean_bytes = sample_size > 0
        ? static_cast<double>(sample_bytes) /
            static_cast<double>(sample_size)
        : 0.0;
    const double estimated = 1.125 * (
        mean_bytes*static_cast<double>(size) +
        separator_length*static_cast<double>(size > 0 ? size-1 : 0)
    );
    const size_t reserve_size = estimated >= static_cast<double>(reserve_limit)
        ? reserve_limit
        : static_cast<size_t>(estimated);
    try {
        output.reserve(reserve_size);
    }
    catch (const std::exception& error) {
        throw StriException("%s", error.what());
    }
}


} // namespace join

using namespace join;


/**
 * Prepare list argument -- ignore empty vectors if needed, used by ci_paste
 *
 * @param x a list of strings
 * @param ignore_null FALSE to do nothing
 * @return a list vector
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-27)
 */
SEXP ci__prepare_arg_list_ignore_null(SEXP x, bool ignore_null)
{
    if (!ignore_null)
        return x;

    PROTECT(x);

#ifndef NDEBUG
    if (!Rf_isVectorList(x))
        Rf_error("ci__prepare_arg_list_ignore_null:: !NDEBUG: not a list"); // error() allowed here
#endif

    R_len_t narg = LENGTH(x);
    if (narg <= 0) {
        UNPROTECT(1);
        return x;
    }
//   else if (narg == 1 && LENGTH(VECTOR_ELT(x, 0)) == 0) {
//      UNPROTECT(1);
//      return Rf_allocVector(VECSXP, 0);
//   }

    SEXP ret;
//   if (ignore_null != NA_INTEGER && ignore_null < 0) { // remove NULL elements
    R_len_t nret = 0;
    for (R_len_t i=0; i<narg; ++i) {
#ifndef NDEBUG
        if (!Rf_isVector(VECTOR_ELT(x, i)))
            Rf_error("ci__prepare_arg_list_ignore_null:: !NDEBUG: not a vector element"); // error() allowed here
#endif
        if (LENGTH(VECTOR_ELT(x, i)) > 0)
            ++nret;
    }

    PROTECT(ret = Rf_allocVector(VECSXP, nret));
    for (R_len_t i=0, j=0; i<narg; ++i) {
        if (LENGTH(VECTOR_ELT(x, i)) > 0)
            SET_VECTOR_ELT(ret, j++, VECTOR_ELT(x, i));
    }
//   }
//   else { // insert one empty string
//      PROTECT(ret = Rf_allocVector(VECSXP, narg));
//      for (R_len_t i=0; i<narg; ++i) {
//         if (LENGTH(VECTOR_ELT(x, i)) > 0)
//            SET_VECTOR_ELT(ret, i, VECTOR_ELT(x, i));
//         else if (ignore_null != NA_INTEGER)
//            SET_VECTOR_ELT(ret, i, ci__vector_empty_strings(1));
////         else
////            SET_VECTOR_ELT(ret, i, ci__vector_NA_strings(1));
//      }
//   }
    UNPROTECT(2);
    return ret;
}


/** Duplicate given strings
 *
 *
 *  @param str character vector
 *  @param times integer vector
 *  @return character vector
 *
 *  The function is vectorized over str and times
 *  if str is NA or times is NA the result will be NA
 *  if times < 0, the result will be NA
 *  if times==0, the result will be an empty string
 *  if str or times is an empty vector, then the result is an empty vector
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *     use UTF-8 input vectorization
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-15)
 *     use io::IntegerInput
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *     make StriException friendly
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.7.6.9001 (Marek Gagolewski, 2022-03-15)
 *    #473: use size_t
*/
SEXP ci_dup(SEXP str, SEXP times)
{
    PROTECT(str = ci__prepare_arg_string(str, "str")); // prepare string argument
    PROTECT(times = ci__prepare_arg_integer(times, "times")); // prepare string argument
    R_len_t vectorize_length = ci__recycling_rule(true, 2, LENGTH(str), LENGTH(times));
    if (vectorize_length <= 0) {
        UNPROTECT(2);
        return Rf_allocVector(STRSXP, 0);
    }

    STRI__ERROR_HANDLER_BEGIN(2)
    const R_len_t str_length = LENGTH(str);
    const SEXP* values = STRING_PTR_RO(str);
    io::IntegerInput times_cont(times, vectorize_length);
    JoinStringCache cache(values, str_length);
    std::unique_ptr<char[]> buffer;
    size_t capacity = 0;
    SEXP ret;
    STRI__PROTECT(ret = Rf_allocVector(STRSXP, vectorize_length));

    for (R_len_t i=0; i<vectorize_length; ++i) {
        const DirectStringView value = cache.get(i%str_length);
        const R_len_t times_cur = times_cont.getNAble(i);
        if (value.is_na || times_cur == NA_INTEGER || times_cur < 0) {
            SET_STRING_ELT(ret, i, NA_STRING);
            continue;
        }

        const size_t length = static_cast<size_t>(value.length);
        if (times_cur == 0 || length == 0) {
            SET_STRING_ELT(ret, i, R_BlankString);
            continue;
        }
        if (length > static_cast<size_t>(POW_2_31_M_1) /
                static_cast<size_t>(times_cur)) {
            throw StriException(MSG__CHARSXP_2147483647);
        }

        const size_t total = length*static_cast<size_t>(times_cur);
        if (total > capacity) {
            buffer.reset(new char[total]);
            capacity = total;
        }
        ci__repeat_bytes(buffer.get(), value.data, length, total);
        SET_STRING_ELT(
            ret, i,
            Rf_mkCharLenCE(buffer.get(), static_cast<int>(total), CE_UTF8)
        );
    }


    // STEP 4.
    // Clean up & finish

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/** Join two character vectors, element by element, no separator, no collapse
 *
 * Vectorized over e1 and e2. Optimized for |e1| >= |e2|
 * (but no harm otherwise)
 *
 * This is used by %s+% operator in stringi R code.
 *
 * @param e1 character vector
 * @param e2 character vector
 * @return character vector, res_i=s1_i + s2_i for |e1|==|e2|
 *  if e1 or e2 is NA then result is NA
 *  if e1 or e2 is empty, then the result is just e1 or e2
 *
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *    use UTF-8 input vectorization
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *    make StriException friendly
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
*/
SEXP ci_join2(SEXP e1, SEXP e2) // a.k.a. ci_join2_nocollapse
{
    PROTECT(e1 = ci__prepare_arg_string(e1, "e1")); // prepare string argument
    PROTECT(e2 = ci__prepare_arg_string(e2, "e2")); // prepare string argument

    R_len_t e1_length = LENGTH(e1);
    R_len_t e2_length = LENGTH(e2);
    R_len_t vectorize_length = ci__recycling_rule(true, 2, e1_length, e2_length);

    if (e1_length <= 0) {
        UNPROTECT(2);
        return e1;
    }
    if (e2_length <= 0) {
        UNPROTECT(2);
        return e2;
    }

    STRI__ERROR_HANDLER_BEGIN(2)
    if (ci__direct_string_source(e1) && ci__direct_string_source(e2)) {
        const SEXP* first = STRING_PTR_RO(e1);
        const SEXP* second = STRING_PTR_RO(e2);
        const bool first_scalar = e1_length == 1;
        const bool second_scalar = e2_length == 1;
        const bool first_aligned = e1_length == vectorize_length;
        const bool second_aligned = e2_length == vectorize_length;
        const DirectStringView first_scalar_view = first_scalar
            ? ci__direct_string_view(first[0])
            : DirectStringView{NULL, 0, false, false, false};
        const DirectStringView second_scalar_view = second_scalar
            ? ci__direct_string_view(second[0])
            : DirectStringView{NULL, 0, false, false, false};
        std::unique_ptr<char[]> buffer;
        size_t capacity = 0;
        SEXP ret;
        STRI__PROTECT(ret = Rf_allocVector(STRSXP, vectorize_length));

        // Normalize recycled scalars once and avoid division when an input is
        // already aligned with the result. Both shapes are common in vector
        // concatenation and need no per-record recycling work.
        for (R_len_t i=0; i<vectorize_length; ++i) {
            const DirectStringView a = first_scalar
                ? first_scalar_view
                : ci__direct_string_view(
                    first[first_aligned ? i : i%e1_length]
                );
            const DirectStringView b = second_scalar
                ? second_scalar_view
                : ci__direct_string_view(
                    second[second_aligned ? i : i%e2_length]
                );
            if (a.is_na || b.is_na) {
                SET_STRING_ELT(ret, i, NA_STRING);
                continue;
            }

            const size_t a_length = static_cast<size_t>(a.length);
            const size_t b_length = static_cast<size_t>(b.length);
            if (a_length > static_cast<size_t>(POW_2_31_M_1)-b_length)
                throw StriException(MSG__CHARSXP_2147483647);
            const size_t total = a_length+b_length;
            if (total == 0) {
                SET_STRING_ELT(ret, i, R_BlankString);
                continue;
            }
            if (total > capacity) {
                buffer.reset(new char[total]);
                capacity = total;
            }
            if (a_length > 0)
                memcpy(buffer.get(), a.data, a_length);
            if (b_length > 0)
                memcpy(buffer.get()+a_length, b.data, b_length);
            SET_STRING_ELT(
                ret, i,
                Rf_mkCharLenCE(buffer.get(), static_cast<int>(total), CE_UTF8)
            );
        }

        STRI__UNPROTECT_ALL
        return ret;
    }

    io::Utf8Input e1_cont(e1, vectorize_length);
    io::Utf8Input e2_cont(e2, vectorize_length);

    // 1. find maximal length of the buffer needed
    size_t nchar = 0;
    for (int i=0; i<vectorize_length; ++i) {
        if (e1_cont.isNA(i) || e2_cont.isNA(i))
            continue;

        size_t c1 = e1_cont.get(i).length();
        size_t c2 = e2_cont.get(i).length();

        if (c1+c2 > nchar) nchar = c1+c2;
    }

    // 2. Create buf & retval
    if (nchar > POW_2_31_M_1)
        throw StriException(MSG__CHARSXP_2147483647);

    String8buf buf(nchar);
    SEXP ret;
    STRI__PROTECT(ret = Rf_allocVector(STRSXP, vectorize_length)); // output vector

    // 3. Set retval
    const io::Utf8Record* last_string_1 = NULL;
    R_len_t last_buf_idx = 0;
    for (R_len_t i = e1_cont.vectorize_init(); // this iterator allows for...
            i != e1_cont.vectorize_end();        // ...smart buffer reusage
            i = e1_cont.vectorize_next(i))
    {
        if (e1_cont.isNA(i) || e2_cont.isNA(i)) {
            SET_STRING_ELT(ret, i, NA_STRING);
            continue;
        }

        // If e1 has length < length of e2, this will be faster:
        const io::Utf8Record* cur_string_1 = &(e1_cont.get(i));
        if (cur_string_1 != last_string_1) {
            last_string_1 = cur_string_1;
            last_buf_idx = cur_string_1->length();
            memcpy(buf.data(), cur_string_1->data(), (size_t)last_buf_idx);
        }
        // else reuse string #1

        const io::Utf8Record* cur_string_2 = &(e2_cont.get(i));
        R_len_t  cur_len_2 = cur_string_2->length();
        memcpy(buf.data()+last_buf_idx, cur_string_2->data(), (size_t)cur_len_2);
        // the result is always in UTF-8
        SET_STRING_ELT(ret, i, Rf_mkCharLenCE(buf.data(), last_buf_idx+cur_len_2, CE_UTF8));
    }

    // 4. Cleanup & finish
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/** Join and flatten two character vectors, no separator between elements but possibly with collapse
 *
 * Vectorized over e1 and e2.
 *
 * @param e1 character vector
 * @param e2 character vector
 * @param collapse single string or NULL
 * @return character vector
 *
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-18)
 *          first version;
 *          This is much faster than ci_flatten(ci_join2(...), ...)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 *  @version 0.4-1 (Marek Gagolewski, 2014-11-26)
 *    #114: inconsistent behaviour w.r.t. paste()
*/
SEXP ci_join2_withcollapse(SEXP e1, SEXP e2, SEXP collapse)
{
    if (Rf_isNull(collapse)) {
        // no collapse - used, e.g., by the %s+% operator
        return ci_join2(e1, e2); // a.k.a. ci_join2_nocollapse
    }

    PROTECT(e1 = ci__prepare_arg_string(e1, "e1")); // prepare string argument
    PROTECT(e2 = ci__prepare_arg_string(e2, "e2")); // prepare string argument
    PROTECT(collapse = ci__prepare_arg_string_1(collapse, "collapse"));
    if (STRING_ELT(collapse, 0) == NA_STRING) {
        UNPROTECT(3);
        return ci__vector_NA_strings(1);
    }

    R_len_t e1_length = LENGTH(e1);
    R_len_t e2_length = LENGTH(e2);
    R_len_t vectorize_length = ci__recycling_rule(true, 2, e1_length, e2_length);

    if (e1_length <= 0 || e2_length <= 0) {
        UNPROTECT(3);
        return ci__vector_empty_strings(1);
    }

    STRI__ERROR_HANDLER_BEGIN(3)
    io::Utf8Input e1_cont(e1, vectorize_length);
    io::Utf8Input e2_cont(e2, vectorize_length);
    io::Utf8Input collapse_cont(collapse, 1);
    R_len_t collapse_nbytes = collapse_cont.get(0).length();
    const char* collapse_s = collapse_cont.get(0).data();


    // Find the required length without overflowing a signed R length before
    // the CHARSXP limit is checked.
    FlattenPlan plan = {0, false, false};
    for (int i=0; i<vectorize_length; ++i) {
        if (e1_cont.isNA(i) || e2_cont.isNA(i)) {
            STRI__UNPROTECT_ALL
            return ci__vector_NA_strings(1); // at least 1 NA => return NA
        }

        ci__plan_add(
            plan, static_cast<size_t>(e1_cont.get(i).length())
        );
        ci__plan_add(
            plan, static_cast<size_t>(e2_cont.get(i).length())
        );
        if (i > 0)
            ci__plan_add(plan, static_cast<size_t>(collapse_nbytes));
    }


    if (plan.too_large)
        throw StriException(MSG__CHARSXP_2147483647);
    io::ScalarStringBuilder output;
    char* destination = output.set_uninitialized(
        plan.bytes, io::OutputEncoding::utf8
    );
    size_t last_buf_idx = 0;
    for (R_len_t i = 0; i < vectorize_length; ++i) // don't change this order, see #114
    {
        // no need to detect NAs - they already have been excluded
        if (collapse_nbytes > 0 && i > 0) { // copy collapse (separator)
            memcpy(destination+last_buf_idx, collapse_s, (size_t)collapse_nbytes);
            last_buf_idx += collapse_nbytes;
        }

        const io::Utf8Record* cur_string_1 = &(e1_cont.get(i));
        R_len_t  cur_len_1 = cur_string_1->length();
        if (cur_len_1 > 0) {
            memcpy(
                destination+last_buf_idx,
                cur_string_1->data(), (size_t)cur_len_1
            );
        }
        last_buf_idx += cur_len_1;

        const io::Utf8Record* cur_string_2 = &(e2_cont.get(i));
        R_len_t  cur_len_2 = cur_string_2->length();
        if (cur_len_2 > 0) {
            memcpy(
                destination+last_buf_idx,
                cur_string_2->data(), (size_t)cur_len_2
            );
        }
        last_buf_idx += cur_len_2;
    }

    SEXP ret;
    STRI__PROTECT(ret = output.to_sexp());
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/**
 * Concatenate Character Vectors, with no collapse
 *
 * @param strlist list of character vectors
 * @param sep single string
 * @param ignore_null single integer
 * @return character vector
 *
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          use UTF-8 input vectorization
 *
 * @version 0.1-12 (Marek Gagolewski, 2013-12-04)
 *          fixed bug #49
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-18)
 *          ci_join has been split to ci_join_nocollapse
 *          and ci_join_withcollapse (for efficiency reasons)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-27)
 *    FR #116: ignore_null arg added
 */
SEXP ci_join_nocollapse(SEXP strlist, SEXP sep, SEXP ignore_null)
{
    bool ignore_null1 = ci__prepare_arg_logical_1_notNA(ignore_null, "ignore_null");
    PROTECT(strlist = ci__prepare_arg_list_ignore_null(
                          ci__prepare_arg_list_string(strlist, "..."), ignore_null1
                      ));
    R_len_t strlist_length = LENGTH(strlist);
    if (strlist_length <= 0) {
        UNPROTECT(1);
        return ci__vector_empty_strings(0);
    }

    // get length of the longest character vector on the list, i.e., vectorize_length
    R_len_t vectorize_length = 0;
    for (R_len_t i=0; i<strlist_length; ++i) {
        R_len_t strlist_cur_length = LENGTH(VECTOR_ELT(strlist, i));
        if (strlist_cur_length <= 0) {
            UNPROTECT(1);
            return ci__vector_empty_strings(0);
        }
        if (strlist_cur_length > vectorize_length)
            vectorize_length = strlist_cur_length;
    }

    PROTECT(sep = ci__prepare_arg_string_1(sep, "sep"));
    if (STRING_ELT(sep, 0) == NA_STRING) {
        UNPROTECT(2);
        return ci__vector_NA_strings(vectorize_length);
    }


    // * special case *
    if (LENGTH(STRING_ELT(sep, 0)) == 0 && strlist_length == 2) {
        // sep==empty string and 2 vectors --
        // an often occurring case - we have some specialized functions for this :-)
        SEXP ret;
        PROTECT(ret = ci_join2(VECTOR_ELT(strlist, 0), VECTOR_ELT(strlist, 1))); // a.k.a. ci_join2_nocollapse
        UNPROTECT(3);
        return ret;
    }

    // note that if 1 vector is given
    // we cannot return VECTOR_ELT(strlist, 0) directly
    // -- it needs to be converted to UTF8
    // so we proceed

    SEXP ret;
    STRI__ERROR_HANDLER_BEGIN(2)

    io::Utf8Input sep_cont(sep, 1);
    const char* sep_char = sep_cont.get(0).data();
    R_len_t     sep_len  = sep_cont.get(0).length();

    io::Utf8ListInput strlist_cont(strlist, vectorize_length);


    // 4. Get buf size and determine where NAs will occur
    size_t buf_maxbytes = 0;
    vector<bool> whichNA(vectorize_length, false); // where are NAs in out?
    for (R_len_t i=0; i<vectorize_length; ++i) {

        FlattenPlan row = {0, false, false};
        for (R_len_t j=0; j<strlist_length; ++j) {
            if (strlist_cont.get(j).isNA(i)) {
                whichNA[i] = true;
                break;
            }
            else {
                ci__plan_add(
                    row,
                    static_cast<size_t>(
                        strlist_cont.get(j).get(i).length()
                    )
                );
                if (j > 0)
                    ci__plan_add(row, static_cast<size_t>(sep_len));
            }
        }
        if (!whichNA[i] && row.too_large)
            throw StriException(MSG__CHARSXP_2147483647);
        if (!whichNA[i] && row.bytes > buf_maxbytes)
            buf_maxbytes = row.bytes;
    }

    // 5. Create ret val
    String8buf buf(buf_maxbytes);
    STRI__PROTECT(ret = Rf_allocVector(STRSXP, vectorize_length));

    for (R_len_t i=0; i<vectorize_length; ++i) {
        if (whichNA[i]) {
            SET_STRING_ELT(ret, i, NA_STRING);
            continue;
        }

        size_t cursize = 0;
        for (R_len_t j=0; j<strlist_length; ++j) {

            if (sep_len >= 0 && j > 0) {
                memcpy(buf.data()+cursize, sep_char, (size_t)sep_len);
                cursize += sep_len;
            }

            const io::Utf8Record* curstring = &(strlist_cont.get(j).get(i));
            size_t curstring_n = curstring->length();
            memcpy(buf.data()+cursize, curstring->data(), (size_t)curstring_n);
            cursize += curstring_n;
        }

        SET_STRING_ELT(ret, i, Rf_mkCharLenCE(buf.data(), cursize, CE_UTF8));
    }

    // nothing more to do:
    STRI__UNPROTECT_ALL
    return ret;

    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/**
 * Concatenate Character Vectors, possibly with collapse
 *
 * @param strlist list of character vectors
 * @param sep single string
 * @param collapse single string or NULL
 * @param ignore_null single integer
 * @return character vector
 *
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-18)
 *          a specialized version of the original ci_join, which
 *          called ci_flatten at the end, if it was requested;
 *          now collapsing is done directly (for time and memory efficiency);
 *          Now calling specialized functions
 *          ci_join2_withcollapse and ci_flatten_withressep, if needed.
 *          If collapse!=NULL and sep=NA, then the result will be single NA
 *          (and not n*NA);
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-27)
 *    FR #116: ignore_null arg added
 */
SEXP ci_join(SEXP strlist, SEXP sep, SEXP collapse, SEXP ignore_null)
{
    // no collapse-case is handled separately:
    if (Rf_isNull(collapse))
        return ci_join_nocollapse(strlist, sep, ignore_null);

    // *result will surely be a single string*

    bool ignore_null1 = ci__prepare_arg_logical_1_notNA(ignore_null, "ignore_null");
    PROTECT(strlist = ci__prepare_arg_list_ignore_null(
                          ci__prepare_arg_list_string(strlist, "..."), ignore_null1
                      ));
    R_len_t strlist_length = LENGTH(strlist);
    if (strlist_length <= 0) {
        UNPROTECT(1);
        return ci__vector_empty_strings(1);
    }
    else if (strlist_length == 1) {
        // one vector + collapse string -- another frequently occurring case
        // sep is ignored here
        SEXP ret;
        PROTECT(ret = ci_flatten(VECTOR_ELT(strlist, 0), collapse)); // a.k.a. ci_flatten_withressep
        UNPROTECT(2);
        return ret;
    }

    PROTECT(sep = ci__prepare_arg_string_1(sep, "sep"));
    PROTECT(collapse = ci__prepare_arg_string_1(collapse, "collapse"));
    if (STRING_ELT(sep, 0) == NA_STRING || STRING_ELT(collapse, 0) == NA_STRING) {
        UNPROTECT(3);
        return ci__vector_NA_strings(1);
    }
    else if (LENGTH(STRING_ELT(sep, 0)) == 0 && strlist_length == 2) {
        // sep==empty string and 2 vectors --
        // an often occurring case - we have some specialized functions for this :-)
        SEXP ret;
        PROTECT(ret = ci_join2_withcollapse(VECTOR_ELT(strlist, 0), VECTOR_ELT(strlist, 1), collapse));
        UNPROTECT(4);
        return ret;
    }

    // get length of the longest character vector on the list, i.e., vectorize_length
    R_len_t vectorize_length = 0;
    for (R_len_t i=0; i<strlist_length; ++i) {
        R_len_t strlist_cur_length = LENGTH(VECTOR_ELT(strlist, i));
        if (strlist_cur_length <= 0) {
            UNPROTECT(3);
            return ci__vector_empty_strings(1);
        }
        if (strlist_cur_length > vectorize_length)
            vectorize_length = strlist_cur_length;
    }


    STRI__ERROR_HANDLER_BEGIN(3)

    io::Utf8ListInput strlist_cont(strlist, vectorize_length);

    io::Utf8Input sep_cont(sep, 1); // definitely not NA
    const char* sep_s = sep_cont.get(0).data();
    R_len_t     sep_n = sep_cont.get(0).length();

    io::Utf8Input collapse_cont(collapse, 1); // definitely not NA
    const char* collapse_s = collapse_cont.get(0).data();
    R_len_t     collapse_n = collapse_cont.get(0).length();

    // Get required buffer size
    FlattenPlan plan = {0, false, false};
    for (R_len_t i=0; i<vectorize_length; ++i) {   // for each vectorized string (vertically)
        for (R_len_t j=0; j<strlist_length; ++j) {  // for each character vector  (horizontally)
            if (strlist_cont.get(j).isNA(i)) {
                STRI__UNPROTECT_ALL
                return ci__vector_NA_strings(1);
            }

            ci__plan_add(
                plan,
                static_cast<size_t>(
                    strlist_cont.get(j).get(i).length()
                )
            );
            if (j > 0)
                ci__plan_add(plan, static_cast<size_t>(sep_n));
        }

        if (i > 0)
            ci__plan_add(plan, static_cast<size_t>(collapse_n));
    }

    // 5. Create ret val
    if (plan.too_large)
        throw StriException(MSG__CHARSXP_2147483647);
    io::ScalarStringBuilder output;
    char* destination = output.set_uninitialized(
        plan.bytes, io::OutputEncoding::utf8
    );
    size_t last_buf_idx = 0;

    for (R_len_t i=0; i<vectorize_length; ++i) {
        // there is no NA anywhere

        if (collapse_n > 0 && i > 0) {
            memcpy(destination+last_buf_idx, collapse_s, (size_t)collapse_n);
            last_buf_idx += collapse_n;
        }

        for (R_len_t j=0; j<strlist_length; ++j) {

            if (sep_n > 0 && j > 0) {
                memcpy(destination+last_buf_idx, sep_s, (size_t)sep_n);
                last_buf_idx += sep_n;
            }

            const io::Utf8Record* curstring = &(strlist_cont.get(j).get(i));
            size_t curstring_n = curstring->length();
            if (curstring_n > 0) {
                memcpy(
                    destination+last_buf_idx,
                    curstring->data(), curstring_n
                );
            }
            last_buf_idx += curstring_n;
        }
    }

#ifndef NDEBUG
    if (plan.bytes != last_buf_idx)
        throw StriException("ci_join_withcollapse: buffer overrun");
#endif

    // we are done
    SEXP ret;
    STRI__PROTECT(ret = output.to_sexp());
    STRI__UNPROTECT_ALL
    return ret;

    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/** String vector flatten, with no separator (i.e., empty) between each string
 *
 *  if any of s is NA, the result will be NA_character_
 *
 *  @param s character vector
 *  @return if s is not empty, then a character vector of length 1
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          UTF-8 input - any R encoding
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException friendly
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-18)
 *          This function hasn't been used at all before (strange, isn't it?);
 *          From now on it's being called by ci_flatten_withressep
 *          (a small performance gain)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.2.1 (Marek Gagolewski, 2018-04-20)
 *    na_empty arg added
 *
 * @version 1.6.2 (Marek Gagolewski, 2021-05-10)
 *    #428 na_empty=NA support
 */
SEXP ci_flatten_noressep(SEXP str, int na_empty)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    R_len_t str_length = LENGTH(str);
    if (str_length <= 0) {
        UNPROTECT(1);
        return ci__vector_empty_strings(1);
    }

    STRI__ERROR_HANDLER_BEGIN(1)
    const SEXP* values = STRING_PTR_RO(str);
    FlattenPlan plan = {0, false, false};
    JoinStringCache cache(values, str_length);
    for (R_len_t i=0; i<str_length; ++i) {
        const DirectStringView value = cache.get(i);
        if (value.is_na) {
            if (na_empty != NA_LOGICAL && !na_empty)
                plan.has_na = true;
            continue;
        }
        ci__plan_add(plan, static_cast<size_t>(value.length));
    }
    if (plan.has_na) {
        STRI__UNPROTECT_ALL
        return ci__vector_NA_strings(1);
    }
    if (plan.too_large)
        throw StriException(MSG__CHARSXP_2147483647);

    io::ScalarStringBuilder output;
    char* destination = output.set_uninitialized(
        plan.bytes, io::OutputEncoding::utf8
    );
    size_t cursor = 0;
    for (R_len_t i=0; i<str_length; ++i) {
        const DirectStringView value = cache.get_bytes(i);
        if (!value.is_na && value.length > 0) {
            memcpy(destination+cursor, value.data, value.length);
            cursor += static_cast<size_t>(value.length);
        }
    }

    SEXP ret;
    STRI__PROTECT(ret = output.to_sexp());
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/** String vector flatten, with separator between each string
 *
 *  if any of str is NA, the result will be NA_character_
 *
 *  @param str character vector
 *  @param collapse a single string
 *  @return if s is not empty, then a character vector of length 1
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Bartek Tartanus)
 *          collapse arg added (1 sep supported)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          UTF-8 input - any R encoding
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException friendly
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-18)
 *          Call ci_flatten_noressep if needed
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.2.1 (Marek Gagolewski, 2018-04-20)
 *    na_empty, omit_empty arg added
 *
 * @version 1.6.2 (Marek Gagolewski, 2021-05-10)
 *    #428 na_empty=NA support
 *
 */
SEXP ci_flatten(SEXP str, SEXP collapse, SEXP na_empty, SEXP omit_empty) // a.k.a. C_ci_flatten_withressep
{
    PROTECT(collapse = ci__prepare_arg_string_1(collapse, "collapse"));
    int na_empty_1 = ci__prepare_arg_logical_1_NA(na_empty, "na_empty");
    bool omit_empty_1 = ci__prepare_arg_logical_1_notNA(omit_empty, "omit_empty");

    if (STRING_ELT(collapse, 0) == NA_STRING) {
        UNPROTECT(1);
        return ci__vector_NA_strings(1);
    }

    // if collapse is an empty string, we may use the following
    // specialized function:
    if (LENGTH(STRING_ELT(collapse, 0)) == 0) {
        UNPROTECT(1);
        return ci_flatten_noressep(str, na_empty_1);
    }

    PROTECT(str = ci__prepare_arg_string(str, "str")); // prepare string argument
    R_len_t str_length = LENGTH(str);
    if (str_length <= 0) {
        UNPROTECT(2);
        return ci__vector_empty_strings(1);
    }

    STRI__ERROR_HANDLER_BEGIN(2)
    const SEXP* values = STRING_PTR_RO(str);
    const SEXP separator_value = STRING_ELT(collapse, 0);
    if (IS_ASCII(separator_value) || IS_UTF8(separator_value)) {
        // A direct separator lets normalization and output growth share one
        // source traversal. Keep scanning after NA or overflow so later
        // encoding errors are still observed; converted separators retain the
        // exact-size two-pass path below.
        const DirectStringView separator =
            ci__direct_string_view(separator_value);
        JoinStringNormalizer normalizer;
        string bytes;
        ci__flatten_reserve(
            bytes, values, str_length,
            static_cast<size_t>(separator.length)
        );
        bool started = false;
        bool has_na = false;
        bool too_large = false;
        for (R_len_t i=0; i<str_length; ++i) {
            const DirectStringView value = normalizer.get(values[i]);
            if (value.is_na &&
                    na_empty_1 != NA_LOGICAL && !na_empty_1) {
                has_na = true;
                continue;
            }
            if (value.is_na && na_empty_1 == NA_LOGICAL)
                continue;
            if (omit_empty_1 && (value.is_na || value.length == 0))
                continue;

            if (!has_na && started) {
                ci__flatten_append(
                    bytes, separator.data,
                    static_cast<size_t>(separator.length), too_large
                );
            }
            else if (!has_na) {
                started = true;
            }
            if (!has_na && !value.is_na) {
                ci__flatten_append(
                    bytes, value.data,
                    static_cast<size_t>(value.length), too_large
                );
            }
        }

        if (has_na) {
            STRI__UNPROTECT_ALL
            return ci__vector_NA_strings(1);
        }
        if (too_large)
            throw StriException(MSG__CHARSXP_2147483647);

        SEXP ret;
        STRI__PROTECT(ret = Rf_allocVector(STRSXP, 1));
        SET_STRING_ELT(
            ret, 0,
            Rf_mkCharLenCE(
                bytes.data(), static_cast<int>(bytes.size()), CE_UTF8
            )
        );
        STRI__UNPROTECT_ALL
        return ret;
    }

    FlattenPlan plan = {0, false, false};
    size_t pieces = 0;
    JoinStringCache cache(values, str_length);
    for (R_len_t i=0; i<str_length; ++i) {
        const DirectStringView value = cache.get(i);
        if (value.is_na && na_empty_1 != NA_LOGICAL && !na_empty_1) {
            plan.has_na = true;
            continue;
        }
        if (value.is_na && na_empty_1 == NA_LOGICAL)
            continue;
        if (omit_empty_1 && (value.is_na || value.length == 0))
            continue;
        if (!value.is_na)
            ci__plan_add(plan, static_cast<size_t>(value.length));
        ++pieces;
    }
    JoinStringNormalizer separator_normalizer;
    const DirectStringView separator = separator_normalizer.get(
        STRING_ELT(collapse, 0)
    );
    const size_t separator_count = pieces > 0 ? pieces-1 : 0;
    const size_t separator_length = static_cast<size_t>(separator.length);
    if (separator_count > 0 && separator_length >
            (static_cast<size_t>(POW_2_31_M_1)-plan.bytes) /
                separator_count) {
        plan.too_large = true;
    }
    else {
        plan.bytes += separator_count*separator_length;
    }
    if (plan.has_na) {
        STRI__UNPROTECT_ALL
        return ci__vector_NA_strings(1);
    }
    if (plan.too_large)
        throw StriException(MSG__CHARSXP_2147483647);

    io::ScalarStringBuilder output;
    char* destination = output.set_uninitialized(
        plan.bytes, io::OutputEncoding::utf8
    );
    size_t cursor = 0;
    bool started = false;
    for (R_len_t i=0; i<str_length; ++i) {
        const DirectStringView value = cache.get_bytes(i);
        if (value.is_na && na_empty_1 == NA_LOGICAL)
            continue;
        if (omit_empty_1 && (value.is_na || value.length == 0))
            continue;
        if (started) {
            if (separator.length > 0) {
                memcpy(
                    destination+cursor, separator.data,
                    static_cast<size_t>(separator.length)
                );
                cursor += static_cast<size_t>(separator.length);
            }
        }
        else
            started = true;
        if (!value.is_na && value.length > 0) {
            memcpy(destination+cursor, value.data, value.length);
            cursor += static_cast<size_t>(value.length);
        }
    }

    SEXP ret;
    STRI__PROTECT(ret = output.to_sexp());
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


} } // namespace charr::base_backend
