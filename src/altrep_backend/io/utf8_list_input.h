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


#ifndef CHARR_ALTREP_UTF8_LIST_INPUT_H
#define CHARR_ALTREP_UTF8_LIST_INPUT_H

#include "utf8_input.h"
#include "vectorized_size.h"
#include "../ci_reader.h"

#include <memory>
#include <vector>

namespace charr {
namespace altrep_backend {
namespace io {


/**
 * A class to handle conversion between R lists of character
 * vectors and lists of UTF-8 string vectors
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 * @version 0.5-3 (Marek Gagolewski, 2015-06-27)
 *      warning on recycling rule, #174
 */
class Utf8ListInput {
public:
    Utf8ListInput();
    Utf8ListInput(
        ci::ReaderContext& context, SEXP rlist,
        R_len_t nrecycle, bool shallowrecycle=true
    );
    Utf8ListInput(const Utf8ListInput&) = delete;
    Utf8ListInput& operator=(const Utf8ListInput&) = delete;

    bool isNA(R_len_t index) const {
        return data_[static_cast<std::size_t>(shape_.index(index))] == nullptr;
    }

    const Utf8Input& get(R_len_t index) const {
        const std::unique_ptr<Utf8Input>& value = data_[
            static_cast<std::size_t>(shape_.index(index))
        ];
        if (!value)
            throw StriException("missing UTF-8 list element");
        return *value;
    }

    R_len_t get_n() const noexcept { return shape_.data_size(); }
    R_len_t get_nrecycle() const noexcept {
        return shape_.recycle_size();
    }

private:
    VectorizedSize shape_;
    std::vector<std::unique_ptr<Utf8Input>> data_;
};

} // namespace io
} // namespace altrep_backend
} // namespace charr

#endif
