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
#include "charclass/pattern_set.h"
#include "../shared/native_to_utf8.h"
#include "altrep_backend/io/utf8_output.h"

#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace charr { namespace altrep_backend {


namespace search_class_trim {

struct TrimSlice {
    const char* data;
    R_len_t length;
    bool removed_non_ascii;
};


struct ScalarTrimPattern {
    const UnicodeSet* retained;
    std::array<unsigned char, 128> ascii_retained;
    bool missing;
};


bool retained_contains(
    const UnicodeSet& retained, const unsigned char* ascii_retained,
    UChar32 code_point
)
{
    if (ascii_retained != nullptr && code_point <= 0x7f)
        return ascii_retained[code_point] != 0;
    return retained.contains(code_point);
}


// Compile-time direction flags keep the hot edge scan branch-free.
template<bool Left, bool Right>
TrimSlice trim_slice(
    const char* data, R_len_t length,
    const UnicodeSet& retained, const unsigned char* ascii_retained,
    bool strip_bom
)
{
    bool removed_non_ascii = false;
    if (strip_bom && STRI__ENC_HAS_BOM_UTF8(data, length)) {
        data += 3;
        length -= 3;
        removed_non_ascii = true;
    }

    R_len_t begin = 0;
    R_len_t end = length;

    if (Left) {
        UChar32 code_point;
        for (R_len_t cursor = 0; cursor < length; ) {
            U8_NEXT(data, cursor, length, code_point);
            if (code_point < 0)
                throw StriException(MSG__INVALID_UTF8);
            if (retained_contains(
                    retained, ascii_retained, code_point
                ))
                break;
            removed_non_ascii = removed_non_ascii || code_point > 0x7f;
            begin = cursor;
        }
    }

    if (Right && begin < length) {
        UChar32 code_point;
        for (R_len_t cursor = length; cursor > 0; ) {
            U8_PREV(data, 0, cursor, code_point);
            if (code_point < 0)
                throw StriException(MSG__INVALID_UTF8);
            if (retained_contains(
                    retained, ascii_retained, code_point
                ))
                break;
            removed_non_ascii = removed_non_ascii || code_point > 0x7f;
            end = cursor;
        }
    }

    return TrimSlice{
        data + begin, end - begin, removed_non_ascii
    };
}


charport::StrView normalize_source(
    const charport::StrView& value,
    charr::shared::NativeToUtf8& converter
)
{
    if (value.is_na())
        return value;
    if (value.ptr == nullptr || value.len < 0)
        throw std::runtime_error("Reader returned an invalid string view");

    shared::ByteView converted;
    switch (value.enc) {
    case cetype_ext_t::CE_ASCII:
    case cetype_ext_t::CE_UTF8:
    case cetype_ext_t::CE_ASCII_OR_UTF8:
        return value;
    case cetype_ext_t::CE_BYTES:
        throw StriException(MSG__BYTESENC);
    case cetype_ext_t::CE_LATIN1:
        converted = converter.latin1(value.ptr, value.len);
        break;
    case cetype_ext_t::CE_NATIVE:
        converted = converter.native(value.ptr, value.len);
        break;
    case cetype_ext_t::CE_NA:
        throw std::logic_error("non-missing Reader record has NA encoding");
    default:
        throw std::runtime_error("Reader returned an unknown string encoding");
    }

    return make_strview(
        converted.ptr, converted.len, cetype_ext_t::CE_UTF8
    );
}


bool strip_input_bom(
    const charport::StrView& original,
    const charport::StrView& normalized
)
{
    if (!STRI__ENC_HAS_BOM_UTF8(normalized.ptr, normalized.len))
        return false;
    if (original.enc == cetype_ext_t::CE_UTF8 ||
            original.enc == cetype_ext_t::CE_ASCII_OR_UTF8)
        return true;
    return original.enc == cetype_ext_t::CE_NATIVE &&
        STRI__ENC_HAS_BOM_UTF8(original.ptr, original.len);
}


cetype_ext_t trimmed_encoding(
    const charport::StrView& original,
    const charport::StrView& normalized,
    const TrimSlice& trimmed
)
{
    if (trimmed.length == 0 ||
            original.enc == cetype_ext_t::CE_ASCII)
        return cetype_ext_t::CE_ASCII;

    // A definite UTF-8 mark promises a non-ASCII record. Removing only ASCII
    // edge code points cannot invalidate that promise. Ambiguous marks and
    // converted records need one scan of the retained slice.
    if (original.enc == cetype_ext_t::CE_UTF8 &&
            normalized.enc == cetype_ext_t::CE_UTF8 &&
            !trimmed.removed_non_ascii)
        return cetype_ext_t::CE_UTF8;

    return ci::is_ascii(
        trimmed.data, static_cast<std::size_t>(trimmed.length)
    ) ? cetype_ext_t::CE_ASCII : cetype_ext_t::CE_UTF8;
}


ScalarTrimPattern make_scalar_pattern(
    const charclass::PatternSet& patterns
)
{
    ScalarTrimPattern scalar{nullptr, {}, patterns.isNA(0)};
    if (scalar.missing)
        return scalar;

    scalar.retained = &patterns.get(0);
    for (std::size_t i = 0; i < scalar.ascii_retained.size(); ++i) {
        scalar.ascii_retained[i] = static_cast<unsigned char>(
            scalar.retained->contains(static_cast<UChar32>(i))
        );
    }
    return scalar;
}


bool source_is_direct_utf8(const charport::StrViews& source)
{
    for (R_xlen_t i = 0; i < source.size(); ++i) {
        const charport::StrView value = source[i];
        if (value.is_na())
            continue;
        if (value.ptr == nullptr || value.len < 0)
            throw std::runtime_error("Reader returned an invalid string view");

        switch (value.enc) {
        case cetype_ext_t::CE_ASCII:
        case cetype_ext_t::CE_UTF8:
        case cetype_ext_t::CE_ASCII_OR_UTF8:
            break;
        case cetype_ext_t::CE_BYTES:
            throw StriException(MSG__BYTESENC);
        case cetype_ext_t::CE_LATIN1:
        case cetype_ext_t::CE_NATIVE:
            return false;
        case cetype_ext_t::CE_NA:
            throw std::logic_error(
                "non-missing Reader record has NA encoding"
            );
        default:
            throw std::runtime_error(
                "Reader returned an unknown string encoding"
            );
        }
    }
    return true;
}


void add_payload_length(std::size_t& total, R_len_t length)
{
    const std::size_t amount = static_cast<std::size_t>(length);
    if (amount > std::numeric_limits<std::size_t>::max() - total)
        throw std::length_error("character output payload is too large");
    total += amount;
}


template<bool ScalarPattern>
const UnicodeSet* select_pattern(
    R_len_t index, const charclass::PatternSet& patterns,
    const ScalarTrimPattern& scalar, bool& missing,
    const unsigned char*& ascii_retained
)
{
    if constexpr (ScalarPattern) {
        missing = scalar.missing;
        ascii_retained = scalar.ascii_retained.data();
        return scalar.retained;
    }
    else {
        missing = patterns.isNA(index);
        ascii_retained = nullptr;
        return missing ? nullptr : &patterns.get(index);
    }
}


template<bool Left, bool Right, bool ScalarPattern, bool RecycledSource>
charr::altrep_backend::io::OutputStore trim_direct_records(
    const charport::StrViews& source, R_len_t output_length,
    const charclass::PatternSet& patterns,
    const ScalarTrimPattern& scalar
)
{
    charr::altrep_backend::io::OutputStore output(
        static_cast<std::size_t>(output_length), 0
    );
    const R_len_t source_length = static_cast<R_len_t>(source.size());
    std::size_t payload_length = 0;

    for (R_len_t i = 0; i < output_length; ++i) {
        const R_len_t source_index = RecycledSource
            ? i % source_length
            : i;
        const charport::StrView original =
            source[static_cast<std::size_t>(source_index)];
        bool pattern_missing;
        const unsigned char* ascii_retained;
        const UnicodeSet* retained = select_pattern<ScalarPattern>(
            i, patterns, scalar, pattern_missing, ascii_retained
        );
        if (original.is_na() || pattern_missing) {
            output.records.set_na(static_cast<std::size_t>(i));
            continue;
        }

        const TrimSlice trimmed = trim_slice<Left, Right>(
            original.ptr, original.len, *retained, ascii_retained,
            strip_input_bom(original, original)
        );
        const cetype_ext_t encoding = trimmed_encoding(
            original, original, trimmed
        );
        const char* record_data = trimmed.length == 0
            ? charport::charvec::components::empty_data()
            : trimmed.data;
        output.records.set(
            static_cast<std::size_t>(i), record_data,
            trimmed.length, encoding
        );
        add_payload_length(payload_length, trimmed.length);
    }

    if (payload_length == 0)
        return output;

    char* destination = output.slices.push_front(payload_length);
    for (R_len_t i = 0; i < output_length; ++i) {
        const std::size_t index = static_cast<std::size_t>(i);
        const charport::StrView record = output.records.view(index);
        if (record.is_na() || record.len == 0)
            continue;
        std::memcpy(
            destination, record.ptr, static_cast<std::size_t>(record.len)
        );
        output.records.set(index, destination, record.len, record.enc);
        destination += record.len;
    }
    return output;
}


template<bool Left, bool Right, bool ScalarPattern, bool RecycledSource>
charr::altrep_backend::io::OutputStore trim_converted_records(
    const charport::StrViews& source, R_len_t output_length,
    const charclass::PatternSet& patterns,
    const ScalarTrimPattern& scalar
)
{
    charport::charvec::Builder output(output_length);
    charr::shared::NativeToUtf8 converter;
    const R_len_t source_length = static_cast<R_len_t>(source.size());

    for (R_len_t i = 0; i < output_length; ++i) {
        const R_len_t source_index = RecycledSource
            ? i % source_length
            : i;
        const charport::StrView original =
            source[static_cast<std::size_t>(source_index)];
        const charport::StrView value = normalize_source(original, converter);
        bool pattern_missing;
        const unsigned char* ascii_retained;
        const UnicodeSet* retained = select_pattern<ScalarPattern>(
            i, patterns, scalar, pattern_missing, ascii_retained
        );
        if (value.is_na() || pattern_missing) {
            output.set_na(i);
            continue;
        }

        const TrimSlice trimmed = trim_slice<Left, Right>(
            value.ptr, value.len, *retained, ascii_retained,
            strip_input_bom(original, value)
        );
        output.set(
            i, trimmed.data, static_cast<std::size_t>(trimmed.length),
            trimmed_encoding(original, value, trimmed)
        );
    }
    return output.release_store();
}


template<bool Left, bool Right, bool ScalarPattern>
charr::altrep_backend::io::OutputStore trim_dispatch_source(
    const charport::StrViews& source, R_len_t output_length,
    const charclass::PatternSet& patterns,
    const ScalarTrimPattern& scalar, bool direct_source
)
{
    const bool recycled_source =
        static_cast<R_len_t>(source.size()) != output_length;
    if (direct_source) {
        return recycled_source
            ? trim_direct_records<Left, Right, ScalarPattern, true>(
                source, output_length, patterns, scalar
            )
            : trim_direct_records<Left, Right, ScalarPattern, false>(
                source, output_length, patterns, scalar
            );
    }
    return recycled_source
        ? trim_converted_records<Left, Right, ScalarPattern, true>(
            source, output_length, patterns, scalar
        )
        : trim_converted_records<Left, Right, ScalarPattern, false>(
            source, output_length, patterns, scalar
        );
}

} // namespace search_class_trim

using namespace search_class_trim;


/**
 * Trim characters from a charclass from left AND/OR right side of the string
 *
 * @param str character vector
 * @param pattern character vector
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-04)
 *          Use io::Utf8Input and CharClass
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-03)
 *          detects invalid UTF-8 byte stream
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-05)
 *          charclass::PatternSet now relies on UnicodeSet
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-06-10) negate
*/
template<bool Left, bool Right>
SEXP ci__trim_leftright(SEXP str, SEXP pattern, bool negate)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern"));
    STRI__ERROR_HANDLER_BEGIN(2)
    SEXP ret;
    charr::altrep_backend::io::OutputStore output_store(0, 0);
    {
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    charport::Reader source_reader(ci::protected_reader_resolve(str));
    R_len_t str_n = ci::checked_r_len(
        source_reader.size(), "character vectors"
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
    charport::StrViews source_views(str_n);
    if (str_n > 0)
        source_reader.views(0, str_n, source_views);

    {
        charclass::PatternSet pattern_cont(
            context, pattern, vectorize_length, negate
        );
        if (vectorize_length == 0) {
            output_store = charr::altrep_backend::io::OutputStore(0, 0);
        }
        else {
            const bool scalar_pattern = pattern_n == 1;
            const ScalarTrimPattern scalar = scalar_pattern
                ? make_scalar_pattern(pattern_cont)
                : ScalarTrimPattern{nullptr, {}, false};
            const bool direct_source = source_is_direct_utf8(source_views);
            output_store = scalar_pattern
                ? trim_dispatch_source<Left, Right, true>(
                    source_views, vectorize_length, pattern_cont, scalar,
                    direct_source
                )
                : trim_dispatch_source<Left, Right, false>(
                    source_views, vectorize_length, pattern_cont, scalar,
                    direct_source
                );
        }
    }
    }

    ret = ci::unwind_protect([&]() -> SEXP {
        return charr::altrep_backend::io::finalize(std::move(output_store));
    });
    STRI__PROTECT(ret);
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/**
 * Trim characters from a charclass from both sides of the string
 *
 * @param str character vector
 * @param pattern character vector
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-04)
 *          Use ci__trim_leftright
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException-friendly
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-06-10) negate
*/
SEXP ci_trim_both(SEXP str, SEXP pattern, SEXP negate)
{
    bool negate_val = ci__prepare_arg_logical_1_notNA(negate, "negate");
    return ci__trim_leftright<true, true>(str, pattern, negate_val);
}


/**
 * Trim characters from a charclass from the left of the string
 *
 * @param str character vector
 * @param pattern character vector
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-04)
 *          Use ci__trim_leftright
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-06-10) negate
*/
SEXP ci_trim_left(SEXP str, SEXP pattern, SEXP negate)
{
    bool negate_val = ci__prepare_arg_logical_1_notNA(negate, "negate");
    return ci__trim_leftright<true, false>(str, pattern, negate_val);
}


/**
 * Trim characters from a charclass from the right of the string
 *
 * @param str character vector
 * @param pattern character vector
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-04)
 *          Use ci__trim_leftright
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-06-10) negate
*/
SEXP ci_trim_right(SEXP str, SEXP pattern, SEXP negate)
{
    bool negate_val = ci__prepare_arg_logical_1_notNA(negate, "negate");
    return ci__trim_leftright<false, true>(str, pattern, negate_val);
}

} } // namespace charr::altrep_backend
