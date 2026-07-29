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

#include "utf16_input.h"

#include "../../shared/native_to_utf8.h"
#include "../ci_exception.h"
#include "../ci_string8buf.h"
#include "../ci_stringi.h"
#include "../ci_ucnv.h"

#include <utility>

namespace charr {
namespace base_backend {
namespace io {

namespace utf16_input {

struct LoadedUtf16 {
    VectorizedSize shape;
    std::vector<icu::UnicodeString> values;
};

LoadedUtf16 load_utf16(
    SEXP source, R_len_t recycle_size, bool materialize_recycle
)
{
#ifndef NDEBUG
    if (!Rf_isString(source))
        throw StriException("UTF-16 input requires a character vector");
#endif

    const R_len_t source_size = LENGTH(source);
    LoadedUtf16 loaded{
        VectorizedSize(source_size, recycle_size, materialize_recycle),
        std::vector<icu::UnicodeString>()
    };
    loaded.values.resize(
        static_cast<std::size_t>(loaded.shape.data_size())
    );
    for (icu::UnicodeString& value : loaded.values)
        value.setToBogus();

    // A zero-length recycling result consumes no source elements, even when
    // the source itself is nonempty.
    if (loaded.shape.data_size() == 0)
        return loaded;

#if defined(_WIN32) || defined(_WIN64)
    StriUcnv latin1("WINDOWS-1252");
#else
    StriUcnv latin1("ISO-8859-1");
#endif
    shared::NativeToUtf8 native_to_utf8;

    for (R_len_t i = 0; i < source_size; ++i) {
        SEXP value = STRING_ELT(source, i);
        if (value == NA_STRING)
            continue;

        if (IS_ASCII(value) || IS_UTF8(value)) {
            loaded.values[static_cast<std::size_t>(i)].setTo(
                icu::UnicodeString::fromUTF8(
                    icu::StringPiece(CHAR(value), LENGTH(value))
                )
            );
        }
        else if (IS_LATIN1(value)) {
            UErrorCode status = U_ZERO_ERROR;
            loaded.values[static_cast<std::size_t>(i)].setTo(
                icu::UnicodeString(
                    CHAR(value), LENGTH(value), latin1.getConverter(), status
                )
            );
            STRI__CHECKICUSTATUS_THROW(status, {})
        }
        else if (IS_BYTES(value)) {
            throw StriException(MSG__BYTESENC);
        }
        else {
            try {
                const shared::ByteView converted = native_to_utf8.native(
                    CHAR(value), LENGTH(value)
                );
                loaded.values[static_cast<std::size_t>(i)].setTo(
                    icu::UnicodeString::fromUTF8(
                        icu::StringPiece(converted.ptr, converted.len)
                    )
                );
            }
            catch (const std::exception& error) {
                throw StriException("%s", error.what());
            }
        }
    }

    if (materialize_recycle && source_size > 0) {
        for (R_len_t i = source_size; i < loaded.shape.data_size(); ++i) {
            loaded.values[static_cast<std::size_t>(i)].setTo(
                loaded.values[static_cast<std::size_t>(i % source_size)]
            );
        }
    }
    return loaded;
}

void convert_indices(
    const icu::UnicodeString& value,
    int* first, int* second, int size,
    int first_adjustment, int second_adjustment
)
{
    const UChar* data = value.getBuffer();
    const int length = value.length();
    int first_index = 0;
    int second_index = 0;
    int utf16_index = 0;
    int codepoint_index = 0;

    while (utf16_index < length &&
            (first_index < size || second_index < size)) {
        while (first_index < size && first[first_index] <= utf16_index) {
            if (first[first_index] == NA_INTEGER || first[first_index] < 0) {
                ++first_index;
                continue;
            }
            first[first_index++] = codepoint_index + first_adjustment;
        }
        while (second_index < size && second[second_index] <= utf16_index) {
            if (second[second_index] == NA_INTEGER || second[second_index] < 0) {
                ++second_index;
                continue;
            }
            second[second_index++] = codepoint_index + second_adjustment;
        }
        U16_FWD_1(data, utf16_index, length);
        ++codepoint_index;
    }

    while (first_index < size && first[first_index] <= length) {
        if (first[first_index] != NA_INTEGER && first[first_index] >= 0)
            first[first_index] = codepoint_index + first_adjustment;
        ++first_index;
    }
    while (second_index < size && second[second_index] <= length) {
        if (second[second_index] != NA_INTEGER && second[second_index] >= 0)
            second[second_index] = codepoint_index + second_adjustment;
        ++second_index;
    }

#ifndef NDEBUG
    if (utf16_index >= length &&
            (first_index < size || second_index < size)) {
        throw StriException("UTF-16 index lies outside its record");
    }
#endif
}

} // namespace utf16_input

using namespace utf16_input;

Utf16Input::Utf16Input() noexcept : shape_(), values_() {}

Utf16Input::Utf16Input(SEXP source, R_len_t recycle_size)
    : shape_(), values_()
{
    LoadedUtf16 loaded = load_utf16(source, recycle_size, false);
    shape_ = loaded.shape;
    values_ = std::move(loaded.values);
}

bool Utf16Input::isNA(R_len_t index) const
{
    return values_[static_cast<std::size_t>(shape_.index(index))].isBogus();
}

const icu::UnicodeString& Utf16Input::get(R_len_t index) const
{
    const icu::UnicodeString& value = values_[
        static_cast<std::size_t>(shape_.index(index))
    ];
#ifndef NDEBUG
    if (value.isBogus())
        throw StriException("cannot get a missing UTF-16 string");
#endif
    return value;
}

void Utf16Input::UChar16_to_UChar32_index(
    R_len_t index, int* first, int* second, int size,
    int first_adjustment, int second_adjustment
) const
{
    convert_indices(
        get(index), first, second, size,
        first_adjustment, second_adjustment
    );
}

Utf16Output::Utf16Output(R_len_t size)
    : shape_(size, size), values_(static_cast<std::size_t>(size))
{
}

Utf16Output::Utf16Output(SEXP source, R_len_t recycle_size)
    : shape_(), values_()
{
    LoadedUtf16 loaded = load_utf16(source, recycle_size, true);
    shape_ = loaded.shape;
    values_ = std::move(loaded.values);
}

bool Utf16Output::isNA(R_len_t index) const
{
    return values_[static_cast<std::size_t>(shape_.index(index))].isBogus();
}

const icu::UnicodeString& Utf16Output::get(R_len_t index) const
{
    const icu::UnicodeString& value = values_[
        static_cast<std::size_t>(shape_.index(index))
    ];
#ifndef NDEBUG
    if (value.isBogus())
        throw StriException("cannot get a missing UTF-16 string");
#endif
    return value;
}

icu::UnicodeString& Utf16Output::getWritable(R_len_t index)
{
    return values_[static_cast<std::size_t>(shape_.index(index))];
}

void Utf16Output::setNA(R_len_t index)
{
    getWritable(index).setToBogus();
}

void Utf16Output::set(R_len_t index, const icu::UnicodeString& value)
{
    getWritable(index).setTo(value);
}

SEXP Utf16Output::toR() const
{
    R_len_t buffer_size = 0;
    for (const icu::UnicodeString& value : values_) {
        if (!value.isBogus() && value.length() > buffer_size)
            buffer_size = value.length();
    }
    buffer_size = UCNV_GET_MAX_BYTES_FOR_STRING(buffer_size, 3);
    String8buf buffer(buffer_size);

    SEXP output = PROTECT(Rf_allocVector(STRSXP, shape_.recycle_size()));
    UErrorCode status = U_ZERO_ERROR;
    for (R_len_t i = 0; i < shape_.recycle_size(); ++i) {
        const icu::UnicodeString& value = values_[
            static_cast<std::size_t>(shape_.index(i))
        ];
        if (value.isBogus()) {
            SET_STRING_ELT(output, i, NA_STRING);
            continue;
        }

        int output_size = 0;
        u_strToUTF8(
            buffer.data(), buffer_size, &output_size,
            value.getBuffer(), value.length(), &status
        );
        STRI__CHECKICUSTATUS_THROW(status, {UNPROTECT(1);})
        SET_STRING_ELT(
            output, i,
            Rf_mkCharLenCE(buffer.data(), output_size, CE_UTF8)
        );
    }
    UNPROTECT(1);
    return output;
}

SEXP Utf16Output::toR(R_len_t index) const
{
    const icu::UnicodeString& value = values_[
        static_cast<std::size_t>(shape_.index(index))
    ];
    if (value.isBogus())
        return NA_STRING;

    std::string utf8;
    value.toUTF8String(utf8);
    return Rf_mkCharLenCE(
        utf8.data(), static_cast<int>(utf8.size()), CE_UTF8
    );
}

} // namespace io
} // namespace base_backend
} // namespace charr
