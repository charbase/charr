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
#include "../shared/native_to_utf8.h"
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>

namespace charr { namespace altrep_backend {


namespace sub {

struct CiSubDirectInput {
    const char* data;
    R_len_t length;
    bool is_na;
    bool is_ascii;
};


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


class CiSubDirectNormalizer {
private:
    charr::shared::NativeToUtf8 converter_;

public:
    CiSubDirectInput get(const charport::StrView& value)
    {
        if (value.is_na())
            return CiSubDirectInput{NULL, 0, true, false};
        if (value.ptr == NULL || value.len < 0)
            throw std::runtime_error("Reader returned an invalid string view");

        const char* data = value.ptr;
        R_len_t length = value.len;
        if (value.enc == cetype_ext_t::CE_ASCII)
            return CiSubDirectInput{data, length, false, true};
        if (value.enc == cetype_ext_t::CE_BYTES)
            throw StriException(MSG__BYTESENC);

        if (value.enc == cetype_ext_t::CE_UTF8 ||
                value.enc == cetype_ext_t::CE_ASCII_OR_UTF8) {
            const bool ascii =
                value.enc == cetype_ext_t::CE_ASCII_OR_UTF8 &&
                ci::is_ascii(data, static_cast<size_t>(length));
            if (!ascii && STRI__ENC_HAS_BOM_UTF8(data, length)) {
                data += 3;
                length -= 3;
            }
            return CiSubDirectInput{data, length, false, ascii};
        }

        shared::ByteView converted;
        if (value.enc == cetype_ext_t::CE_LATIN1) {
            converted = converter_.latin1(data, length);
        }
        else if (value.enc == cetype_ext_t::CE_NATIVE) {
            const bool native_has_bom =
                STRI__ENC_HAS_BOM_UTF8(data, length);
            converted = converter_.native(data, length);
            if (native_has_bom &&
                    STRI__ENC_HAS_BOM_UTF8(
                        converted.ptr, converted.len
                    )) {
                return CiSubDirectInput{
                    converted.ptr+3, converted.len-3, false, false
                };
            }
        }
        else {
            throw std::runtime_error(
                "Reader returned an unknown string encoding"
            );
        }
        return CiSubDirectInput{
            converted.ptr, converted.len, false, false
        };
    }
};


cetype_ext_t ci__sub_direct_encoding(const CiSubDirectInput& value)
{
    return value.is_ascii
        ? cetype_ext_t::CE_ASCII
        : cetype_ext_t::CE_UTF8;
}


void ci__sub_reader_views(
    charport::Reader& reader, R_len_t expected_size,
    charport::StrViews& views
)
{
    if (reader.size() != expected_size)
        throw std::runtime_error(
            "character vector length changed during an operation"
        );
    reader.views(0, reader.size(), views);
}


void ci__sub_direct_range(
    const CiSubDirectInput& value, R_len_t from, R_len_t to,
    R_len_t& from_byte, R_len_t& to_byte
)
{
    const R_len_t from_target = from-1;
    if (value.is_ascii) {
        from_byte = std::min(from_target, value.length);
        to_byte = std::min(to, value.length);
        return;
    }

    from_byte = from_target <= 0 ? 0 : value.length;
    to_byte = to <= 0 ? 0 : value.length;
    const R_len_t target = std::max(from_target, to);
    R_len_t byte = 0;
    R_len_t current = 0;
    while (current < target && byte < value.length) {
        U8_FWD_1(
            reinterpret_cast<const uint8_t*>(value.data),
            byte, value.length
        );
        ++current;
        if (current == from_target)
            from_byte = byte;
        if (current == to)
            to_byte = byte;
    }
}


void ci__sub_direct_extract(
    SEXP source, R_len_t source_length,
    R_len_t from, R_len_t to,
    charport::charvec::Store& output
)
{
    charport::Reader reader(ci::protected_reader_resolve(source));
    charport::StrViews views;
    ci__sub_reader_views(reader, source_length, views);

    charport::charvec::Builder builder(source_length);
    CiSubDirectNormalizer normalizer;
    for (R_len_t i = 0; i < source_length; ++i) {
        const CiSubDirectInput value = normalizer.get(views[i]);
        if (value.is_na) {
            builder.set_na(i);
            continue;
        }
        R_len_t from_byte = 0;
        R_len_t to_byte = 0;
        ci__sub_direct_range(value, from, to, from_byte, to_byte);
        if (to_byte > from_byte) {
            ci::builder_set(
                builder, i, value.data+from_byte,
                static_cast<size_t>(to_byte-from_byte),
                value.is_ascii
                    ? cetype_ext_t::CE_ASCII
                    : cetype_ext_t::CE_ASCII_OR_UTF8
            );
        }
        else {
            ci::builder_set(
                builder, i, "", 0, cetype_ext_t::CE_ASCII
            );
        }
    }
    output = builder.release_store();
}


void ci__sub_direct_replace(
    SEXP source, R_len_t source_length,
    SEXP replacement, R_len_t replacement_length,
    R_len_t from, R_len_t to, bool omit_na,
    charport::charvec::Store& output
)
{
    charport::Reader source_reader(ci::protected_reader_resolve(source));
    charport::Reader replacement_reader(
        ci::protected_reader_resolve(replacement)
    );
    charport::StrViews source_views;
    charport::StrViews replacement_views;
    ci__sub_reader_views(source_reader, source_length, source_views);
    ci__sub_reader_views(
        replacement_reader, replacement_length, replacement_views
    );

    CiSubDirectNormalizer replacement_normalizer;
    const CiSubDirectInput replacement_value =
        replacement_normalizer.get(replacement_views[0]);

    charport::charvec::Builder builder(source_length);
    CiSubDirectNormalizer source_normalizer;
    for (R_len_t i = 0; i < source_length; ++i) {
        const CiSubDirectInput value =
            source_normalizer.get(source_views[i]);
        if (value.is_na) {
            builder.set_na(i);
            continue;
        }
        if (replacement_value.is_na) {
            if (omit_na) {
                ci::builder_set(
                    builder, i, value.data,
                    static_cast<size_t>(value.length),
                    ci__sub_direct_encoding(value)
                );
            }
            else {
                builder.set_na(i);
            }
            continue;
        }
        R_len_t from_byte = 0;
        R_len_t to_byte = 0;
        ci__sub_direct_range(value, from, to, from_byte, to_byte);
        if (to_byte < from_byte)
            to_byte = from_byte;

        const size_t prefix = static_cast<size_t>(from_byte);
        const size_t replacement_size =
            static_cast<size_t>(replacement_value.length);
        const size_t suffix = static_cast<size_t>(value.length-to_byte);
        if (prefix > std::numeric_limits<size_t>::max()-replacement_size ||
                prefix+replacement_size >
                    std::numeric_limits<size_t>::max()-suffix) {
            throw std::length_error("character output size overflow");
        }
        const size_t output_size = prefix+replacement_size+suffix;
        bool output_ascii = false;
        if (value.is_ascii) {
            output_ascii = replacement_value.is_ascii;
        }
        else if (replacement_value.is_ascii) {
            output_ascii = ci::is_ascii(value.data, prefix) &&
                ci::is_ascii(value.data+to_byte, suffix);
        }
        char* destination = builder.reserve(
            i, output_size,
            output_ascii
                ? cetype_ext_t::CE_ASCII
                : cetype_ext_t::CE_UTF8
        );
        if (prefix > 0)
            memcpy(destination, value.data, prefix);
        if (replacement_size > 0) {
            memcpy(
                destination+prefix, replacement_value.data,
                replacement_size
            );
        }
        if (suffix > 0) {
            memcpy(
                destination+prefix+replacement_size,
                value.data+to_byte, suffix
            );
        }
    }
    output = builder.release_store();
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
// Deviation from stringi: callers with live operation state pass a queue so
// controlled index diagnostics are signalled after that state is released.
R_len_t ci__sub_prepare_from_to_length(SEXP& from, SEXP& to, SEXP& length,
        R_len_t& from_len, R_len_t& to_len, R_len_t& length_len,
        int*& from_tab, int*& to_tab, int*& length_tab, bool use_matrix_1,
        ci::DeferredWarnings* warnings=NULL)
{
    R_len_t sub_protected = 0;
    try {
        bool from_ismatrix = use_matrix_1 && Rf_isMatrix(from);
        if (from_ismatrix) {
            SEXP t;
            PROTECT(t = Rf_getAttrib(from, R_DimSymbol));
            if (INTEGER(t)[1] == 1)
                from_ismatrix = false; /* it's a column vector */
            else if (INTEGER(t)[1] > 2) {
                /* error() is allowed here */
                UNPROTECT(1); // t
                if (warnings) {
                    throw StriException(
                        MSG__ARG_EXPECTED_MATRIX_WITH_GIVEN_COLUMNS,
                        "from", 2
                    );
                }
                Rf_error(
                    MSG__ARG_EXPECTED_MATRIX_WITH_GIVEN_COLUMNS, "from", 2
                );
            }
            UNPROTECT(1);  // t
        }

        PROTECT(from = ci__prepare_arg_integer(
            from, "from", true, true, warnings
        ));
        sub_protected++;
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
            PROTECT(to = ci__prepare_arg_integer(
                to, "to", true, true, warnings
            ));
            sub_protected++;
            from_len      = LENGTH(from);
            from_tab      = INTEGER(from);
            to_len        = LENGTH(to);
            to_tab        = INTEGER(to);
            //PROTECT(length); /* fake - not to provoke stack imbalance */
        }
        else {
            PROTECT(length = ci__prepare_arg_integer(
                length, "length", true, true, warnings
            ));
            sub_protected++;
            from_len      = LENGTH(from);
            from_tab      = INTEGER(from);
            length_len    = LENGTH(length);
            length_tab    = INTEGER(length);
            //PROTECT(to); /* fake - not to provoke stack imbalance */
        }
        return sub_protected;
    }
    catch (...) {
        // Deviation from stringi: deferred argument errors are C++
        // exceptions, so release any protections acquired before the error.
        UNPROTECT(sub_protected);
        throw;
    }

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


// Deviation from stringi: share the Builder-producing core with `ci_sub_all`
// so it does not create a scalar CHARSXP and call back through `ci_sub()`.
charport::charvec::Store ci__sub_build(
    io::IndexedUtf8Input& str_cont,
    R_len_t from_len, R_len_t to_len, R_len_t length_len,
    int* from_tab, int* to_tab, int* length_tab,
    bool ignore_negative_length,
    charport::charvec::Builder& builder,
    charport::charvec::Builder& filtered
)
{
    const R_len_t vectorize_len = str_cont.get_nrecycle();
    if (vectorize_len == 1) {
        R_len_t i = str_cont.vectorize_init();
        R_len_t cur_from = from_tab[i % from_len];
        R_len_t cur_to = to_tab
            ? to_tab[i % to_len]
            : length_tab[i % length_len];
        if (str_cont.isNA(i) || cur_from == NA_INTEGER ||
                cur_to == NA_INTEGER) {
            return charport::charvec::Store::scalar(
                NULL, 0, cetype_ext_t::CE_NA
            );
        }

        if (length_tab) {
            if (cur_to == 0) {
                return ci::scalar_store(
                    "", 0, cetype_ext_t::CE_ASCII
                );
            }
            else if (cur_to < 0) {
                if (ignore_negative_length)
                    return charport::charvec::Store(0, 0);
                return charport::charvec::Store::scalar(
                    NULL, 0, cetype_ext_t::CE_NA
                );
            }

            cur_to = ci__sub_length_endpoint(cur_from, cur_to);
        }

        const char* str_cur_s = str_cont.get(i).data();
        R_len_t cur_from2;
        R_len_t cur_to2;
        ci__sub_get_indices(
            str_cont, i, cur_from, cur_to, cur_from2, cur_to2
        );
        if (cur_to2 > cur_from2) {
            return ci::scalar_store(
                str_cur_s+cur_from2,
                static_cast<size_t>(cur_to2-cur_from2),
                cetype_ext_t::CE_ASCII_OR_UTF8
            );
        }
        return ci::scalar_store(
            "", 0, cetype_ext_t::CE_ASCII
        );
    }

    builder.reset(vectorize_len);
    R_len_t num_negative_length = 0;

    for (R_len_t i = str_cont.vectorize_init();
            i != str_cont.vectorize_end();
            i = str_cont.vectorize_next(i))
    {
        R_len_t cur_from = from_tab[i % from_len];
        R_len_t cur_to = to_tab
            ? to_tab[i % to_len]
            : length_tab[i % length_len];
        if (str_cont.isNA(i) || cur_from == NA_INTEGER ||
                cur_to == NA_INTEGER) {
            builder.set_na(i);
            continue;
        }

        if (length_tab) {
            if (cur_to == 0) {
                ci::builder_set(
                    builder, i, "", 0, cetype_ext_t::CE_ASCII
                );
                continue;
            }
            else if (cur_to < 0) {
                builder.set_na(i);
                num_negative_length++;
                continue;
            }

            cur_to = ci__sub_length_endpoint(cur_from, cur_to);
        }

        const char* str_cur_s = str_cont.get(i).data();
        R_len_t cur_from2;
        R_len_t cur_to2;

        ci__sub_get_indices(
            str_cont, i, cur_from, cur_to, cur_from2, cur_to2
        );

        if (cur_to2 > cur_from2) {
            ci::builder_set(
                builder, i, str_cur_s+cur_from2, cur_to2-cur_from2,
                cetype_ext_t::CE_ASCII_OR_UTF8
            );
        }
        else {
            ci::builder_set(
                builder, i, "", 0, cetype_ext_t::CE_ASCII
            );
        }
    }

    if (num_negative_length > 0 && ignore_negative_length) {
        STRI_ASSERT(length_tab)
        charport::charvec::Store unfiltered = builder.release_store();
        const R_len_t filtered_len = vectorize_len-num_negative_length;
        if (filtered_len == 1) {
            for (R_len_t i = str_cont.vectorize_init();
                    i != str_cont.vectorize_end();
                    i = str_cont.vectorize_next(i))
            {
                R_len_t cur_from = from_tab[i % from_len];
                R_len_t cur_to = length_tab[i % length_len];
                if (!str_cont.isNA(i) && cur_from != NA_INTEGER &&
                        cur_to != NA_INTEGER && cur_to < 0) {
                    continue;
                }
                const charport::StrView value = unfiltered.view(
                    static_cast<size_t>(i)
                );
                return charport::charvec::Store::scalar(
                    value.ptr, static_cast<size_t>(value.len), value.enc
                );
            }
            throw std::logic_error("substring result count mismatch");
        }

        filtered.reset(filtered_len);
        R_len_t k = 0;
        for (R_len_t i = str_cont.vectorize_init();
                i != str_cont.vectorize_end();
                i = str_cont.vectorize_next(i))
        {
            R_len_t cur_from = from_tab[i % from_len];
            R_len_t cur_to = length_tab[i % length_len];
            if (!str_cont.isNA(i) && cur_from != NA_INTEGER &&
                    cur_to != NA_INTEGER && cur_to < 0) {
                continue;
            }
            filtered.set(k++, unfiltered.view(static_cast<size_t>(i)));
        }
        return filtered.release_store();
    }

    return builder.release_store();
}


charport::charvec::Store ci__sub_build(
    io::IndexedUtf8Input& str_cont,
    R_len_t from_len, R_len_t to_len, R_len_t length_len,
    int* from_tab, int* to_tab, int* length_tab,
    bool ignore_negative_length
)
{
    charport::charvec::Builder builder(0);
    charport::charvec::Builder filtered(0);
    return ci__sub_build(
        str_cont, from_len, to_len, length_len,
        from_tab, to_tab, length_tab, ignore_negative_length,
        builder, filtered
    );
}


namespace sub {

struct CiOwnedUtf8 {
    bool is_na;
    bool is_ascii;
    const char* data;
    R_len_t length;

    CiOwnedUtf8() : is_na(true), is_ascii(false), data(nullptr), length(0)
    {
    }
};


struct CiOwnedUtf8Set {
    std::vector<CiOwnedUtf8> records;
    charr::shared::SliceArena bytes;
};

} // namespace sub


std::unique_ptr<CiOwnedUtf8Set> ci__sub_read_owned(
    ci::ReaderContext& context, SEXP str
)
{
    const R_len_t str_len = ci::checked_r_len(
        context.size(str), "character vectors"
    );
    std::unique_ptr<CiOwnedUtf8Set> output =
        std::make_unique<CiOwnedUtf8Set>();
    output->records.resize(static_cast<size_t>(str_len));
    if (str_len <= 0)
        return output;

    // Normalize before later R argument preparation so the Reader can be
    // released first. One stable arena owns the copied bytes; records remain
    // small views instead of allocating a string object for every element.
    {
        io::Utf8Input str_cont(context, str, str_len);
        for (R_len_t i=0; i<str_len; ++i) {
            CiOwnedUtf8& current = output->records[static_cast<size_t>(i)];
            current.is_na = str_cont.isNA(i);
            if (current.is_na)
                continue;
            const io::Utf8Record& value = str_cont.get(i);
            current.is_ascii = value.isASCII();
            current.length = value.length();
            if (value.length() > 0) {
                char* destination = output->bytes.allocate(
                    static_cast<size_t>(value.length())
                );
                memcpy(
                    destination, value.data(),
                    static_cast<size_t>(value.length())
                );
                current.data = destination;
            }
            else {
                current.data = "";
            }
        }
    }
    return output;
}


const char* ci__sub_owned_data(const CiOwnedUtf8& value)
{
    return value.length == 0 ? "" : value.data;
}


R_len_t ci__sub_owned_positive_boundary(
    const CiOwnedUtf8& value, R_len_t codepoints
)
{
    const R_len_t length = value.length;
    if (codepoints <= 0)
        return 0;
    if (value.is_ascii)
        return std::min(codepoints, length);

    const char* data = ci__sub_owned_data(value);
    R_len_t byte = 0;
    R_len_t current = 0;
    while (current < codepoints && byte < length) {
        U8_FWD_1(reinterpret_cast<const uint8_t*>(data), byte, length);
        ++current;
    }
    return byte;
}


// Deviation from stringi: cumulative replacement output stays in size_t, and
// growth arithmetic is checked before Builder applies R's string-length limit.
size_t ci__sub_checked_output_size(size_t current, size_t additional)
{
    if (additional > std::numeric_limits<size_t>::max()-current)
        throw std::length_error("character output size overflow");
    return current+additional;
}


void ci__sub_builder_set_owned(
    charport::charvec::Builder& builder, R_len_t i,
    const CiOwnedUtf8& value
)
{
    if (value.is_na) {
        builder.set_na(i);
        return;
    }
    ci::builder_set(
        builder, i, ci__sub_owned_data(value),
        static_cast<size_t>(value.length), value.is_ascii ?
            cetype_ext_t::CE_ASCII : cetype_ext_t::CE_UTF8
    );
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
 *    Use io::Utf8Input and ci__UChar32_to_UTF8_index
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-01)
 *    Use io::Utf8Input's UChar32-to-UTF8 index
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *    Make StriException-friendly
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-20)
 *    Use io::IndexedUtf8Input
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

    R_len_t from_len      = 0;
    R_len_t to_len        = 0;
    R_len_t length_len    = 0;
    int* from_tab         = 0;
    int* to_tab           = 0;
    int* length_tab       = 0;

    STRI__ERROR_HANDLER_BEGIN(1)
    R_len_t sub_protected = 0;
    ci::unwind_protect([&]() -> SEXP {
        sub_protected = ci__sub_prepare_from_to_length(
            from, to, length,
            from_len, to_len, length_len,
            from_tab, to_tab, length_tab,
            use_matrix_1, &STRI__DEFERRED_WARNINGS
        );
        return R_NilValue;
    });
    __ci_protected_sexp_num += sub_protected;
    const R_len_t str_len = ci::checked_r_len(
        XLENGTH(str), "character vectors"
    );
    R_len_t vectorize_len = 0;
    ci::unwind_protect([&]() -> SEXP {
        vectorize_len = ci__recycling_rule(
            STRI__DEFERRED_WARNINGS, 3, str_len, from_len,
            (to_len>length_len)?to_len:length_len
        );
        return R_NilValue;
    });

    charport::charvec::Store output(0, 0);
    if (vectorize_len <= 0) {
        charport::charvec::Builder builder(0);
        output = builder.release_store();
    }
    else {
        const bool scalar_bounds = !length_tab && to_tab &&
            from_len == 1 && to_len == 1 &&
            from_tab[0] > 0 && to_tab[0] > 0;
        if (scalar_bounds) {
            ci__sub_direct_extract(
                str, str_len, from_tab[0], to_tab[0], output
            );
        }
        else {
            ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
            {
                io::IndexedUtf8Input str_cont(
                    context, str, vectorize_len
                );
                output = ci__sub_build(
                    str_cont, from_len, to_len, length_len,
                    from_tab, to_tab, length_tab,
                    ignore_negative_length_1
                );
            }
        }
    }

    SEXP ret;
    STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
        return charport::charvec::wrap(std::move(output));
    }));
    // Install the Store finalizer before a deferred warning can unwind.
    STRI__DEFERRED_WARNINGS.emit();
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
 *          use io::Utf8Input and ci__UChar32_to_UTF8_index
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-01)
 *          use io::Utf8Input's UChar32-to-UTF8 index
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException-friendly
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-20)
 *          Use io::IndexedUtf8Input
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

    R_len_t from_len      = 0; // see below
    R_len_t to_len        = 0; // see below
    R_len_t length_len    = 0; // see below
    int* from_tab         = 0; // see below
    int* to_tab           = 0; // see below
    int* length_tab       = 0; // see below

    STRI__ERROR_HANDLER_BEGIN(2)
    R_len_t sub_protected = 0;
    ci::unwind_protect([&]() -> SEXP {
        sub_protected = ci__sub_prepare_from_to_length(
            from, to, length,
            from_len, to_len, length_len,
            from_tab, to_tab, length_tab,
            use_matrix_1, &STRI__DEFERRED_WARNINGS
        );
        return R_NilValue;
    });
    __ci_protected_sexp_num += sub_protected;
    const R_len_t value_len = ci::checked_r_len(
        XLENGTH(value), "character vectors"
    );
    const R_len_t str_len = ci::checked_r_len(
        XLENGTH(str), "character vectors"
    );
    R_len_t vectorize_len = 0;
    ci::unwind_protect([&]() -> SEXP {
        vectorize_len = ci__recycling_rule(
            STRI__DEFERRED_WARNINGS, 4,
            str_len, value_len, from_len,
            (to_len>length_len)?to_len:length_len
        );
        return R_NilValue;
    });

    charport::charvec::Store output(0, 0);
    const bool scalar_bounds = vectorize_len > 0 && !length_tab &&
        to_tab && from_len == 1 && to_len == 1 && value_len == 1 &&
        from_tab[0] > 0 && to_tab[0] > 0;

    if (vectorize_len <= 0) {
        charport::charvec::Builder builder(0);
        output = builder.release_store();
    }
    else if (scalar_bounds) {
        ci__sub_direct_replace(
            str, str_len, value, value_len,
            from_tab[0], to_tab[0], omit_na_1, output
        );
    }
    else {
        ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
        charport::charvec::Builder builder(vectorize_len);
        {
            io::IndexedUtf8Input str_cont(
                context, str, vectorize_len
            );
            io::Utf8Input value_cont(context, value, vectorize_len);
            for (R_len_t i = str_cont.vectorize_init();
                    i != str_cont.vectorize_end();
                    i = str_cont.vectorize_next(i)) {
                R_len_t cur_from = from_tab[i % from_len];
                R_len_t cur_to = to_tab
                    ? to_tab[i % to_len]
                    : length_tab[i % length_len];

                if (str_cont.isNA(i)) {
                    builder.set_na(i);
                    continue;
                }

                if (cur_from == NA_INTEGER || cur_to == NA_INTEGER ||
                        value_cont.isNA(i)) {
                    if (omit_na_1)
                        ci::builder_set(builder, i, str_cont.get(i));
                    else
                        builder.set_na(i);
                    continue;
                }

                if (!to_tab && cur_to/*length*/ < 0) {
                    ci::builder_set(builder, i, str_cont.get(i));
                    continue;
                }

                if (length_tab) {
                    if (cur_to <= 0) {
                        cur_to = 0;
                    }
                    else {
                        cur_to = ci__sub_length_endpoint(cur_from, cur_to);
                    }
                }

                const io::Utf8Record& str_cur = str_cont.get(i);
                const io::Utf8Record& value_cur = value_cont.get(i);
                const char* str_cur_s = str_cur.data();
                R_len_t str_cur_n = str_cur.length();
                const char* value_cur_s = value_cur.data();
                R_len_t value_cur_n = value_cur.length();
                R_len_t cur_from2;
                R_len_t cur_to2;

                ci__sub_get_indices(
                    str_cont, i, cur_from, cur_to, cur_from2, cur_to2
                );
                if (cur_to2 < cur_from2)
                    cur_to2 = cur_from2;

                const size_t prefix = static_cast<size_t>(cur_from2);
                const size_t replacement = static_cast<size_t>(value_cur_n);
                const size_t suffix = static_cast<size_t>(
                    str_cur_n-cur_to2
                );
                size_t output_size = ci__sub_checked_output_size(
                    prefix, replacement
                );
                output_size = ci__sub_checked_output_size(
                    output_size, suffix
                );
                const bool source_ascii = str_cur.isASCII();
                const bool output_ascii =
                    (source_ascii ||
                     ci::is_ascii(str_cur_s, cur_from2)) &&
                    (value_cur.isASCII() ||
                     ci::is_ascii(value_cur_s, value_cur_n)) &&
                    (source_ascii ||
                     ci::is_ascii(
                         str_cur_s+cur_to2, str_cur_n-cur_to2
                     ));
                // The final byte count is known, so fill Builder storage
                // directly instead of staging through String8buf.
                char* output = builder.reserve(
                    i, output_size,
                    output_ascii
                        ? cetype_ext_t::CE_ASCII
                        : cetype_ext_t::CE_UTF8
                );
                if (prefix > 0)
                    memcpy(output, str_cur_s, prefix);
                if (replacement > 0) {
                    memcpy(
                        output+prefix, value_cur_s, replacement
                    );
                }
                if (suffix > 0) {
                    memcpy(
                        output+prefix+replacement,
                        str_cur_s+cur_to2, suffix
                    );
                }
            }
        }
        output = builder.release_store();
    }

    SEXP ret;
    STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
        return charport::charvec::wrap(std::move(output));
    }));
    // Install the Store finalizer before a deferred warning can unwind.
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}



bool ci__sub_all_plain_logical_scalar_not_na(SEXP value)
{
    return TYPEOF(value) == LGLSXP &&
        !Rf_isObject(value) && !ALTREP(value) &&
        LENGTH(value) == 1 && LOGICAL_RO(value)[0] != NA_LOGICAL;
}


bool ci__sub_all_plain_integer_elements(SEXP values, R_len_t values_len)
{
    if (ALTREP(values))
        return false;

    for (R_len_t i=0; i<values_len; ++i) {
        SEXP value = VECTOR_ELT(values, i);
        if (Rf_isNull(value))
            continue;
        if (TYPEOF(value) != INTSXP ||
                Rf_isObject(value) || ALTREP(value)) {
            return false;
        }
    }
    return true;
}


bool ci__sub_all_plain_integer_scalar(SEXP value, int& output)
{
    if (TYPEOF(value) != INTSXP || Rf_isObject(value) || ALTREP(value) ||
            !NO_ATTRIB(value) || XLENGTH(value) != 1) {
        return false;
    }
    output = INTEGER_RO(value)[0];
    return true;
}


bool ci__sub_all_plain_list_scalar(
    SEXP values, R_len_t values_len, int& output
)
{
    return TYPEOF(values) == VECSXP && !Rf_isObject(values) &&
        !ALTREP(values) && NO_ATTRIB(values) && values_len == 1 &&
        ci__sub_all_plain_integer_scalar(VECTOR_ELT(values, 0), output);
}


// Sharing the source Reader is safe only when per-element preparation cannot
// dispatch or emit an immediate R condition. Other index forms keep the
// copied prepare/read order and use a short source borrow for each element.
bool ci__sub_all_can_share_reader(
    SEXP from, R_len_t from_len,
    SEXP to, R_len_t to_len,
    SEXP length, R_len_t length_len,
    SEXP use_matrix, SEXP ignore_negative_length,
    bool has_to, bool has_length
)
{
    if (!ci__sub_all_plain_logical_scalar_not_na(use_matrix) ||
            !ci__sub_all_plain_logical_scalar_not_na(
                ignore_negative_length
            )) {
        return false;
    }
    if (!ci__sub_all_plain_integer_elements(from, from_len))
        return false;
    if (has_to && !ci__sub_all_plain_integer_elements(to, to_len))
        return false;
    if (has_length &&
            !ci__sub_all_plain_integer_elements(length, length_len)) {
        return false;
    }
    return true;
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

    STRI__ERROR_HANDLER_BEGIN(4)
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    const R_len_t str_len = ci::checked_r_len(
        context.size(str), "character vectors"
    );
    R_len_t from_len = 0;
    R_len_t to_len = 0;
    R_len_t length_len = 0;
    bool has_to = false;
    bool has_length = false;
    R_len_t vectorize_len = 0;
    ci::unwind_protect([&]() -> SEXP {
        from_len = LENGTH(from);
        to_len = LENGTH(to);
        length_len = LENGTH(length);
        has_to = !Rf_isNull(to);
        has_length = !Rf_isNull(length);
        if (has_to) {
            vectorize_len = ci__recycling_rule(
                STRI__DEFERRED_WARNINGS, 3,
                str_len, from_len, to_len
            );
        }
        else if (has_length) {
            vectorize_len = ci__recycling_rule(
                STRI__DEFERRED_WARNINGS, 3,
                str_len, from_len, length_len
            );
        }
        else {
            vectorize_len = ci__recycling_rule(
                STRI__DEFERRED_WARNINGS, 2, str_len, from_len
            );
        }
        return R_NilValue;
    });

    SEXP ret;
    STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
        return Rf_allocVector(VECSXP, vectorize_len);
    }));

    int scalar_from = 0;
    int scalar_to = 0;
    const bool scalar_bounds = has_to && !has_length &&
        ci__sub_all_plain_list_scalar(
            from, from_len, scalar_from
        ) &&
        ci__sub_all_plain_list_scalar(to, to_len, scalar_to) &&
        scalar_from > 0 && scalar_to > 0;

    if (scalar_bounds) {
        (void)ci__prepare_arg_logical_1_notNA(
            use_matrix, "use_matrix", &STRI__DEFERRED_WARNINGS
        );
        (void)ci__prepare_arg_logical_1_notNA(
            ignore_negative_length, "ignore_negative_length",
            &STRI__DEFERRED_WARNINGS
        );

        std::vector<charport::charvec::Store> outputs;
        outputs.reserve(static_cast<size_t>(vectorize_len));
        {
            io::IndexedUtf8Input str_cont(
                context, str, vectorize_len
            );
            for (R_len_t i = 0; i < vectorize_len; ++i) {
                if (str_cont.isNA(i)) {
                    outputs.push_back(charport::charvec::Store::scalar(
                        NULL, 0, cetype_ext_t::CE_NA
                    ));
                    continue;
                }

                R_len_t cur_from = scalar_from;
                R_len_t cur_to = scalar_to;
                R_len_t from_byte = 0;
                R_len_t to_byte = 0;
                R_len_t source_index = i;
                ci__sub_get_indices(
                    str_cont, source_index, cur_from, cur_to,
                    from_byte, to_byte
                );
                const io::Utf8Record& source = str_cont.get(i);
                if (to_byte > from_byte) {
                    outputs.push_back(ci::scalar_store(
                        source.data()+from_byte,
                        static_cast<size_t>(to_byte-from_byte),
                        cetype_ext_t::CE_ASCII_OR_UTF8
                    ));
                }
                else {
                    outputs.push_back(ci::scalar_store(
                        "", 0, cetype_ext_t::CE_ASCII
                    ));
                }
            }
        }

        ci::unwind_protect([&]() -> SEXP {
            for (R_len_t i = 0; i < vectorize_len; ++i) {
                SEXP inner = PROTECT(charport::charvec::wrap(
                    std::move(outputs[static_cast<size_t>(i)])
                ));
                SET_VECTOR_ELT(ret, i, inner);
                UNPROTECT(1);
            }
            return R_NilValue;
        });

        context.emitWarnings();
        STRI__UNPROTECT_ALL
        return ret;
    }

    const bool can_share_reader = ci__sub_all_can_share_reader(
        from, from_len, to, to_len, length, length_len,
        use_matrix, ignore_negative_length, has_to, has_length
    );

    // Deviation from stringi: the copied loop creates a scalar STRSXP and
    // calls ci_sub() for every element. Plain scalar flags and integer indices
    // need no user callback, so one Reader and one unwind boundary can serve
    // that same loop. Finished Stores wait in native memory until the shared
    // borrow has ended.
    std::vector<charport::charvec::Store> staged_outputs;
    if (can_share_reader)
        staged_outputs.reserve(static_cast<size_t>(vectorize_len));

    if (can_share_reader) {
        const bool use_matrix_1 = LOGICAL_RO(use_matrix)[0] != 0;
        const bool ignore_negative_length_1 =
            LOGICAL_RO(ignore_negative_length)[0] != 0;
        std::shared_ptr<ci::ReaderBorrow> shared_source_borrow;
        const charport::StrViews* source_views = NULL;
        std::unique_ptr<io::IndexedUtf8Input> str_cont;
        charport::charvec::Builder inner_builder(0);
        charport::charvec::Builder filtered_builder(0);
        charport::charvec::Store inner_output(0, 0);

        ci::unwind_protect([&]() -> SEXP {
            for (R_len_t i=0; i<vectorize_len; ++i) {
                ci::UnwindCallbackProtector callback_protector;
                SEXP inner_from = R_NilValue;
                SEXP inner_to = R_NilValue;
                SEXP inner_length = R_NilValue;
                R_len_t inner_from_len = 0;
                R_len_t inner_to_len = 0;
                R_len_t inner_length_len = 0;
                int* inner_from_tab = NULL;
                int* inner_to_tab = NULL;
                int* inner_length_tab = NULL;
                R_len_t inner_protected = 0;

                inner_from = VECTOR_ELT(from, i%from_len);
                if (has_to)
                    inner_to = VECTOR_ELT(to, i%to_len);
                else if (has_length)
                    inner_length = VECTOR_ELT(length, i%length_len);
                inner_protected = ci__sub_prepare_from_to_length(
                    inner_from, inner_to, inner_length,
                    inner_from_len, inner_to_len, inner_length_len,
                    inner_from_tab, inner_to_tab, inner_length_tab,
                    use_matrix_1, &STRI__DEFERRED_WARNINGS
                );
                callback_protector.adopt(inner_protected);
                const R_len_t inner_vectorize_len = ci__recycling_rule(
                    STRI__DEFERRED_WARNINGS, 3, 1, inner_from_len,
                    (inner_to_len>inner_length_len)
                        ? inner_to_len : inner_length_len
                );

                if (inner_vectorize_len <= 0) {
                    inner_builder.reset(0);
                    inner_output = inner_builder.release_store();
                }
                else {
                    if (!shared_source_borrow) {
                        shared_source_borrow = context.acquire(str);
                        source_views = &shared_source_borrow->views();
                    }
                    str_cont.reset(new io::IndexedUtf8Input(
                        shared_source_borrow,
                        (*source_views)[static_cast<R_xlen_t>(i%str_len)],
                        inner_vectorize_len, true
                    ));
                    inner_output = ci__sub_build(
                        *str_cont,
                        inner_from_len, inner_to_len, inner_length_len,
                        inner_from_tab, inner_to_tab, inner_length_tab,
                        ignore_negative_length_1,
                        inner_builder, filtered_builder
                    );
                    str_cont.reset();
                }

                callback_protector.release(inner_protected);
                staged_outputs.push_back(std::move(inner_output));
            }
            return R_NilValue;
        });
    }
    else {
        charport::charvec::Builder inner_builder(0);
        charport::charvec::Builder filtered_builder(0);

        for (R_len_t i=0; i<vectorize_len; ++i) {
            SEXP inner_from = R_NilValue;
            SEXP inner_to = R_NilValue;
            SEXP inner_length = R_NilValue;
            R_len_t inner_from_len = 0;
            R_len_t inner_to_len = 0;
            R_len_t inner_length_len = 0;
            int* inner_from_tab = NULL;
            int* inner_to_tab = NULL;
            int* inner_length_tab = NULL;
            R_len_t inner_protected = 0;
            R_len_t inner_vectorize_len = 0;
            bool use_matrix_1 = false;
            bool ignore_negative_length_1 = false;

            ci::unwind_protect([&]() -> SEXP {
                use_matrix_1 = ci__prepare_arg_logical_1_notNA(
                    use_matrix, "use_matrix", &STRI__DEFERRED_WARNINGS
                );
                ignore_negative_length_1 =
                    ci__prepare_arg_logical_1_notNA(
                        ignore_negative_length, "ignore_negative_length",
                        &STRI__DEFERRED_WARNINGS
                    );
                inner_from = VECTOR_ELT(from, i%from_len);
                if (has_to)
                    inner_to = VECTOR_ELT(to, i%to_len);
                else if (has_length)
                    inner_length = VECTOR_ELT(length, i%length_len);
                inner_protected = ci__sub_prepare_from_to_length(
                    inner_from, inner_to, inner_length,
                    inner_from_len, inner_to_len, inner_length_len,
                    inner_from_tab, inner_to_tab, inner_length_tab,
                    use_matrix_1, &STRI__DEFERRED_WARNINGS
                );
                return R_NilValue;
            });
            __ci_protected_sexp_num += inner_protected;
            inner_vectorize_len = ci__recycling_rule(
                STRI__DEFERRED_WARNINGS, 3, 1, inner_from_len,
                (inner_to_len>inner_length_len)
                    ? inner_to_len : inner_length_len
            );

            charport::charvec::Store inner_output(0, 0);
            if (inner_vectorize_len <= 0) {
                inner_builder.reset(0);
                inner_output = inner_builder.release_store();
            }
            else {
                {
                    std::shared_ptr<ci::ReaderBorrow> source_borrow;
                    charport::StrView source_view;
                    source_borrow.reset(new ci::ReaderBorrow(str));
                    if (source_borrow->size() != str_len) {
                        throw std::runtime_error(
                            "character vector length changed during an operation"
                        );
                    }
                    source_view = source_borrow->view(
                        static_cast<R_xlen_t>(i%str_len)
                    );
                    io::IndexedUtf8Input str_cont(
                        source_borrow, source_view,
                        inner_vectorize_len, true
                    );
                    inner_output = ci__sub_build(
                        str_cont,
                        inner_from_len, inner_to_len, inner_length_len,
                        inner_from_tab, inner_to_tab, inner_length_tab,
                        ignore_negative_length_1,
                        inner_builder, filtered_builder
                    );
                }
            }

            STRI__UNPROTECT(inner_protected);
            SEXP tmp;
            STRI__PROTECT(tmp = ci::unwind_protect([&]() -> SEXP {
                return charport::charvec::wrap(std::move(inner_output));
            }));
            SET_VECTOR_ELT(ret, i, tmp);
            STRI__UNPROTECT(1);
        }
    }

    if (can_share_reader) {
        ci::unwind_protect([&]() -> SEXP {
            for (R_len_t i=0; i<vectorize_len; ++i) {
                SEXP tmp = PROTECT(charport::charvec::wrap(
                    std::move(staged_outputs[static_cast<size_t>(i)])
                ));
                SET_VECTOR_ELT(ret, i, tmp);
                UNPROTECT(1);
            }
            return R_NilValue;
        });
    }

    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/** internal function - replace multiple substrings in a single string
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
void ci__sub_replacement_all_single(
    charport::charvec::Builder& builder, R_len_t output_index,
    const CiOwnedUtf8& source,
    const std::vector<CiOwnedUtf8>& replacements,
    R_len_t from_len, R_len_t to_len, R_len_t length_len,
    int* from_tab, int* to_tab, int* length_tab,
    R_len_t vectorize_len, bool omit_na,
    ci::ReaderContext& context
)
{
    if (vectorize_len <= 0) {
        ci__sub_builder_set_owned(builder, output_index, source);
        return;
    }

    const R_len_t value_len = static_cast<R_len_t>(replacements.size());
    if (value_len <= 0) {
        context.warn(MSG__REPLACEMENT_ZERO);
        builder.set_na(output_index);
        return;
    }

    if (!omit_na) {
        for (R_len_t i=0; i<vectorize_len; ++i) {
            const R_len_t cur_from = from_tab[i % from_len];
            const R_len_t cur_to = to_tab
                ? to_tab[i % to_len]
                : length_tab[i % length_len];
            if (cur_from == NA_INTEGER || cur_to == NA_INTEGER) {
                builder.set_na(output_index);
                return;
            }
        }

        for (R_len_t i=0; i<vectorize_len; ++i) {
            if (replacements[static_cast<size_t>(i % value_len)].is_na) {
                builder.set_na(output_index);
                return;
            }
        }
    }

    const char* source_data = ci__sub_owned_data(source);
    const R_len_t source_length = source.length;
    R_len_t source_codepoints = -1;
    if (source.is_ascii) {
        source_codepoints = source_length;
    }
    else {
        source_codepoints = 0;
        R_len_t j = 0;
        while (j < source_length) {
            U8_FWD_1(
                reinterpret_cast<const uint8_t*>(source_data), j,
                source_length
            );
            ++source_codepoints;
        }
    }

    std::vector<char> buf;
    R_len_t num_replaced = 0;
    R_len_t last_pos = 0;
    R_len_t byte_pos = 0;
    for (R_len_t i=0; i<vectorize_len; ++i) {
        R_len_t cur_from = from_tab[i % from_len];
        R_len_t cur_to = to_tab
            ? to_tab[i % to_len]
            : length_tab[i % length_len];
        const CiOwnedUtf8& replacement =
            replacements[static_cast<size_t>(i % value_len)];

        if (cur_from == NA_INTEGER || cur_to == NA_INTEGER ||
                replacement.is_na || (!to_tab && cur_to < 0)) {
            continue;
        }

        num_replaced++;

        cur_from = ci__sub_replacement_all_from(
            cur_from, source_codepoints
        );

        cur_to = ci__sub_replacement_all_to(
            cur_to, length_tab != NULL, cur_from, source_codepoints
        );

        if (last_pos > cur_from)
            throw StriException(MSG__OVERLAPPING_OR_UNSORTED_INDEXES);

        const R_len_t byte_pos_last = byte_pos;
        while (last_pos < cur_from) {
            U8_FWD_1(
                reinterpret_cast<const uint8_t*>(source_data), byte_pos,
                source_length
            );
            ++last_pos;
        }

        if (byte_pos-byte_pos_last > 0) {
            const size_t buf_size = buf.size();
            const size_t copy_length = static_cast<size_t>(
                byte_pos-byte_pos_last
            );
            buf.resize(ci__sub_checked_output_size(buf_size, copy_length));
            if (!buf.data() || !source_data)
                throw StriException(MSG__MEM_ALLOC_ERROR);
            memcpy(
                buf.data()+buf_size, source_data+byte_pos_last,
                copy_length
            );
        }

        const size_t replacement_length = static_cast<size_t>(
            replacement.length
        );
        if (replacement_length > 0) {
            const size_t buf_size = buf.size();
            buf.resize(ci__sub_checked_output_size(
                buf_size, replacement_length
            ));
            const char* replacement_data = ci__sub_owned_data(replacement);
            if (!buf.data() || !replacement_data)
                throw StriException(MSG__MEM_ALLOC_ERROR);
            memcpy(
                buf.data()+buf_size, replacement_data, replacement_length
            );
        }

        while (last_pos < cur_to) {
            U8_FWD_1(
                reinterpret_cast<const uint8_t*>(source_data), byte_pos,
                source_length
            );
            ++last_pos;
        }
    }

    if (source_length-byte_pos > 0) {
        const size_t buf_size = buf.size();
        const size_t copy_length = static_cast<size_t>(
            source_length-byte_pos
        );
        buf.resize(ci__sub_checked_output_size(buf_size, copy_length));
        if (!buf.data() || !source_data)
            throw StriException(MSG__MEM_ALLOC_ERROR);
        memcpy(
            buf.data()+buf_size, source_data+byte_pos, copy_length
        );
    }

    if (num_replaced > 0 && vectorize_len % value_len != 0)
        context.warn(MSG__WARN_RECYCLING_RULE2);

    ci::builder_set(
        builder, output_index, buf.empty() ? "" : buf.data(),
        buf.size(), cetype_ext_t::CE_ASCII_OR_UTF8
    );
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
    PROTECT(str = ci__prepare_arg_string(str, "str"));

    STRI__ERROR_HANDLER_BEGIN(1)
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    SEXP ret;
    {
        const std::unique_ptr<CiOwnedUtf8Set> strings_owner =
            ci__sub_read_owned(context, str);
        const std::vector<CiOwnedUtf8>& strings = strings_owner->records;

        const auto prepare_list = [&](SEXP& arg, const char* name) {
            SEXP prepared = R_NilValue;
            ci::unwind_protect([&]() -> SEXP {
                prepared = ci__prepare_arg_list(
                    arg, name, &STRI__DEFERRED_WARNINGS
                );
                PROTECT(prepared);
                return R_NilValue;
            });
            arg = prepared;
            ++__ci_protected_sexp_num;
        };
        prepare_list(from, "from");
        prepare_list(to, "to");
        prepare_list(length, "length");
        prepare_list(value, "value");

        bool omit_na_1 = false;
        bool use_matrix_1 = false;
        R_len_t str_len = 0;
        R_len_t from_len = 0;
        R_len_t to_len = 0;
        R_len_t length_len = 0;
        R_len_t value_len = 0;
        R_len_t vectorize_len = 0;
        bool has_to = false;
        bool has_length = false;
        ci::unwind_protect([&]() -> SEXP {
            omit_na_1 = ci__prepare_arg_logical_1_notNA(
                omit_na, "omit_na", &STRI__DEFERRED_WARNINGS
            );
            use_matrix_1 = ci__prepare_arg_logical_1_notNA(
                use_matrix, "use_matrix", &STRI__DEFERRED_WARNINGS
            );
            str_len = static_cast<R_len_t>(strings.size());
            from_len = LENGTH(from);
            to_len = LENGTH(to);
            length_len = LENGTH(length);
            value_len = LENGTH(value);
            has_to = !Rf_isNull(to);
            has_length = !Rf_isNull(length);
            if (has_to) {
                vectorize_len = ci__recycling_rule(
                    STRI__DEFERRED_WARNINGS, 4,
                    str_len, from_len, value_len, to_len
                );
            }
            else if (has_length) {
                vectorize_len = ci__recycling_rule(
                    STRI__DEFERRED_WARNINGS, 4,
                    str_len, from_len, value_len, length_len
                );
            }
            else {
                vectorize_len = ci__recycling_rule(
                    STRI__DEFERRED_WARNINGS, 3,
                    str_len, from_len, value_len
                );
            }
            return R_NilValue;
        });

        int scalar_from = 0;
        int scalar_to = 0;
        SEXP scalar_value = R_NilValue;
        const bool scalar_bounds = vectorize_len > 0 && has_to &&
            !has_length &&
            ci__sub_all_plain_list_scalar(
                from, from_len, scalar_from
            ) &&
            ci__sub_all_plain_list_scalar(to, to_len, scalar_to) &&
            scalar_from > 0 && scalar_to > 0;
        const bool scalar_replacement = value_len == 1 &&
            TYPEOF(value) == VECSXP && !Rf_isObject(value) &&
            !ALTREP(value) && NO_ATTRIB(value) &&
            ((scalar_value = VECTOR_ELT(value, 0)),
             TYPEOF(scalar_value) == STRSXP &&
             !Rf_isObject(scalar_value) && !ALTREP(scalar_value) &&
             NO_ATTRIB(scalar_value) && XLENGTH(scalar_value) == 1);

        if (scalar_bounds && scalar_replacement) {
            const std::unique_ptr<CiOwnedUtf8Set> replacements_owner =
                ci__sub_read_owned(context, scalar_value);
            const std::vector<CiOwnedUtf8>& replacements =
                replacements_owner->records;
            const CiOwnedUtf8& replacement = replacements[0];
            charport::charvec::Builder builder(vectorize_len);

            for (R_len_t i = 0; i < vectorize_len; ++i) {
                const CiOwnedUtf8& source =
                    strings[static_cast<size_t>(i % str_len)];
                if (source.is_na || (!omit_na_1 && replacement.is_na)) {
                    builder.set_na(i);
                    continue;
                }
                if (replacement.is_na) {
                    ci__sub_builder_set_owned(builder, i, source);
                    continue;
                }

                const R_len_t source_length = source.length;
                const R_len_t from_byte = ci__sub_owned_positive_boundary(
                    source, scalar_from-1
                );
                R_len_t to_byte = ci__sub_owned_positive_boundary(
                    source, scalar_to
                );
                if (to_byte < from_byte)
                    to_byte = from_byte;

                const size_t prefix = static_cast<size_t>(from_byte);
                const size_t replacement_length = static_cast<size_t>(
                    replacement.length
                );
                const size_t suffix = static_cast<size_t>(
                    source_length-to_byte
                );
                size_t output_length = ci__sub_checked_output_size(
                    prefix, replacement_length
                );
                output_length = ci__sub_checked_output_size(
                    output_length, suffix
                );
                const char* source_data = ci__sub_owned_data(source);
                const char* replacement_data =
                    ci__sub_owned_data(replacement);
                const bool output_ascii =
                    (source.is_ascii ||
                     ci::is_ascii(source_data, prefix)) &&
                    (replacement.is_ascii ||
                     ci::is_ascii(
                         replacement_data, replacement_length
                     )) &&
                    (source.is_ascii ||
                     ci::is_ascii(source_data+to_byte, suffix));
                char* output = builder.reserve(
                    i, output_length,
                    output_ascii
                        ? cetype_ext_t::CE_ASCII
                        : cetype_ext_t::CE_UTF8
                );
                if (prefix > 0)
                    memcpy(output, source_data, prefix);
                if (replacement_length > 0) {
                    memcpy(
                        output+prefix, replacement_data,
                        replacement_length
                    );
                }
                if (suffix > 0) {
                    memcpy(
                        output+prefix+replacement_length,
                        source_data+to_byte, suffix
                    );
                }
            }

            STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
                return builder.to_sexp();
            }));
            context.emitWarnings();
            STRI__UNPROTECT_ALL
            return ret;
        }

        charport::charvec::Builder builder(vectorize_len);
        for (R_len_t i=0; i<vectorize_len; ++i) {
            const CiOwnedUtf8& source =
                strings[static_cast<size_t>(i % str_len)];
            if (source.is_na) {
                builder.set_na(i);
                continue;
            }

            SEXP inner_value = R_NilValue;
            ci::unwind_protect([&]() -> SEXP {
                inner_value = ci__prepare_arg_string(
                    VECTOR_ELT(value, i % value_len), "str", true,
                    &STRI__DEFERRED_WARNINGS
                );
                PROTECT(inner_value);
                return R_NilValue;
            });
            ++__ci_protected_sexp_num;
            const std::unique_ptr<CiOwnedUtf8Set> replacements_owner =
                ci__sub_read_owned(context, inner_value);
            const std::vector<CiOwnedUtf8>& replacements =
                replacements_owner->records;
            STRI__UNPROTECT(1);

            SEXP inner_from = R_NilValue;
            SEXP inner_to = R_NilValue;
            SEXP inner_length = R_NilValue;
            R_len_t inner_from_len = 0;
            R_len_t inner_to_len = 0;
            R_len_t inner_length_len = 0;
            int* inner_from_tab = NULL;
            int* inner_to_tab = NULL;
            int* inner_length_tab = NULL;
            R_len_t inner_protected = 0;
            R_len_t inner_vectorize_len = 0;

            ci::unwind_protect([&]() -> SEXP {
                inner_from = VECTOR_ELT(from, i % from_len);
                if (has_to)
                    inner_to = VECTOR_ELT(to, i % to_len);
                else if (has_length)
                    inner_length = VECTOR_ELT(length, i % length_len);
                inner_protected = ci__sub_prepare_from_to_length(
                    inner_from, inner_to, inner_length,
                    inner_from_len, inner_to_len, inner_length_len,
                    inner_from_tab, inner_to_tab, inner_length_tab,
                    use_matrix_1, &STRI__DEFERRED_WARNINGS
                );
                return R_NilValue;
            });
            __ci_protected_sexp_num += inner_protected;
            inner_vectorize_len = ci__recycling_rule(
                STRI__DEFERRED_WARNINGS, 2, inner_from_len,
                (inner_to_len>inner_length_len)
                    ? inner_to_len : inner_length_len
            );

            ci__sub_replacement_all_single(
                builder, i, source, replacements,
                inner_from_len, inner_to_len, inner_length_len,
                inner_from_tab, inner_to_tab, inner_length_tab,
                inner_vectorize_len, omit_na_1, context
            );
            STRI__UNPROTECT(inner_protected);
        }

        STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
            return builder.to_sexp();
        }));
    }

    context.emitWarnings();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}

} } // namespace charr::altrep_backend
