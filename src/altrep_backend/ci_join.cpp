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
#include "ci_container_base.h"
#include "ci_utf8.h"
#include "ci_container_integer.h"
#include "ci_container_listutf8.h"
#include "../altrep/native_to_utf8.h"
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>
using namespace std;


namespace {


struct ScalarStringInfo {
    bool is_na;
    bool is_empty;
};


struct DirectStringView {
    const char* data;
    R_len_t length;
    bool is_na;
    bool is_ascii;
    bool is_direct;
};


class JoinStringNormalizer {
private:
    charr::altrep::NativeToUtf8 converter_;

public:
    DirectStringView get(const charport::StrView& value)
    {
        if (value.is_na())
            return DirectStringView{NULL, 0, true, false, true};
        if (value.enc == cetype_ext_t::CE_BYTES)
            throw StriException(MSG__BYTESENC);

        const char* data = value.ptr;
        R_len_t length = value.len;
        if (value.enc == cetype_ext_t::CE_ASCII)
            return DirectStringView{data, length, false, true, true};
        if (value.enc == cetype_ext_t::CE_UTF8) {
            if (STRI__ENC_HAS_BOM_UTF8(data, length)) {
                data += 3;
                length -= 3;
            }
            return DirectStringView{data, length, false, false, true};
        }
        if (value.enc == cetype_ext_t::CE_ASCII_OR_UTF8) {
            if (STRI__ENC_HAS_BOM_UTF8(data, length)) {
                data += 3;
                length -= 3;
            }
            return DirectStringView{
                data, length, false,
                ci::is_ascii(data, static_cast<size_t>(length)), true
            };
        }
        if (value.enc != cetype_ext_t::CE_NATIVE &&
                value.enc != cetype_ext_t::CE_LATIN1) {
            throw StriException("unknown charport string encoding");
        }

        const bool native_has_bom =
            value.enc == cetype_ext_t::CE_NATIVE &&
            STRI__ENC_HAS_BOM_UTF8(data, length);
        const charport::ByteView converted =
            value.enc == cetype_ext_t::CE_LATIN1
            ? converter_.latin1(data, length)
            : converter_.native(data, length);
        data = converted.ptr;
        length = converted.len;
        if (native_has_bom && STRI__ENC_HAS_BOM_UTF8(data, length)) {
            data += 3;
            length -= 3;
        }
        return DirectStringView{data, length, false, false, false};
    }
};


DirectStringView ci__direct_string_bytes(const charport::StrView& value);
DirectStringView ci__direct_string_view(const charport::StrView& value);


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

    const charport::StrViews& values_;
    vector<size_t> slots_;
    vector<Entry> entries_;

    static size_t no_slot()
    {
        return static_cast<size_t>(-1);
    }

    void add(R_xlen_t i, const DirectStringView& value)
    {
        if (slots_.empty())
            slots_.assign(static_cast<size_t>(values_.size()), no_slot());
        slots_[static_cast<size_t>(i)] = entries_.size();
        entries_.emplace_back(value);
    }

public:
    explicit JoinStringCache(const charport::StrViews& values) :
        values_(values), slots_(), entries_()
    {
        JoinStringNormalizer normalizer;
        const R_xlen_t size = values.size();
        for (R_xlen_t i=0; i<size; ++i) {
            const charport::StrView value = values[i];
            if (value.is_na() || value.enc == cetype_ext_t::CE_ASCII ||
                    value.enc == cetype_ext_t::CE_UTF8 ||
                    value.enc == cetype_ext_t::CE_ASCII_OR_UTF8) {
                continue;
            }
            add(i, normalizer.get(value));
        }
    }

    DirectStringView get(R_xlen_t i) const
    {
        if (!slots_.empty()) {
            const size_t slot = slots_[static_cast<size_t>(i)];
            if (slot != no_slot())
                return entries_[slot].view;
        }
        return ci__direct_string_view(values_[i]);
    }

    DirectStringView get_bytes(R_xlen_t i) const
    {
        if (!slots_.empty()) {
            const size_t slot = slots_[static_cast<size_t>(i)];
            if (slot != no_slot())
                return entries_[slot].view;
        }
        return ci__direct_string_bytes(values_[i]);
    }
};


struct FlattenPlan {
    size_t bytes;
    bool has_na;
    bool too_large;
    bool is_ascii;
};


void ci__plan_add(FlattenPlan& plan, size_t bytes)
{
    if (bytes > static_cast<size_t>(POW_2_31_M_1)-plan.bytes) {
        plan.too_large = true;
        return;
    }
    plan.bytes += bytes;
}


bool ci__direct_string_views(const charport::StrViews& values)
{
    // Reader records that already contain UTF-8 bytes can feed the final
    // Builder directly. Native and Latin-1 records retain the conversion
    // containers used by the fallback paths.
    const R_xlen_t size = values.size();
    for (R_xlen_t i=0; i<size; ++i) {
        const charport::StrView value = values[i];
        if (value.is_na())
            continue;
        switch (value.enc) {
        case cetype_ext_t::CE_ASCII:
        case cetype_ext_t::CE_UTF8:
        case cetype_ext_t::CE_ASCII_OR_UTF8:
            break;
        case cetype_ext_t::CE_NATIVE:
        case cetype_ext_t::CE_LATIN1:
            return false;
        case cetype_ext_t::CE_BYTES:
            throw StriException(MSG__BYTESENC);
        case cetype_ext_t::CE_NA:
            break;
        default:
            throw StriException("unknown charport string encoding");
        }
    }
    return true;
}


DirectStringView ci__direct_string_bytes(const charport::StrView& value)
{
    if (value.is_na())
        return DirectStringView{NULL, 0, true, false, true};

    const char* data = value.ptr;
    R_len_t length = value.len;
    if ((value.enc == cetype_ext_t::CE_UTF8 ||
            value.enc == cetype_ext_t::CE_ASCII_OR_UTF8) &&
            STRI__ENC_HAS_BOM_UTF8(data, length)) {
        data += 3;
        length -= 3;
    }
    return DirectStringView{data, length, false, false, true};
}


DirectStringView ci__direct_string_view(const charport::StrView& value)
{
    DirectStringView output = ci__direct_string_bytes(value);
    if (output.is_na)
        return output;
    const bool is_ascii = value.enc == cetype_ext_t::CE_ASCII ||
        (value.enc == cetype_ext_t::CE_ASCII_OR_UTF8 &&
            ci::is_ascii(output.data, output.length));
    output.is_ascii = is_ascii;
    return output;
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


// Deviation from stringi: inspect only scalar NA and byte length through a
// short Reader borrow. Normalization, including bytes errors, stays deferred
// to the copied container boundary instead of materializing a CHARSXP here.
SEXP ci__inspect_scalar_string(SEXP source, ScalarStringInfo& info)
{
    STRI__ERROR_HANDLER_BEGIN(0)
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    {
        std::shared_ptr<ci::ReaderBorrow> borrow = context.acquire(source);
        if (borrow->size() != 1)
            throw StriException(MSG__INTERNAL_ERROR);

        const charport::StrView value = borrow->views()[0];
        info.is_na = value.is_na();
        info.is_empty = !info.is_na && value.len == 0;
    }
    context.emitWarnings();
    return R_NilValue;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


} // namespace


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
 *     use Utf8Input's vectorization
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-15)
 *     use StriContainerInteger
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

    STRI__ERROR_HANDLER_BEGIN(2)
    if (vectorize_length <= 0) {
        charport::charvec::Builder builder(0);
        SEXP ret;
        STRI__PROTECT(ret = builder.to_sexp());
        STRI__UNPROTECT_ALL
        return ret;
    }

    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    charport::charvec::Builder builder(vectorize_length);
    StriContainerInteger times_cont(times, vectorize_length);
    {
        std::shared_ptr<ci::ReaderBorrow> borrow = context.acquire(str);
        const charport::StrViews& values = borrow->views();
        const R_len_t str_length = ci::checked_r_len(
            borrow->size(), "character vectors"
        );
        JoinStringCache cache(values);
        for (R_len_t i=0; i<vectorize_length; ++i) {
            const DirectStringView value = cache.get(i%str_length);
            const R_len_t times_cur = times_cont.getNAble(i);
            if (value.is_na || times_cur == NA_INTEGER || times_cur < 0) {
                builder.set_na(i);
                continue;
            }

            const size_t length = static_cast<size_t>(value.length);
            if (times_cur == 0 || length == 0) {
                builder.set(i, "", 0, cetype_ext_t::CE_ASCII);
                continue;
            }
            if (length > static_cast<size_t>(POW_2_31_M_1) /
                    static_cast<size_t>(times_cur)) {
                throw StriException(MSG__CHARSXP_2147483647);
            }

            const size_t total = length*static_cast<size_t>(times_cur);
            char* destination = builder.reserve(
                i, total,
                value.is_ascii
                    ? cetype_ext_t::CE_ASCII
                    : cetype_ext_t::CE_UTF8
            );
            ci__repeat_bytes(destination, value.data, length, total);
        }
    }


    // STEP 4.
    // Clean up & finish

    SEXP ret;
    STRI__PROTECT(ret = builder.to_sexp());
    context.emitWarnings();
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
 *    use Utf8Input's vectorization
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
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    charport::charvec::Builder builder(vectorize_length);
    {
        std::shared_ptr<ci::ReaderBorrow> first_borrow = context.acquire(e1);
        std::shared_ptr<ci::ReaderBorrow> second_borrow = context.acquire(e2);
        const charport::StrViews& first = first_borrow->views();
        const charport::StrViews& second = second_borrow->views();
        if (ci__direct_string_views(first) &&
                ci__direct_string_views(second)) {
            for (R_len_t i=0; i<vectorize_length; ++i) {
                const DirectStringView a = ci__direct_string_view(
                    first[i%e1_length]
                );
                const DirectStringView b = ci__direct_string_view(
                    second[i%e2_length]
                );
                if (a.is_na || b.is_na) {
                    builder.set_na(i);
                    continue;
                }

                const size_t a_length = static_cast<size_t>(a.length);
                const size_t b_length = static_cast<size_t>(b.length);
                if (a_length > static_cast<size_t>(POW_2_31_M_1)-b_length)
                    throw StriException(MSG__CHARSXP_2147483647);
                const size_t total = a_length+b_length;
                char* destination = builder.reserve(
                    i, total,
                    a.is_ascii && b.is_ascii
                        ? cetype_ext_t::CE_ASCII
                        : cetype_ext_t::CE_UTF8
                );
                if (a_length > 0)
                    memcpy(destination, a.data, a_length);
                if (b_length > 0)
                    memcpy(destination+a_length, b.data, b_length);
            }
        }
        else {
            Utf8Input e1_cont(context, e1, vectorize_length);
            Utf8Input e2_cont(context, e2, vectorize_length);
            for (R_len_t i=0; i<vectorize_length; ++i) {
                if (e1_cont.isNA(i) || e2_cont.isNA(i)) {
                    builder.set_na(i);
                    continue;
                }

                const Utf8Record& a = e1_cont.get(i);
                const Utf8Record& b = e2_cont.get(i);
                const size_t a_length = static_cast<size_t>(a.length());
                const size_t b_length = static_cast<size_t>(b.length());
                if (a_length > static_cast<size_t>(POW_2_31_M_1)-b_length)
                    throw StriException(MSG__CHARSXP_2147483647);
                const size_t total = a_length+b_length;
                char* destination = builder.reserve(
                    i, total,
                    a.isASCII() && b.isASCII()
                        ? cetype_ext_t::CE_ASCII
                        : cetype_ext_t::CE_UTF8
                );
                if (a_length > 0)
                    memcpy(destination, a.data(), a_length);
                if (b_length > 0)
                    memcpy(destination+a_length, b.data(), b_length);
            }
        }
    }

    // 4. Cleanup & finish
    SEXP ret;
    STRI__PROTECT(ret = builder.to_sexp());
    context.emitWarnings();
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

    ScalarStringInfo collapse_info = {false, false};
    ci__inspect_scalar_string(collapse, collapse_info);
    R_len_t e1_length = 0;
    R_len_t e2_length = 0;
    R_len_t vectorize_length = 0;
    if (!collapse_info.is_na) {
        e1_length = LENGTH(e1);
        e2_length = LENGTH(e2);
        vectorize_length = ci__recycling_rule(
            true, 2, e1_length, e2_length
        );
    }

    STRI__ERROR_HANDLER_BEGIN(3)
    if (collapse_info.is_na) {
        charport::charvec::Store output =
            charport::charvec::Store::scalar(
                NULL, 0, cetype_ext_t::CE_NA
            );
        SEXP ret;
        STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
        STRI__UNPROTECT_ALL
        return ret;
    }

    if (e1_length <= 0 || e2_length <= 0) {
        charport::charvec::Store output = ci::scalar_store(
            "", 0, cetype_ext_t::CE_ASCII
        );
        SEXP ret;
        STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
        STRI__UNPROTECT_ALL
        return ret;
    }

    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    charport::charvec::Store output(0, 0);
    {
        Utf8Input e1_cont(context, e1, vectorize_length);
        Utf8Input e2_cont(context, e2, vectorize_length);
        Utf8Input collapse_cont(context, collapse, 1);
        R_len_t collapse_nbytes = collapse_cont.get(0).length();
        const char* collapse_s = collapse_cont.get(0).data();


        // Find the required length component by component so neither an
        // R_len_t intermediate nor the running size can wrap.
        FlattenPlan plan = {0, false, false, true};
        if (vectorize_length > 1)
            plan.is_ascii = collapse_cont.get(0).isASCII();
        for (int i=0; i<vectorize_length; ++i) {
            if (e1_cont.isNA(i) || e2_cont.isNA(i)) {
                plan.has_na = true;
                break;
            }

            ci__plan_add(
                plan, static_cast<size_t>(e1_cont.get(i).length())
            );
            ci__plan_add(
                plan, static_cast<size_t>(e2_cont.get(i).length())
            );
            if (i > 0)
                ci__plan_add(plan, static_cast<size_t>(collapse_nbytes));
            plan.is_ascii = plan.is_ascii && e1_cont.get(i).isASCII() &&
                e2_cont.get(i).isASCII();
        }


        if (plan.has_na) {
            output = charport::charvec::Store::scalar(
                NULL, 0, cetype_ext_t::CE_NA
            );
        }
        else {
            if (plan.too_large)
                throw StriException(MSG__CHARSXP_2147483647);
            charport::charvec::Builder builder(1);
            char* destination = builder.reserve(
                0, plan.bytes,
                plan.is_ascii
                    ? cetype_ext_t::CE_ASCII
                    : cetype_ext_t::CE_UTF8
            );
            size_t last_buf_idx = 0;
            for (R_len_t i = 0; i < vectorize_length; ++i) // don't change this order, see #114
            {
                // no need to detect NAs - they already have been excluded
                if (collapse_nbytes > 0 && i > 0) { // copy collapse (separator)
                    memcpy(destination+last_buf_idx, collapse_s, (size_t)collapse_nbytes);
                    last_buf_idx += collapse_nbytes;
                }

                const Utf8Record* cur_string_1 = &(e1_cont.get(i));
                R_len_t  cur_len_1 = cur_string_1->length();
                if (cur_len_1 > 0) {
                    memcpy(
                        destination+last_buf_idx,
                        cur_string_1->data(), (size_t)cur_len_1
                    );
                }
                last_buf_idx += cur_len_1;

                const Utf8Record* cur_string_2 = &(e2_cont.get(i));
                R_len_t  cur_len_2 = cur_string_2->length();
                if (cur_len_2 > 0) {
                    memcpy(
                        destination+last_buf_idx,
                        cur_string_2->data(), (size_t)cur_len_2
                    );
                }
                last_buf_idx += cur_len_2;
            }

            output = builder.release_store();
        }
    }

    SEXP ret;
    STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
    context.emitWarnings();
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
 *          use Utf8Input's vectorization
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException-friendly, useStriContainerListUTF8
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
        STRI__ERROR_HANDLER_BEGIN(1)
        charport::charvec::Builder builder(0);
        SEXP ret;
        STRI__PROTECT(ret = builder.to_sexp());
        STRI__UNPROTECT_ALL
        return ret;
        STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
    }

    // get length of the longest character vector on the list, i.e., vectorize_length
    R_len_t vectorize_length = 0;
    for (R_len_t i=0; i<strlist_length; ++i) {
        R_len_t strlist_cur_length = LENGTH(VECTOR_ELT(strlist, i));
        if (strlist_cur_length <= 0) {
            STRI__ERROR_HANDLER_BEGIN(1)
            charport::charvec::Builder builder(0);
            SEXP ret;
            STRI__PROTECT(ret = builder.to_sexp());
            STRI__UNPROTECT_ALL
            return ret;
            STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
        }
        if (strlist_cur_length > vectorize_length)
            vectorize_length = strlist_cur_length;
    }

    PROTECT(sep = ci__prepare_arg_string_1(sep, "sep"));
    ScalarStringInfo sep_info = {false, false};
    ci__inspect_scalar_string(sep, sep_info);
    if (sep_info.is_na) {
        STRI__ERROR_HANDLER_BEGIN(2)
        charport::charvec::Builder builder(vectorize_length);
        for (R_len_t i=0; i<vectorize_length; ++i)
            builder.set_na(i);
        SEXP ret;
        STRI__PROTECT(ret = builder.to_sexp());
        STRI__UNPROTECT_ALL
        return ret;
        STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
    }


    // * special case *
    if (sep_info.is_empty && strlist_length == 2) {
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

    STRI__ERROR_HANDLER_BEGIN(2)
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    charport::charvec::Builder builder(vectorize_length);
    {
        Utf8Input sep_cont(context, sep, 1);
        const char* sep_char = sep_cont.get(0).data();
        R_len_t     sep_len  = sep_cont.get(0).length();

        StriContainerListUTF8 strlist_cont(
            context, strlist, vectorize_length
        );


        // 4. Get buf size and determine where NAs will occur
        vector<FlattenPlan> plans(
            static_cast<size_t>(vectorize_length),
            FlattenPlan{0, false, false, true}
        );
        for (R_len_t i=0; i<vectorize_length; ++i) {
            FlattenPlan& plan = plans[static_cast<size_t>(i)];
            for (R_len_t j=0; j<strlist_length; ++j) {
                if (strlist_cont.get(j).isNA(i)) {
                    plan.has_na = true;
                    break;
                }
                const Utf8Record& value = strlist_cont.get(j).get(i);
                ci__plan_add(plan, static_cast<size_t>(value.length()));
                plan.is_ascii = plan.is_ascii && value.isASCII();
                if (j > 0) {
                    ci__plan_add(plan, static_cast<size_t>(sep_len));
                    plan.is_ascii = plan.is_ascii &&
                        sep_cont.get(0).isASCII();
                }
            }
            if (plan.too_large)
                throw StriException(MSG__CHARSXP_2147483647);
        }

        for (R_len_t i=0; i<vectorize_length; ++i) {
            const FlattenPlan& plan = plans[static_cast<size_t>(i)];
            if (plan.has_na) {
                builder.set_na(i);
                continue;
            }

            char* destination = builder.reserve(
                i, plan.bytes,
                plan.is_ascii
                    ? cetype_ext_t::CE_ASCII
                    : cetype_ext_t::CE_UTF8
            );
            size_t cursize = 0;
            for (R_len_t j=0; j<strlist_length; ++j) {

                if (sep_len > 0 && j > 0) {
                    memcpy(destination+cursize, sep_char, (size_t)sep_len);
                    cursize += sep_len;
                }

                const Utf8Record* curstring = &(strlist_cont.get(j).get(i));
                size_t curstring_n = curstring->length();
                if (curstring_n > 0) {
                    memcpy(
                        destination+cursize,
                        curstring->data(), curstring_n
                    );
                }
                cursize += curstring_n;
            }
        }
    }

    // nothing more to do:
    SEXP ret;
    STRI__PROTECT(ret = builder.to_sexp());
    context.emitWarnings();
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
        STRI__ERROR_HANDLER_BEGIN(1)
        charport::charvec::Store output = ci::scalar_store(
            "", 0, cetype_ext_t::CE_ASCII
        );
        SEXP ret;
        STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
        STRI__UNPROTECT_ALL
        return ret;
        STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
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
    ScalarStringInfo sep_info = {false, false};
    ScalarStringInfo collapse_info = {false, false};
    ci__inspect_scalar_string(sep, sep_info);
    if (!sep_info.is_na)
        ci__inspect_scalar_string(collapse, collapse_info);
    if (sep_info.is_na || collapse_info.is_na) {
        STRI__ERROR_HANDLER_BEGIN(3)
        charport::charvec::Store output =
            charport::charvec::Store::scalar(
                NULL, 0, cetype_ext_t::CE_NA
            );
        SEXP ret;
        STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
        STRI__UNPROTECT_ALL
        return ret;
        STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
    }
    else if (sep_info.is_empty && strlist_length == 2) {
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
            STRI__ERROR_HANDLER_BEGIN(3)
            charport::charvec::Store output = ci::scalar_store(
                "", 0, cetype_ext_t::CE_ASCII
            );
            SEXP ret;
            STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
            STRI__UNPROTECT_ALL
            return ret;
            STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
        }
        if (strlist_cur_length > vectorize_length)
            vectorize_length = strlist_cur_length;
    }


    STRI__ERROR_HANDLER_BEGIN(3)
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    charport::charvec::Store output(0, 0);
    {
        StriContainerListUTF8 strlist_cont(
            context, strlist, vectorize_length
        );

        Utf8Input sep_cont(context, sep, 1); // definitely not NA
        const char* sep_s = sep_cont.get(0).data();
        R_len_t     sep_n = sep_cont.get(0).length();

        Utf8Input collapse_cont(
            context, collapse, 1
        ); // definitely not NA
        const char* collapse_s = collapse_cont.get(0).data();
        R_len_t     collapse_n = collapse_cont.get(0).length();

        // Get required buffer size
        FlattenPlan plan = {0, false, false, true};
        if (strlist_length > 1)
            plan.is_ascii = sep_cont.get(0).isASCII();
        if (vectorize_length > 1) {
            plan.is_ascii = plan.is_ascii &&
                collapse_cont.get(0).isASCII();
        }
        for (R_len_t i=0; i<vectorize_length; ++i) {   // for each vectorized string (vertically)
            for (R_len_t j=0; j<strlist_length; ++j) {  // for each character vector  (horizontally)
                if (strlist_cont.get(j).isNA(i)) {
                    plan.has_na = true;
                    break;
                }

                const Utf8Record& value = strlist_cont.get(j).get(i);
                ci__plan_add(plan, static_cast<size_t>(value.length()));
                plan.is_ascii = plan.is_ascii && value.isASCII();
                if (j > 0)
                    ci__plan_add(plan, static_cast<size_t>(sep_n));
            }

            if (plan.has_na)
                break;
            if (i > 0)
                ci__plan_add(plan, static_cast<size_t>(collapse_n));
        }

        if (plan.has_na) {
            output = charport::charvec::Store::scalar(
                NULL, 0, cetype_ext_t::CE_NA
            );
        }
        else {
            // 5. Create ret val
            if (plan.too_large)
                throw StriException(MSG__CHARSXP_2147483647);
            charport::charvec::Builder builder(1);
            char* destination = builder.reserve(
                0, plan.bytes,
                plan.is_ascii
                    ? cetype_ext_t::CE_ASCII
                    : cetype_ext_t::CE_UTF8
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

                    const Utf8Record* curstring = &(strlist_cont.get(j).get(i));
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

            output = builder.release_store();
        }
    }

    // we are done
    SEXP ret;
    STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
    context.emitWarnings();
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
 *          Utf8Input - any R Encoding
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
        STRI__ERROR_HANDLER_BEGIN(1)
        charport::charvec::Store output = ci::scalar_store(
            "", 0, cetype_ext_t::CE_ASCII
        );
        SEXP ret;
        STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
        STRI__UNPROTECT_ALL
        return ret;
        STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
    }

    STRI__ERROR_HANDLER_BEGIN(1)
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    charport::charvec::Store output(0, 0);
    {
        std::shared_ptr<ci::ReaderBorrow> borrow = context.acquire(str);
        const charport::StrViews& values = borrow->views();
        FlattenPlan plan = {0, false, false, true};
        JoinStringCache cache(values);
        for (R_len_t i=0; i<str_length; ++i) {
            const DirectStringView value = cache.get(i);
            if (value.is_na) {
                if (na_empty != NA_LOGICAL && !na_empty)
                    plan.has_na = true;
                continue;
            }
            ci__plan_add(plan, static_cast<size_t>(value.length));
            plan.is_ascii = plan.is_ascii && value.is_ascii;
        }
        if (plan.has_na) {
            output = charport::charvec::Store::scalar(
                NULL, 0, cetype_ext_t::CE_NA
            );
        }
        else {
            if (plan.too_large)
                throw StriException(MSG__CHARSXP_2147483647);
            charport::charvec::Builder builder(1);
            char* destination = builder.reserve(
                0, plan.bytes,
                plan.is_ascii
                    ? cetype_ext_t::CE_ASCII
                    : cetype_ext_t::CE_UTF8
            );
            size_t cursor = 0;
            for (R_len_t i=0; i<str_length; ++i) {
                const DirectStringView value = cache.get_bytes(i);
                if (!value.is_na && value.length > 0) {
                    memcpy(destination+cursor, value.data, value.length);
                    cursor += static_cast<size_t>(value.length);
                }
            }
            output = builder.release_store();
        }
    }

    // 3. Get ret val & good bye
    SEXP ret;
    STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
    context.emitWarnings();
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
 *          Utf8Input - any R Encoding
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

    ScalarStringInfo collapse_info = {false, false};
    ci__inspect_scalar_string(collapse, collapse_info);
    if (collapse_info.is_na) {
        STRI__ERROR_HANDLER_BEGIN(1)
        charport::charvec::Store output =
            charport::charvec::Store::scalar(
                NULL, 0, cetype_ext_t::CE_NA
            );
        SEXP ret;
        STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
        STRI__UNPROTECT_ALL
        return ret;
        STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
    }

    // if collapse is an empty string, we may use the following
    // specialized function:
    if (collapse_info.is_empty) {
        UNPROTECT(1);
        return ci_flatten_noressep(str, na_empty_1);
    }

    PROTECT(str = ci__prepare_arg_string(str, "str")); // prepare string argument
    R_len_t str_length = LENGTH(str);
    if (str_length <= 0) {
        STRI__ERROR_HANDLER_BEGIN(2)
        charport::charvec::Store output = ci::scalar_store(
            "", 0, cetype_ext_t::CE_ASCII
        );
        SEXP ret;
        STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
        STRI__UNPROTECT_ALL
        return ret;
        STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
    }

    STRI__ERROR_HANDLER_BEGIN(2)
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    charport::charvec::Store output(0, 0);
    {
        std::shared_ptr<ci::ReaderBorrow> str_borrow = context.acquire(str);
        std::shared_ptr<ci::ReaderBorrow> sep_borrow = context.acquire(collapse);
        const charport::StrViews& values = str_borrow->views();
        const charport::StrViews& separators = sep_borrow->views();
        FlattenPlan plan = {0, false, false, true};
        size_t pieces = 0;
        JoinStringCache cache(values);
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
            if (!value.is_na) {
                ci__plan_add(plan, static_cast<size_t>(value.length));
                plan.is_ascii = plan.is_ascii && value.is_ascii;
            }
            ++pieces;
        }
        JoinStringCache separator_cache(separators);
        const DirectStringView separator = separator_cache.get(0);
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
        if (separator_count > 0)
            plan.is_ascii = plan.is_ascii && separator.is_ascii;
        if (plan.has_na) {
            output = charport::charvec::Store::scalar(
                NULL, 0, cetype_ext_t::CE_NA
            );
        }
        else {
            if (plan.too_large)
                throw StriException(MSG__CHARSXP_2147483647);
            charport::charvec::Builder builder(1);
            char* destination = builder.reserve(
                0, plan.bytes,
                plan.is_ascii
                    ? cetype_ext_t::CE_ASCII
                    : cetype_ext_t::CE_UTF8
            );
            size_t cursor = 0;
            bool started = false;
            for (R_len_t i=0; i<str_length; ++i) {
                const DirectStringView value = cache.get_bytes(i);
                if (value.is_na && na_empty_1 == NA_LOGICAL)
                    continue;
                if (omit_empty_1 && (value.is_na || value.length == 0))
                    continue;
                if (started && separator.length > 0) {
                    memcpy(
                        destination+cursor, separator.data,
                        static_cast<size_t>(separator.length)
                    );
                    cursor += static_cast<size_t>(separator.length);
                }
                else if (!started) {
                    started = true;
                }
                if (!value.is_na && value.length > 0) {
                    memcpy(destination+cursor, value.data, value.length);
                    cursor += static_cast<size_t>(value.length);
                }
            }
            output = builder.release_store();
        }
    }

    // 3. Get ret val & return
    SEXP ret;
    STRI__PROTECT(ret = charport::charvec::wrap(std::move(output)));
    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/**
 * Concatenate strings in a list
 *
 * @param x list of character vectors
 * @param sep single string
 * @param collapse single string or NULL
 * @return character vector
 *
 * @version 1.0-3 (Marek Gagolewski, 2016-02-07)
 *    FR#175
 */
SEXP ci_join_list(SEXP x, SEXP sep, SEXP collapse)
{
    PROTECT(x = ci__prepare_arg_list_ignore_null(
                    ci__prepare_arg_list_string(x, "x"), true
                ));

    R_len_t strlist_length = LENGTH(x);
    if (strlist_length <= 0) {
        STRI__ERROR_HANDLER_BEGIN(1)
        charport::charvec::Builder builder(0);
        SEXP ret;
        STRI__PROTECT(ret = builder.to_sexp());
        STRI__UNPROTECT_ALL
        return ret;
        STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
    }

    PROTECT(sep = ci__prepare_arg_string_1(sep, "sep"));
    if (Rf_isNull(collapse))
        PROTECT(collapse);
    else
        PROTECT(collapse = ci__prepare_arg_string_1(collapse, "collapse"));

    STRI__ERROR_HANDLER_BEGIN(3)

    // Deviation from stringi: run the nested flatten entry points before
    // opening output Readers or a Builder. A protected list keeps their lazy
    // scalar results alive without materializing them as CHARSXPs.
    SEXP flattened;
    STRI__PROTECT(flattened = charport::unwind_protect([&]() -> SEXP {
        return Rf_allocVector(VECSXP, strlist_length);
    }));
    for (R_len_t j=0; j<strlist_length; ++j) {
        SEXP ret2;
        STRI__PROTECT(ret2 = charport::unwind_protect([&]() -> SEXP {
            return ci_flatten(VECTOR_ELT(x, j), sep);
        }));
        SET_VECTOR_ELT(flattened, j, ret2);
        STRI__UNPROTECT(1);
    }

    charport::charvec::Builder builder(strlist_length);
    ci::ReaderContext output_context(STRI__DEFERRED_WARNINGS);
    for (R_len_t j=0; j<strlist_length; ++j) {
        SEXP ret2 = VECTOR_ELT(flattened, j);
        {
            Utf8Input ret2_cont(output_context, ret2, 1);
            ci::builder_set(builder, j, ret2_cont.getNAble(0));
        }
    }
    SEXP ret;
    STRI__PROTECT(ret = builder.to_sexp());
    if (!Rf_isNull(collapse)) {
        STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
            return ci_flatten(ret, collapse);
        }));
    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;

    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}
