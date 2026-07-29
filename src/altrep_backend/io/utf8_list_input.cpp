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

#include "utf8_list_input.h"

#include "../ci_stringi.h"

namespace charr {
namespace altrep_backend {
namespace io {

Utf8ListInput::Utf8ListInput() : shape_(), data_() {}

Utf8ListInput::Utf8ListInput(
    ci::ReaderContext& context, SEXP source,
    R_len_t recycle_size, bool shallow_recycle
) : shape_(), data_()
{
#ifndef NDEBUG
    if (!Rf_isVectorList(source))
        throw StriException("UTF-8 list input requires an R list");
#endif

    R_len_t source_size = 0;
    std::vector<SEXP> elements;
    ci::unwind_protect([&]() -> SEXP {
        source_size = ci::checked_r_len(XLENGTH(source), "lists");
        elements.resize(static_cast<std::size_t>(source_size));
        for (R_len_t i = 0; i < source_size; ++i)
            elements[static_cast<std::size_t>(i)] = VECTOR_ELT(source, i);
        return R_NilValue;
    });
    shape_.reset(source_size, source_size);
    data_.reserve(static_cast<std::size_t>(source_size));

    bool recycling_warning = false;
    for (R_len_t i = 0; i < source_size; ++i) {
        const R_len_t element_size = ci::checked_r_len(
            context.size(elements[static_cast<std::size_t>(i)]),
            "character vectors"
        );
        if (!recycling_warning && element_size > 0 &&
                recycle_size % element_size != 0) {
            context.warn(MSG__WARN_RECYCLING_RULE);
            recycling_warning = true;
        }
    }

    for (R_len_t i = 0; i < source_size; ++i) {
        data_.emplace_back(new Utf8Input(
            context, elements[static_cast<std::size_t>(i)],
            recycle_size, shallow_recycle
        ));
    }
}

} // namespace io
} // namespace altrep_backend
} // namespace charr
