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
#include "ci_string8buf.h"
#include "../shared/native_to_utf8.h"
#include <cstdint>
#include <limits>
#include <stdexcept>


namespace charr { namespace base_backend {

namespace sub {

bool ci__sub_plain_integer_scalar(SEXP value, int& output)
{
    if (TYPEOF(value) != INTSXP || Rf_isObject(value) || ALTREP(value) ||
            !NO_ATTRIB(value) || XLENGTH(value) != 1) {
        return false;
    }
    output = INTEGER_RO(value)[0];
    return true;
}


bool ci__sub_plain_list_integer_scalar(
    SEXP values, R_len_t values_length, int& output
)
{
    return TYPEOF(values) == VECSXP && !Rf_isObject(values) &&
        !ALTREP(values) && NO_ATTRIB(values) && values_length == 1 &&
        ci__sub_plain_integer_scalar(VECTOR_ELT(values, 0), output);
}


R_len_t ci__sub_positive_utf8_boundary(
    const char* data, R_len_t length, R_len_t codepoints, bool ascii
)
{
    if (codepoints <= 0)
        return 0;
    if (ascii)
        return std::min(codepoints, length);

    R_len_t byte = 0;
    R_len_t current = 0;
    while (current < codepoints && byte < length) {
        // Keep malformed marked UTF-8 bounded by the current record.
        U8_FWD_1(reinterpret_cast<const uint8_t*>(data), byte, length);
        ++current;
    }
    return byte;
}


size_t ci__sub_checked_output_size(size_t current, size_t additional)
{
    if (additional > std::numeric_limits<size_t>::max()-current)
        throw std::length_error("character output size overflow");
    const size_t output = current+additional;
    if (output > static_cast<size_t>(R_LEN_T_MAX))
        throw std::length_error("character output is too large");
    return output;
}


R_len_t ci__sub_nonnegative_index(std::int64_t value)
{
    if (value <= 0)
        return 0;
    if (value >= static_cast<std::int64_t>(R_LEN_T_MAX))
        return R_LEN_T_MAX;
    return static_cast<R_len_t>(value);
}


R_len_t ci__sub_length_endpoint(R_len_t from, R_len_t length)
{
    const std::int64_t endpoint = static_cast<std::int64_t>(from)+
        static_cast<std::int64_t>(length)-1;
    if (from < 0 && endpoint >= 0)
        return -1;
    if (endpoint >= static_cast<std::int64_t>(R_LEN_T_MAX))
        return R_LEN_T_MAX;
    if (endpoint <= -static_cast<std::int64_t>(R_LEN_T_MAX))
        return -R_LEN_T_MAX;
    return static_cast<R_len_t>(endpoint);
}


R_len_t ci__sub_replacement_all_from(R_len_t from, R_len_t codepoints)
{
    std::int64_t position = from;
    if (position < 0)
        position = static_cast<std::int64_t>(codepoints)+position+1;
    if (position <= 0)
        position = 1;
    --position;
    if (position >= codepoints)
        return codepoints;
    return static_cast<R_len_t>(position);
}


R_len_t ci__sub_replacement_all_to(
    R_len_t to, bool is_length, R_len_t from, R_len_t codepoints
)
{
    std::int64_t position;
    if (is_length) {
        position = static_cast<std::int64_t>(from)+std::max(to, 0);
    }
    else {
        position = to;
        if (position < 0)
            position = static_cast<std::int64_t>(codepoints)+position+1;
        if (position < from)
            position = from;
    }
    if (position >= codepoints)
        return codepoints;
    return static_cast<R_len_t>(position);
}


struct CiSubInput {
    const char* data;
    R_len_t length;
    bool is_na;
    bool is_ascii;
};


class CiSubNormalizer {
private:
    shared::NativeToUtf8 converter_;

public:
    CiSubInput get(SEXP value)
    {
        if (value == NA_STRING)
            return CiSubInput{NULL, 0, true, false};

        const char* data = CHAR(value);
        R_len_t length = LENGTH(value);
        if (IS_ASCII(value))
            return CiSubInput{data, length, false, true};
        if (IS_BYTES(value))
            throw StriException(MSG__BYTESENC);

        if (!IS_UTF8(value)) {
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
            return CiSubInput{data, length, false, false};
        }

        if (STRI__ENC_HAS_BOM_UTF8(data, length)) {
            data += 3;
            length -= 3;
        }
        return CiSubInput{data, length, false, false};
    }
};


inline SEXP ci__sub_source_element(
    SEXP source, const SEXP* direct, R_len_t index
)
{
    return direct ? direct[index] : STRING_ELT(source, index);
}


void ci__sub_marked_utf8_range(
    const char* data, R_len_t length, R_len_t from, R_len_t to,
    bool ascii, R_len_t& from_byte, R_len_t& to_byte
)
{
    const R_len_t from_target = from-1;
    if (ascii) {
        from_byte = std::min(from_target, length);
        to_byte = std::min(to, length);
        return;
    }

    from_byte = from_target <= 0 ? 0 : length;
    to_byte = to <= 0 ? 0 : length;
    const R_len_t target = std::max(from_target, to);
    R_len_t byte = 0;
    R_len_t current = 0;
    while (current < target && byte < length) {
        U8_FWD_1(reinterpret_cast<const uint8_t*>(data), byte, length);
        ++current;
        if (current == from_target)
            from_byte = byte;
        if (current == to)
            to_byte = byte;
    }
}


} // namespace sub

using namespace sub;

/**
 * used both in ci_sub and ci_sub_replacement
 *
 * @return number of objects PROTECTEd
 *
 * @version ??? (Marek Gagolewski, 20??-??-??)
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-30) allow (from,length) matrices
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-07-08) use_matrix
 */
R_len_t ci__sub_prepare_from_to_length(SEXP& from, SEXP& to, SEXP& length,
        R_len_t& from_len, R_len_t& to_len, R_len_t& length_len,
        int*& from_tab, int*& to_tab, int*& length_tab, bool use_matrix_1)
{
    R_len_t sub_protected = 0;
    bool from_ismatrix = use_matrix_1 && Rf_isMatrix(from);
    if (from_ismatrix) {
        SEXP t;
        PROTECT(t = Rf_getAttrib(from, R_DimSymbol));
        if (INTEGER(t)[1] == 1)
            from_ismatrix = false; /* it's a column vector */
        else if (INTEGER(t)[1] > 2) {
            /* error() is allowed here */
            UNPROTECT(1); // t
            Rf_error(MSG__ARG_EXPECTED_MATRIX_WITH_GIVEN_COLUMNS, "from", 2);
        }
        UNPROTECT(1);  // t
    }

    sub_protected++;
    PROTECT(from = ci__prepare_arg_integer(from, "from"));
    /* may remove R_DimSymbol */

    if (from_ismatrix) {
        bool fromlength_matrix = false;
        SEXP t;
        PROTECT(t = Rf_getAttrib(from, R_DimNamesSymbol));
        if (!Rf_isNull(t)) {
            SEXP t2;
            PROTECT(t2 = VECTOR_ELT(t, 1));
            if (
                Rf_isString(t2) && LENGTH(t2) == 2 &&
                strcmp("length", CHAR(STRING_ELT(t2, 1))) == 0
            ) {
                fromlength_matrix = true;
            }
            UNPROTECT(1);  // t2
        }
        UNPROTECT(1);  // t

        if (fromlength_matrix) {
            from_len      = LENGTH(from)/2;
            length_len    = from_len;
            from_tab      = INTEGER(from);
            length_tab    = from_tab+from_len;
        }
        else {
            from_len      = LENGTH(from)/2;
            to_len        = from_len;
            from_tab      = INTEGER(from);
            to_tab        = from_tab+from_len;
        }
        //PROTECT(to); /* fake - not to provoke stack imbalance */
        //PROTECT(length); /* fake - not to provoke stack imbalance */
    }
    else if (Rf_isNull(length)) {
        sub_protected++;
        PROTECT(to    = ci__prepare_arg_integer(to, "to"));
        from_len      = LENGTH(from);
        from_tab      = INTEGER(from);
        to_len        = LENGTH(to);
        to_tab        = INTEGER(to);
        //PROTECT(length); /* fake - not to provoke stack imbalance */
    }
    else {
        sub_protected++;
        PROTECT(length= ci__prepare_arg_integer(length, "length"));
        from_len      = LENGTH(from);
        from_tab      = INTEGER(from);
        length_len    = LENGTH(length);
        length_tab    = INTEGER(length);
        //PROTECT(to); /* fake - not to provoke stack imbalance */
    }
    return sub_protected;

    /* rchk reports that this function
     * [PB] has possible protection stack imbalance
     *
     * well, of course it does!! -> this is by design, UPROTECTing somewhere else.
     */
}


/**
 * used both in ci_sub and ci_sub_replacement
 */
inline void ci__sub_get_indices(io::IndexedUtf8Input& str_cont, R_len_t& i,
                                  R_len_t& cur_from,  R_len_t& cur_to,
                                  R_len_t& cur_from2, R_len_t& cur_to2)
{
    if (cur_from >= 0) {
        const R_len_t position = ci__sub_nonnegative_index(
            static_cast<std::int64_t>(cur_from)-1
        );
        cur_from2 = str_cont.UChar32_to_UTF8_index_fwd(i, position);
    }
    else {
        const R_len_t position = ci__sub_nonnegative_index(
            -static_cast<std::int64_t>(cur_from)
        );
        cur_from2 = str_cont.UChar32_to_UTF8_index_back(i, position);
    }
    if (cur_to >= 0) {
        ; /* do nothing with cur_to ; 1-based -> 0-based index */
        /* but +1 as we need the next one (bound) */
        cur_to2 = str_cont.UChar32_to_UTF8_index_fwd(i, cur_to);
    }
    else {
        const R_len_t position = ci__sub_nonnegative_index(
            -static_cast<std::int64_t>(cur_to)-1
        );
        cur_to2 = str_cont.UChar32_to_UTF8_index_back(i, position);
    }
}


/**
 * Get substring
 *
 *
 * @param str character vector
 * @param from integer vector (possibly with negative indices)
 * @param to integer vector (possibly with negative indices) or NULL
 * @param length integer vector or NULL
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *    ci_sub
 *
 * @version 0.1-?? (Marek Gagolewski)
 *    Use UTF-8 input and code-point-to-byte indexing
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-01)
 *    Use cached code-point-to-byte indexes
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *    Make StriException-friendly
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-20)
 *    Use indexed UTF-8 input
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-03)
 *    Use ci__sub_prepare_from_to_length()
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.5-9003 (Marek Gagolewski, 2015-08-05)
 *    Bugfix #183: floating point exception when to or length is an empty vector
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-28)
 *    Negative length yields NA
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-07-08)
 *    use_matrix, ignore_negative_length
 */
SEXP ci_sub(SEXP str, SEXP from, SEXP to, SEXP length, SEXP use_matrix, SEXP ignore_negative_length)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    bool use_matrix_1 = ci__prepare_arg_logical_1_notNA(use_matrix, "use_matrix");
    bool ignore_negative_length_1 = ci__prepare_arg_logical_1_notNA(ignore_negative_length, "ignore_negative_length");

    R_len_t str_len       = LENGTH(str);
    R_len_t from_len      = 0;
    R_len_t to_len        = 0;
    R_len_t length_len    = 0;
    int* from_tab         = 0;
    int* to_tab           = 0;
    int* length_tab       = 0;

    R_len_t sub_protected =  1+  /* how many objects to PROTECT on ret? */
                             ci__sub_prepare_from_to_length(from, to, length,
                                     from_len, to_len, length_len, from_tab, to_tab, length_tab, use_matrix_1);

    R_len_t vectorize_len = ci__recycling_rule(true, 3,
                            str_len, from_len, (to_len>length_len)?to_len:length_len);

    if (vectorize_len <= 0) {
        UNPROTECT(sub_protected);
        return Rf_allocVector(STRSXP, 0);
    }

    // Scalar positive bounds need no per-record recycling or index-mode
    // branch; the general path retains negative, length, and matrix forms.
    const bool scalar_bounds = !length_tab && to_tab &&
        from_len == 1 && to_len == 1 &&
        from_tab[0] > 0 && to_tab[0] > 0;
    STRI__ERROR_HANDLER_BEGIN(sub_protected)
    if (scalar_bounds) {
        const SEXP* source = ALTREP(str) ? NULL : STRING_PTR_RO(str);
        SEXP ret;
        STRI__PROTECT(ret = Rf_allocVector(STRSXP, vectorize_len));
        CiSubNormalizer normalizer;
        for (R_len_t i = 0; i < vectorize_len; ++i) {
            const CiSubInput value = normalizer.get(
                ci__sub_source_element(str, source, i)
            );
            if (value.is_na) {
                SET_STRING_ELT(ret, i, NA_STRING);
                continue;
            }

            R_len_t from_byte = 0;
            R_len_t to_byte = 0;
            ci__sub_marked_utf8_range(
                value.data, value.length,
                from_tab[0], to_tab[0], value.is_ascii,
                from_byte, to_byte
            );
            if (to_byte > from_byte) {
                SET_STRING_ELT(
                    ret, i,
                    Rf_mkCharLenCE(
                        value.data+from_byte, to_byte-from_byte,
                        value.is_ascii ? CE_NATIVE : CE_UTF8
                    )
                );
            }
            else {
                SET_STRING_ELT(ret, i, R_BlankString);
            }
        }

        STRI__UNPROTECT_ALL
        return ret;
    }

    io::IndexedUtf8Input str_cont(str, vectorize_len);
    SEXP ret;
    STRI__PROTECT(ret = Rf_allocVector(STRSXP, vectorize_len));
    R_len_t num_negative_length = 0;
    for (R_len_t i = str_cont.vectorize_init();
            i != str_cont.vectorize_end();
            i = str_cont.vectorize_next(i))
    {
        R_len_t cur_from     = from_tab[i % from_len];
        R_len_t cur_to       = (to_tab)?to_tab[i % to_len]:length_tab[i % length_len];
        if (str_cont.isNA(i) || cur_from == NA_INTEGER || cur_to == NA_INTEGER) {
            SET_STRING_ELT(ret, i, NA_STRING);
            continue;
        }

        if (length_tab) {
            if (cur_to == 0) {
                SET_STRING_ELT(ret, i, R_BlankString);
                continue;
            }
            else if (cur_to < 0) {
                SET_STRING_ELT(ret, i, NA_STRING);
                num_negative_length++;
                continue;
            }

            cur_to = ci__sub_length_endpoint(cur_from, cur_to);
        }

        const char* str_cur_s = str_cont.get(i).data();

        R_len_t cur_from2; // UTF-8 byte indices
        R_len_t cur_to2;   // UTF-8 byte indices

        ci__sub_get_indices(str_cont, i, cur_from, cur_to, cur_from2, cur_to2);

        if (cur_to2 > cur_from2) { // just copy
            SET_STRING_ELT(ret, i, Rf_mkCharLenCE(str_cur_s+cur_from2, cur_to2-cur_from2, CE_UTF8));
        }
        else {
            // maybe a warning here?
            SET_STRING_ELT(ret, i, Rf_mkCharLen(NULL, 0));
        }
    }

    if (num_negative_length > 0 && ignore_negative_length_1) {
        // stringx: ignore items corresponding to length<0
        STRI_ASSERT(length_tab)

        SEXP ret_old = ret;
        STRI__PROTECT(ret = Rf_allocVector(STRSXP, vectorize_len-num_negative_length));
        R_len_t k = 0;
        for (R_len_t i = str_cont.vectorize_init();
            i != str_cont.vectorize_end();
            i = str_cont.vectorize_next(i))
        {
            R_len_t cur_from     = from_tab[i % from_len];
            R_len_t cur_to       = length_tab[i % length_len];
            if (!str_cont.isNA(i) && cur_from != NA_INTEGER && cur_to != NA_INTEGER && cur_to < 0) {
                // ignore
            }
            else {
                SET_STRING_ELT(ret, k, STRING_ELT(ret_old, i));
                ++k;
            }
        }
    }

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/**
 * Substring replacement function
 *
 *
 * @param str character vector
 * @param from integer vector (possibly with negative indices)
 * @param to integer vector (possibly with negative indices) or NULL
 * @param length integer vector or NULL
 * @param omit_na logical scalar
 * @param value character vector replacement
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          use UTF-8 input and code-point-to-byte indexing
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-01)
 *          use cached code-point-to-byte indexes
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException-friendly
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-20)
 *          Use indexed UTF-8 input
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-03)
 *          Use ci__sub_prepare_from_to_length()
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.5-9003 (Marek Gagolewski, 2015-08-05)
 *    Bugfix #183: floating point exception when to or length is an empty vector
 *
 * @version 1.0-2 (Marek Gagolewski, 2016-01-31)
 *    FR #199: new arg: `omit_na`
 *    FR #207: allow insertions
 *
 *
 * @version 1.4.3 (Marek Gagolewski, 2019-03-12)
 *    #346: na_omit for `value`
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-28)
 *    negative length does not alter input
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-07-08)
 *    use_matrix
 */
SEXP ci_sub_replacement(SEXP str, SEXP from, SEXP to, SEXP length, SEXP omit_na, SEXP value, SEXP use_matrix)
{
    PROTECT(str   = ci__prepare_arg_string(str, "str"));
    PROTECT(value = ci__prepare_arg_string(value, "value"));
    bool omit_na_1 = ci__prepare_arg_logical_1_notNA(omit_na, "omit_na");
    bool use_matrix_1 = ci__prepare_arg_logical_1_notNA(use_matrix, "use_matrix");

    R_len_t value_len     = LENGTH(value);
    R_len_t str_len       = LENGTH(str);
    R_len_t from_len      = 0; // see below
    R_len_t to_len        = 0; // see below
    R_len_t length_len    = 0; // see below
    int* from_tab         = 0; // see below
    int* to_tab           = 0; // see below
    int* length_tab       = 0; // see below

    R_len_t sub_protected =  2+ /* how many objects to PROTECT on ret? */
                             ci__sub_prepare_from_to_length(from, to, length,
                                     from_len, to_len, length_len, from_tab, to_tab, length_tab, use_matrix_1);

    R_len_t vectorize_len = ci__recycling_rule(true, 4,
                            str_len, value_len, from_len, (to_len>length_len)?to_len:length_len);

    if (vectorize_len <= 0) {
        UNPROTECT(sub_protected);
        return Rf_allocVector(STRSXP, 0);
    }

    const bool scalar_bounds = !length_tab && to_tab &&
        from_len == 1 && to_len == 1 && value_len == 1 &&
        from_tab[0] > 0 && to_tab[0] > 0;
    // Shared positive bounds and replacement remove recycling branches from
    // the record loop; other index modes retain the copied implementation.
    STRI__ERROR_HANDLER_BEGIN(sub_protected)
    if (scalar_bounds) {
        const SEXP* sources = ALTREP(str) ? NULL : STRING_PTR_RO(str);
        CiSubNormalizer replacement_normalizer;
        const CiSubInput replacement = replacement_normalizer.get(
            STRING_ELT(value, 0)
        );
        CiSubNormalizer source_normalizer;
        SEXP ret;
        STRI__PROTECT(ret = Rf_allocVector(STRSXP, vectorize_len));
        String8buf buf(0);

        for (R_len_t i = 0; i < vectorize_len; ++i) {
            const CiSubInput source = source_normalizer.get(
                ci__sub_source_element(str, sources, i)
            );
            if (source.is_na) {
                SET_STRING_ELT(ret, i, NA_STRING);
                continue;
            }
            if (replacement.is_na) {
                if (omit_na_1) {
                    SET_STRING_ELT(
                        ret, i,
                        Rf_mkCharLenCE(
                            source.data, source.length,
                            source.is_ascii ? CE_NATIVE : CE_UTF8
                        )
                    );
                }
                else {
                    SET_STRING_ELT(ret, i, NA_STRING);
                }
                continue;
            }

            R_len_t from_byte = 0;
            R_len_t to_byte = 0;
            ci__sub_marked_utf8_range(
                source.data, source.length, from_tab[0], to_tab[0],
                source.is_ascii, from_byte, to_byte
            );
            if (to_byte < from_byte)
                to_byte = from_byte;

            const size_t prefix = static_cast<size_t>(from_byte);
            const size_t replacement_length =
                static_cast<size_t>(replacement.length);
            const size_t suffix =
                static_cast<size_t>(source.length-to_byte);
            if (prefix > std::numeric_limits<size_t>::max()-
                    replacement_length ||
                    prefix+replacement_length >
                        std::numeric_limits<size_t>::max()-suffix) {
                throw std::length_error("character output size overflow");
            }
            const size_t output_size = prefix+replacement_length+suffix;
            if (output_size > static_cast<size_t>(R_LEN_T_MAX))
                throw std::length_error("character output is too large");
            const R_len_t output_length =
                static_cast<R_len_t>(output_size);
            buf.resize(output_length, false);
            if (from_byte > 0)
                memcpy(buf.data(), source.data, prefix);
            if (replacement_length > 0) {
                memcpy(
                    buf.data()+from_byte, replacement.data,
                    replacement_length
                );
            }
            if (suffix > 0) {
                memcpy(
                    buf.data()+from_byte+replacement_length,
                    source.data+to_byte, suffix
                );
            }
            SET_STRING_ELT(
                ret, i,
                Rf_mkCharLenCE(
                    buf.data(), output_length,
                    source.is_ascii && replacement.is_ascii
                        ? CE_NATIVE : CE_UTF8
                )
            );
        }

        STRI__UNPROTECT_ALL
        return ret;
    }

    io::IndexedUtf8Input str_cont(str, vectorize_len);
    io::Utf8Input value_cont(value, vectorize_len);
    SEXP ret;
    STRI__PROTECT(ret = Rf_allocVector(STRSXP, vectorize_len));
    String8buf buf(0); // @TODO: estimate bufsize a priori
    for (R_len_t i = str_cont.vectorize_init();
            i != str_cont.vectorize_end();
            i = str_cont.vectorize_next(i))
    {
        R_len_t cur_from     = from_tab[i % from_len];
        R_len_t cur_to       = (to_tab)?to_tab[i % to_len]:length_tab[i % length_len];

        if (str_cont.isNA(i)) {
            SET_STRING_ELT(ret, i, NA_STRING);
            continue;
        }

        if (cur_from == NA_INTEGER || cur_to == NA_INTEGER || value_cont.isNA(i)) {
            if (omit_na_1) {
                const io::Utf8Record& original = str_cont.get(i);
                SET_STRING_ELT(
                    ret, i, Rf_mkCharLenCE(
                        original.data(), original.length(),
                        original.isASCII() ? CE_NATIVE : CE_UTF8
                    )
                );
            }
            else {
                SET_STRING_ELT(ret, i, NA_STRING);
            }
            continue;
        }

        if (!to_tab && cur_to/*length*/ < 0) {  // so not NA
            const io::Utf8Record& original = str_cont.get(i);
            SET_STRING_ELT(
                ret, i, Rf_mkCharLenCE(
                    original.data(), original.length(),
                    original.isASCII() ? CE_NATIVE : CE_UTF8
                )
            );
            continue;
        }

        if (length_tab) {
            if (cur_to <= 0) {
                // SET_STRING_ELT(ret, i, R_BlankString);
                // continue;
                cur_to = 0;
            }
            else {
                cur_to = ci__sub_length_endpoint(cur_from, cur_to);
            }
        }

        const char* str_cur_s   = str_cont.get(i).data();
        R_len_t str_cur_n       = str_cont.get(i).length();
        const char* value_cur_s = value_cont.get(i).data();
        R_len_t value_cur_n     = value_cont.get(i).length();

        R_len_t cur_from2; // UTF-8 byte indices
        R_len_t cur_to2;   // UTF-8 byte indices

        ci__sub_get_indices(str_cont, i, cur_from, cur_to, cur_from2, cur_to2);
        if (cur_to2 < cur_from2) cur_to2 = cur_from2;

        const size_t prefix = static_cast<size_t>(cur_from2);
        const size_t replacement = static_cast<size_t>(value_cur_n);
        const size_t suffix = static_cast<size_t>(str_cur_n-cur_to2);
        size_t output_size = ci__sub_checked_output_size(prefix, replacement);
        output_size = ci__sub_checked_output_size(output_size, suffix);
        buf.resize(output_size, false/*destroy contents*/);
        if (prefix > 0)
            memcpy(buf.data(), str_cur_s, prefix);
        if (replacement > 0)
            memcpy(buf.data()+prefix, value_cur_s, replacement);
        if (suffix > 0) {
            memcpy(
                buf.data()+prefix+replacement, str_cur_s+cur_to2, suffix
            );
        }
        SET_STRING_ELT(ret, i, Rf_mkCharLenCE(
            buf.data(), static_cast<R_len_t>(output_size), CE_UTF8
        ));
    }

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}



/**
 * Extract multiple substrings
 *
 *
 * @param str character vector
 * @param from list
 * @param to list
 * @param length list
 * @return list of character vectors
 *
 * @version 1.3.2 (Marek Gagolewski, 2019-02-21)
 *    #30: new function
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-28)
 *    negative length yields NA
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-07-08)
 *    use_matrix, ignore_negative_length
 */
SEXP ci_sub_all(SEXP str, SEXP from, SEXP to, SEXP length, SEXP use_matrix, SEXP ignore_negative_length)
{
    PROTECT(str    = ci__prepare_arg_string(str, "str"));
    PROTECT(from   = ci__prepare_arg_list(from, "from"));
    PROTECT(to     = ci__prepare_arg_list(to, "to"));
    PROTECT(length = ci__prepare_arg_list(length, "length"));
//     bool use_matrix_1 = ci__prepare_arg_logical_1_notNA(use_matrix, "use_matrix");

    R_len_t str_len       = LENGTH(str);
    R_len_t from_len      = LENGTH(from);
    // R_len_t to_len        = LENGTH(to);
    // R_len_t length_len    = LENGTH(length);


    R_len_t vectorize_len;
    if (!Rf_isNull(to))
        vectorize_len = ci__recycling_rule(true, 3,
                                             str_len, from_len, LENGTH(to));
    else if (!Rf_isNull(length))
        vectorize_len = ci__recycling_rule(true, 3,
                                             str_len, from_len, LENGTH(length));
    else
        vectorize_len = ci__recycling_rule(true, 2, str_len, from_len);

    if (vectorize_len <= 0) {
        UNPROTECT(4);
        return Rf_allocVector(VECSXP, 0);
    }

    int scalar_from = 0;
    int scalar_to = 0;
    const bool scalar_bounds = Rf_isNull(length) && !Rf_isNull(to) &&
        ci__sub_plain_list_integer_scalar(from, from_len, scalar_from) &&
        ci__sub_plain_list_integer_scalar(to, LENGTH(to), scalar_to) &&
        scalar_from > 0 && scalar_to > 0;

    // The copied implementation re-enters ci_sub for every outer element.
    // One source container is enough for the common singleton-index shape.
    if (scalar_bounds) {
        (void)ci__prepare_arg_logical_1_notNA(use_matrix, "use_matrix");
        (void)ci__prepare_arg_logical_1_notNA(
            ignore_negative_length, "ignore_negative_length"
        );

        STRI__ERROR_HANDLER_BEGIN(4)
        io::IndexedUtf8Input str_cont(str, vectorize_len);
        SEXP ret;
        STRI__PROTECT(ret = Rf_allocVector(VECSXP, vectorize_len));

        for (R_len_t i = 0; i < vectorize_len; ++i) {
            SEXP inner;
            STRI__PROTECT(inner = Rf_allocVector(STRSXP, 1));
            if (str_cont.isNA(i)) {
                SET_STRING_ELT(inner, 0, NA_STRING);
            }
            else {
                R_len_t cur_from = scalar_from;
                R_len_t cur_to = scalar_to;
                R_len_t from_byte = 0;
                R_len_t to_byte = 0;
                R_len_t source_index = i;
                ci__sub_get_indices(
                    str_cont, source_index, cur_from, cur_to,
                    from_byte, to_byte
                );
                if (to_byte > from_byte) {
                    const io::Utf8Record& source = str_cont.get(i);
                    SET_STRING_ELT(
                        inner, 0,
                        Rf_mkCharLenCE(
                            source.data()+from_byte,
                            to_byte-from_byte, CE_UTF8
                        )
                    );
                }
                else {
                    SET_STRING_ELT(inner, 0, R_BlankString);
                }
            }
            SET_VECTOR_ELT(ret, i, inner);
            STRI__UNPROTECT(1);
        }

        STRI__UNPROTECT_ALL
        return ret;
        STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
    }


    // no STRI__ERROR_HANDLER_BEGIN  block ---- ci_sub can longjmp with Rf_error...

    SEXP ret, str_tmp, tmp;
    PROTECT(ret = Rf_allocVector(VECSXP, vectorize_len)); //5
    PROTECT(str_tmp = Rf_allocVector(STRSXP, 1));         //6

    for (R_len_t i = 0; i<vectorize_len; ++i)
    {
        PROTECT(tmp = STRING_ELT(str, i%str_len));
        SET_STRING_ELT(str_tmp, 0, tmp);
        UNPROTECT(1); //tmp

        if (!Rf_isNull(to)) {
            PROTECT(tmp = ci_sub(
                str_tmp, VECTOR_ELT(from, i%from_len), VECTOR_ELT(to, i%LENGTH(to)), R_NilValue, use_matrix, ignore_negative_length
            ));
        }
        else if (!Rf_isNull(length)) {
            PROTECT(tmp = ci_sub(
                str_tmp, VECTOR_ELT(from, i%from_len), R_NilValue, VECTOR_ELT(length, i%LENGTH(length)), use_matrix, ignore_negative_length
            ));
        }
        else {
            PROTECT(tmp = ci_sub(
                str_tmp, VECTOR_ELT(from, i%from_len), R_NilValue, R_NilValue, use_matrix, ignore_negative_length
            ));
        }

        SET_VECTOR_ELT(ret, i, tmp);
        UNPROTECT(1); //tmp
    }

    UNPROTECT(6);
    return ret;
}


/** internal function - replace multiple substrings in a single string
 * can raise Rf_error
 *
 *  @version 1.3.2 (Marek Gagolewski, 2019-02-23)
 *
 * @version 1.4.3 (Marek Gagolewski, 2019-03-12)
 *    #346: na_omit for `value`
 *
 * @version 1.4.4 (Marek Gagolewski, 2019-03-13)-
 *    #348: UBSAN runtime error: null pointer passed as argument 1,
 *     which is declared to never be null
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-28)
 *    negative length does not alter input
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-07-08)
 *    use_matrix
 */
SEXP ci__sub_replacement_all_single(
    SEXP curs,
    SEXP from, SEXP to, SEXP length, bool omit_na_1, bool use_matrix_1, SEXP value
) {
    // curs is a CHARSXP in UTF-8

    PROTECT(value = ci_enc_toutf8(value, Rf_ScalarLogical(FALSE), Rf_ScalarLogical(FALSE)));
    R_len_t value_len     = LENGTH(value);

    R_len_t from_len      = 0; // see below
    R_len_t to_len        = 0; // see below
    R_len_t length_len    = 0; // see below
    int* from_tab         = 0; // see below
    int* to_tab           = 0; // see below
    int* length_tab       = 0; // see below

    R_len_t sub_protected = 1+ /* how many objects to PROTECT on ret? */
                            ci__sub_prepare_from_to_length(from, to, length,
                                    from_len, to_len, length_len, from_tab, to_tab, length_tab, use_matrix_1);

    R_len_t vectorize_len = ci__recycling_rule(true, 2, // does not care about value_len
                            from_len, (to_len>length_len)?to_len:length_len);

    if (vectorize_len <= 0) { // "nothing" is being replaced -> return the input as-is
        UNPROTECT(sub_protected);
        return curs;
    }
    if (value_len <= 0) { // things are supposed to be replaced with "nothing"...
        UNPROTECT(sub_protected);
        r_warning(MSG__REPLACEMENT_ZERO);
        return NA_STRING;
    }

    const char* curs_s = CHAR(curs); // already in UTF-8  // TODO: ALTREP will be problematic?
    R_len_t curs_n = LENGTH(curs);

    // first check for NAs....
    if (!omit_na_1) {
        for (R_len_t i=0; i<vectorize_len; ++i) {
            R_len_t cur_from     = from_tab[i % from_len];
            R_len_t cur_to       = (to_tab)?to_tab[i % to_len]:length_tab[i % length_len];
            if (cur_from == NA_INTEGER || cur_to == NA_INTEGER) {
                UNPROTECT(sub_protected);
                if (omit_na_1) return curs;
                else return NA_STRING;
            }
        }

        for (R_len_t i=0; i<vectorize_len; ++i) {
            if (STRING_ELT(value, i%value_len) == NA_STRING) {
                UNPROTECT(sub_protected);
                return NA_STRING;
            }
        }
    }



    // get the number of code points in curs, if required (for negative indexes)
    R_len_t curs_m = -1;
    if (IS_ASCII(curs)) curs_m = curs_n;
    else { // is UTF-8
        curs_m = 0;    // code points count
        R_len_t j = 0; // byte pos
        while (j < curs_n) {
            U8_FWD_1(
                reinterpret_cast<const uint8_t*>(curs_s), j, curs_n
            );
            ++curs_m;
        }
    }

    STRI__ERROR_HANDLER_BEGIN(sub_protected)
    std::vector<char> buf; // convenience >> speed

    R_len_t num_replaced = 0;
    R_len_t last_pos = 0;
    R_len_t byte_pos = 0;
    for (R_len_t i=0; i<vectorize_len; ++i) {
        R_len_t cur_from     = from_tab[i % from_len];
        R_len_t cur_to       = (to_tab)?to_tab[i % to_len]:length_tab[i % length_len];

        if (
            cur_from == NA_INTEGER ||
            cur_to == NA_INTEGER ||
            STRING_ELT(value, i%value_len) == NA_STRING ||
            (!to_tab && cur_to/*length*/ < 0)
        ) {
            // omit_na is true or negative length
            continue;
        }

        num_replaced++;

        cur_from = ci__sub_replacement_all_from(cur_from, curs_m);

        // cur_from is in [0, curs_m]

        cur_to = ci__sub_replacement_all_to(
            cur_to, length_tab != NULL, cur_from, curs_m
        );

        // the chunk to replace is at code points [cur_from, cur_to)

        // Rprintf("orig [%d,%d) repl [%d,%d)\n", last_pos, cur_from, cur_from, cur_to);

        if (last_pos > cur_from)
            throw StriException(MSG__OVERLAPPING_OR_UNSORTED_INDEXES);

        // first, copy [last_pos, cur_from)
        R_len_t byte_pos_last = byte_pos;
        while (last_pos < cur_from) {
            U8_FWD_1(
                reinterpret_cast<const uint8_t*>(curs_s), byte_pos,
                curs_n
            );
            ++last_pos;
        }

        if (byte_pos-byte_pos_last > 0) {
            const size_t buf_size = buf.size();
            const size_t copy_length = static_cast<size_t>(
                byte_pos-byte_pos_last
            );
            buf.resize(ci__sub_checked_output_size(buf_size, copy_length));
            if (!buf.data() || !curs_s)
                throw StriException(MSG__MEM_ALLOC_ERROR);
            memcpy(
                buf.data()+buf_size, curs_s+byte_pos_last, copy_length
            );
        }

        // then, copy the corresponding replacement string
        SEXP value_cur = STRING_ELT(value, i%value_len);
        const char* value_s = CHAR(value_cur);  // TODO: ALTREP will be problematic?
        const size_t value_n = static_cast<size_t>(LENGTH(value_cur));
        if (value_n > 0) {
            const size_t buf_size = buf.size();
            buf.resize(ci__sub_checked_output_size(buf_size, value_n));
            if (!buf.data() || !value_s)
                throw StriException(MSG__MEM_ALLOC_ERROR);
            memcpy(buf.data()+buf_size, value_s, value_n);
        }


        // lastly, update last_pos
        // ---> last_pos = cur_to;
        while (last_pos < cur_to) {
            U8_FWD_1(
                reinterpret_cast<const uint8_t*>(curs_s), byte_pos,
                curs_n
            );
            ++last_pos;
        }
    }

    // finally, copy [last_pos, curs_m)
    if (curs_n-byte_pos > 0) {
        const size_t buf_size = buf.size();
        const size_t copy_length = static_cast<size_t>(curs_n-byte_pos);
        buf.resize(ci__sub_checked_output_size(buf_size, copy_length));
        if (!buf.data() || !curs_s)
            throw StriException(MSG__MEM_ALLOC_ERROR);
        memcpy(buf.data()+buf_size, curs_s+byte_pos, copy_length);
    }

    // only warn if not NA
    if (num_replaced > 0 && vectorize_len % value_len != 0)
        r_warning(MSG__WARN_RECYCLING_RULE2);

    SEXP ret;
    STRI__PROTECT(ret = Rf_mkCharLenCE(
        buf.data(), static_cast<R_len_t>(buf.size()), CE_UTF8
    ));
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/**
 * Replace multiple substrings
 *
 *
 * @param str character vector
 * @param from integer vector (possibly with negative indices)
 * @param to integer vector (possibly with negative indices) or NULL
 * @param length integer vector or NULL
 * @param omit_na logical scalar
 * @param value character vector replacement
 * @return character vector
 *
 * @version 1.3.2 (Marek Gagolewski, 2019-02-22)
 *    #30: new function
 *
 *
 * @version 1.4.3 (Marek Gagolewski, 2019-03-12)
 *    #346: na_omit for `value`
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-28)
 *    negative length does not alter input
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-07-08)
 *    use_matrix
 */
SEXP ci_sub_replacement_all(SEXP str, SEXP from, SEXP to, SEXP length, SEXP omit_na, SEXP value, SEXP use_matrix)
{
    //PROTECT(str    = ci__prepare_arg_string(str, "str"));
    PROTECT(str = ci_enc_toutf8(str, Rf_ScalarLogical(FALSE), Rf_ScalarLogical(FALSE)));
    PROTECT(from   = ci__prepare_arg_list(from, "from"));
    PROTECT(to     = ci__prepare_arg_list(to, "to"));
    PROTECT(length = ci__prepare_arg_list(length, "length"));
    PROTECT(value  = ci__prepare_arg_list(value, "value"));
    bool omit_na_1 = ci__prepare_arg_logical_1_notNA(omit_na, "omit_na");
    bool use_matrix_1 = ci__prepare_arg_logical_1_notNA(use_matrix, "use_matrix");

    R_len_t str_len       = LENGTH(str);
    R_len_t from_len      = LENGTH(from);
    R_len_t value_len     = LENGTH(value);


    R_len_t vectorize_len;
    if (!Rf_isNull(to))
        vectorize_len = ci__recycling_rule(true, 4,
                                             str_len, from_len, value_len, LENGTH(to));
    else if (!Rf_isNull(length))
        vectorize_len = ci__recycling_rule(true, 4,
                                             str_len, from_len, value_len, LENGTH(length));
    else
        vectorize_len = ci__recycling_rule(true, 3, str_len, from_len, value_len);

    if (vectorize_len <= 0) {
        UNPROTECT(5);
        return Rf_allocVector(STRSXP, 0);
    }

    int scalar_from = 0;
    int scalar_to = 0;
    SEXP scalar_value = R_NilValue;
    const bool scalar_bounds = Rf_isNull(length) && !Rf_isNull(to) &&
        ci__sub_plain_list_integer_scalar(from, from_len, scalar_from) &&
        ci__sub_plain_list_integer_scalar(to, LENGTH(to), scalar_to) &&
        scalar_from > 0 && scalar_to > 0;
    const bool scalar_replacement = value_len == 1 &&
        TYPEOF(value) == VECSXP && !Rf_isObject(value) &&
        !ALTREP(value) && NO_ATTRIB(value) &&
        ((scalar_value = VECTOR_ELT(value, 0)),
         TYPEOF(scalar_value) == STRSXP && !Rf_isObject(scalar_value) &&
         !ALTREP(scalar_value) && NO_ATTRIB(scalar_value) &&
         XLENGTH(scalar_value) == 1);

    // Shared singleton bounds and replacement are prepared once instead of
    // re-entering the single-string replacement kernel for every source.
    if (scalar_bounds && scalar_replacement) {
        SEXP replacement;
        PROTECT(replacement = ci_enc_toutf8(
            scalar_value, Rf_ScalarLogical(FALSE),
            Rf_ScalarLogical(FALSE)
        ));
        SEXP replacement_charsxp = STRING_ELT(replacement, 0);
        SEXP ret;
        PROTECT(ret = Rf_allocVector(STRSXP, vectorize_len));

        STRI__ERROR_HANDLER_BEGIN(7)
        String8buf buf(0);

        for (R_len_t i = 0; i < vectorize_len; ++i) {
            SEXP source = STRING_ELT(str, i % str_len);
            if (source == NA_STRING ||
                    (!omit_na_1 && replacement_charsxp == NA_STRING)) {
                SET_STRING_ELT(ret, i, NA_STRING);
                continue;
            }
            if (replacement_charsxp == NA_STRING) {
                SET_STRING_ELT(ret, i, source);
                continue;
            }

            const char* source_data = CHAR(source);
            const R_len_t source_length = LENGTH(source);
            const bool source_ascii = IS_ASCII(source);
            R_len_t from_byte = ci__sub_positive_utf8_boundary(
                source_data, source_length, scalar_from-1, source_ascii
            );
            R_len_t to_byte = ci__sub_positive_utf8_boundary(
                source_data, source_length, scalar_to, source_ascii
            );
            if (to_byte < from_byte)
                to_byte = from_byte;

            const char* replacement_data = CHAR(replacement_charsxp);
            const size_t prefix = static_cast<size_t>(from_byte);
            const size_t replacement_length = static_cast<size_t>(
                LENGTH(replacement_charsxp)
            );
            const size_t suffix = static_cast<size_t>(source_length-to_byte);
            if (prefix > std::numeric_limits<size_t>::max()-
                    replacement_length ||
                    prefix+replacement_length >
                        std::numeric_limits<size_t>::max()-suffix) {
                throw std::length_error("character output size overflow");
            }
            const size_t output_size = prefix+replacement_length+suffix;
            if (output_size > static_cast<size_t>(R_LEN_T_MAX))
                throw std::length_error("character output is too large");
            const R_len_t output_length = static_cast<R_len_t>(output_size);
            buf.resize(output_length, false);
            if (from_byte > 0)
                memcpy(buf.data(), source_data, prefix);
            if (replacement_length > 0) {
                memcpy(
                    buf.data()+prefix, replacement_data,
                    replacement_length
                );
            }
            if (suffix > 0) {
                memcpy(
                    buf.data()+prefix+replacement_length,
                    source_data+to_byte, suffix
                );
            }
            SET_STRING_ELT(
                ret, i,
                Rf_mkCharLenCE(buf.data(), output_length, CE_UTF8)
            );
        }

        STRI__UNPROTECT_ALL
        return ret;
        STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
    }

// no STRI__ERROR_HANDLER_BEGIN  block ---- below we can longjmp with Rf_error...

    SEXP ret, curs, tmp;
    PROTECT(ret = Rf_allocVector(STRSXP, vectorize_len)); // 6
    for (R_len_t i = 0; i<vectorize_len; ++i)
    {
        curs = STRING_ELT(str, i%str_len);
        if (curs == NA_STRING) {
            SET_STRING_ELT(ret, i, NA_STRING);
            continue;
        }

        if (!Rf_isNull(to)) {
            PROTECT(tmp = ci__sub_replacement_all_single(curs,
                          VECTOR_ELT(from, i%from_len),
                          VECTOR_ELT(to, i%LENGTH(to)), R_NilValue,
                          omit_na_1, use_matrix_1, VECTOR_ELT(value, i%value_len)));
        }
        else if (!Rf_isNull(length)) {
            PROTECT(tmp = ci__sub_replacement_all_single(curs,
                          VECTOR_ELT(from, i%from_len),
                          R_NilValue, VECTOR_ELT(length, i%LENGTH(length)),
                          omit_na_1, use_matrix_1, VECTOR_ELT(value, i%value_len)));
        }
        else {
            PROTECT(tmp = ci__sub_replacement_all_single(curs,
                          VECTOR_ELT(from, i%from_len),
                          R_NilValue, R_NilValue,
                          omit_na_1, use_matrix_1, VECTOR_ELT(value, i%value_len)));
        }

        SET_STRING_ELT(ret, i, tmp);
        UNPROTECT(1); //tmp
    }

    UNPROTECT(6);
    return ret;
}

} } // namespace charr::base_backend
