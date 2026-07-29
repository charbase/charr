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


#ifndef CHARR_BASE_INTEGER_INPUT_H
#define CHARR_BASE_INTEGER_INPUT_H

#include "vectorized_size.h"
#include "../ci_exception.h"

namespace charr {
namespace base_backend {
namespace io {

/**
 * A wrapper-class for R integer vectors
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-15)
 */
class IntegerInput {
public:
    IntegerInput() noexcept : shape_(), data_(nullptr) {}

    IntegerInput(SEXP source, R_len_t recycle_size)
        : shape_(), data_(nullptr)
    {
#ifndef NDEBUG
        if (!Rf_isInteger(source))
            throw StriException("integer input requires an integer vector");
#endif
        shape_.reset(LENGTH(source), recycle_size);
        data_ = INTEGER_RO(source);
    }

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

    bool isNA(R_len_t index) const {
        return data_[shape_.index(index)] == NA_INTEGER;
    }

    int get(R_len_t index) const {
        const int value = data_[shape_.index(index)];
#ifndef NDEBUG
        if (value == NA_INTEGER)
            throw StriException("cannot get a missing integer");
#endif
        return value;
    }

    int getNAble(R_len_t index) const {
        return data_[shape_.index(index)];
    }

private:
    VectorizedSize shape_;
    const int* data_;
};

} // namespace io
} // namespace base_backend
} // namespace charr

#endif
