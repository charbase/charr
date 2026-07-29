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

#ifndef CHARR_BASE_UTF16_INPUT_H
#define CHARR_BASE_UTF16_INPUT_H

#include "vectorized_size.h"
#include "../ci_external.h"

#include <vector>

namespace charr {
namespace base_backend {
namespace io {

class Utf16Input {
public:
    Utf16Input() noexcept;
    Utf16Input(SEXP source, R_len_t recycle_size);

    R_len_t get_n() const noexcept { return shape_.data_size(); }
    R_len_t get_nrecycle() const noexcept {
        return shape_.recycle_size();
    }
    R_len_t vectorize_init() const noexcept {
        return shape_.vectorize_init();
    }
    R_len_t vectorize_end() const noexcept {
        return shape_.vectorize_end();
    }
    R_len_t vectorize_next(R_len_t index) const noexcept {
        return shape_.vectorize_next(index);
    }

    bool isNA(R_len_t index) const;
    const icu::UnicodeString& get(R_len_t index) const;
    void UChar16_to_UChar32_index(
        R_len_t index, int* first, int* second, int size,
        int first_adjustment, int second_adjustment
    ) const;

private:
    VectorizedSize shape_;
    std::vector<icu::UnicodeString> values_;
};

class Utf16Output {
public:
    explicit Utf16Output(R_len_t size);
    Utf16Output(SEXP source, R_len_t recycle_size);

    R_len_t get_n() const noexcept { return shape_.data_size(); }
    R_len_t get_nrecycle() const noexcept {
        return shape_.recycle_size();
    }
    bool isNA(R_len_t index) const;
    const icu::UnicodeString& get(R_len_t index) const;
    icu::UnicodeString& getWritable(R_len_t index);
    void setNA(R_len_t index);
    void set(R_len_t index, const icu::UnicodeString& value);
    SEXP toR(R_len_t index) const;
    SEXP toR() const;

private:
    VectorizedSize shape_;
    std::vector<icu::UnicodeString> values_;
};

} // namespace io

SEXP ci__subset_by_logical(
    const io::Utf16Input& input,
    const std::vector<int>& which, int result_counter
);

} // namespace base_backend
} // namespace charr

#endif
