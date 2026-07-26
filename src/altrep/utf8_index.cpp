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

#include "utf8_input.h"

#include "ci_stringi.h"

#include <algorithm>
#include <unicode/utf8.h>

namespace charr {
namespace altrep {

IndexedUtf8Input::IndexedUtf8Input() noexcept : Utf8Input()
{
    reset_index_cache();
}

IndexedUtf8Input::IndexedUtf8Input(
    ci::ReaderContext& context, SEXP source, R_xlen_t recycle_size,
    bool shallow_recycle
) : Utf8Input(context, source, recycle_size, shallow_recycle)
{
    reset_index_cache();
}

IndexedUtf8Input::IndexedUtf8Input(
    const std::shared_ptr<ci::ReaderBorrow>& borrow,
    const charport::StrView& value, R_xlen_t recycle_size,
    bool shallow_recycle
) : Utf8Input(borrow, value, recycle_size, shallow_recycle)
{
    reset_index_cache();
}

IndexedUtf8Input::IndexedUtf8Input(
    const IndexedUtf8Input& other
) noexcept : Utf8Input(other)
{
    reset_index_cache();
}

IndexedUtf8Input& IndexedUtf8Input::operator=(
    const IndexedUtf8Input& other
) noexcept
{
    if (this != &other) {
        Utf8Input::operator=(other);
        reset_index_cache();
    }
    return *this;
}

void IndexedUtf8Input::reset_index_cache() noexcept
{
    last_fwd_codepoint_ = 0;
    last_fwd_utf8_ = 0;
    last_fwd_data_ = nullptr;
    last_back_codepoint_ = 0;
    last_back_utf8_ = 0;
    last_back_data_ = nullptr;
}

R_len_t IndexedUtf8Input::UChar32_to_UTF8_index_back(
    R_len_t i, R_len_t wh
)
{
    const Utf8Record& value = get(i);
    const R_len_t length = value.length();
    if (wh <= 0)
        return length;
    if (value.isASCII())
        return std::max(length - wh, 0);
    const char* data = value.data();

    if (last_back_data_ != data) {
        last_back_codepoint_ = 0;
        last_back_utf8_ = length;
        last_back_data_ = data;
    }

    R_len_t codepoint = 0;
    R_len_t offset = length;
    if (last_back_codepoint_ > 0) {
        if (wh < last_back_codepoint_ &&
                last_back_codepoint_ - wh < wh) {
            codepoint = last_back_codepoint_;
            offset = last_back_utf8_;
            while (codepoint > wh && offset < length) {
                U8_FWD_1(reinterpret_cast<const uint8_t*>(data), offset, length);
                --codepoint;
            }
            last_back_codepoint_ = wh;
            last_back_utf8_ = offset;
            return offset;
        }
        if (wh >= last_back_codepoint_) {
            codepoint = last_back_codepoint_;
            offset = last_back_utf8_;
        }
    }

    while (codepoint < wh && offset > 0) {
        U8_BACK_1(reinterpret_cast<const uint8_t*>(data), 0, offset);
        ++codepoint;
    }
    last_back_codepoint_ = codepoint;
    last_back_utf8_ = offset;
    return offset;
}

R_len_t IndexedUtf8Input::UChar32_to_UTF8_index_fwd(
    R_len_t i, R_len_t wh
)
{
    if (wh <= 0)
        return 0;
    const Utf8Record& value = get(i);
    if (value.isASCII())
        return std::min(wh, value.length());
    const R_len_t length = value.length();
    const char* data = value.data();

    if (last_fwd_data_ != data) {
        last_fwd_codepoint_ = 0;
        last_fwd_utf8_ = 0;
        last_fwd_data_ = data;
    }

    R_len_t codepoint = 0;
    R_len_t offset = 0;
    if (last_fwd_codepoint_ > 0) {
        if (wh < last_fwd_codepoint_ &&
                last_fwd_codepoint_ - wh < wh) {
            codepoint = last_fwd_codepoint_;
            offset = last_fwd_utf8_;
            while (codepoint > wh && offset > 0) {
                U8_BACK_1(reinterpret_cast<const uint8_t*>(data), 0, offset);
                --codepoint;
            }
            last_fwd_codepoint_ = wh;
            last_fwd_utf8_ = offset;
            return offset;
        }
        if (wh >= last_fwd_codepoint_) {
            codepoint = last_fwd_codepoint_;
            offset = last_fwd_utf8_;
        }
    }

    while (codepoint < wh && offset < length) {
        U8_FWD_1(reinterpret_cast<const uint8_t*>(data), offset, length);
        ++codepoint;
    }
    last_fwd_codepoint_ = codepoint;
    last_fwd_utf8_ = offset;
    return offset;
}

void IndexedUtf8Input::UTF8_to_UChar32_index(
    R_len_t i, int* i1, int* i2, int ni, int adj1, int adj2
)
{
    const Utf8Record& value = get(i);
    if (value.isASCII()) {
        for (int j = 0; j < ni; ++j) {
            i1[j] += adj1;
            i2[j] += adj2;
        }
        return;
    }

    const char* data = value.data();
    const int length = value.length();
    int j1 = 0;
    int j2 = 0;
    int offset = 0;
    int codepoint = 0;
    while (offset < length && (j1 < ni || j2 < ni)) {
        if (j1 < ni && i1[j1] <= offset)
            i1[j1++] = codepoint + adj1;
        if (j2 < ni && i2[j2] <= offset)
            i2[j2++] = codepoint + adj2;
        U8_FWD_1(data, offset, length);
        ++codepoint;
    }
    if (j1 < ni && i1[j1] <= length)
        i1[j1++] = codepoint + adj1;
    if (j2 < ni && i2[j2] <= length)
        i2[j2++] = codepoint + adj2;
#ifndef NDEBUG
    if (offset >= length && (j1 < ni || j2 < ni))
        throw StriException("UTF-8 index lies outside its record");
#endif
}

} // namespace altrep
} // namespace charr
