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

#include "raw_list_input.h"

#include "../ci_stringi.h"

#include <stdexcept>
#include <vector>

namespace charr {
namespace altrep_backend {
namespace io {

struct RawListInput::Storage {
    std::shared_ptr<ci::ReaderBorrow> borrow;
    std::vector<ByteView> records;
};

namespace raw_list_input {

const char empty_bytes[] = "";

ByteView raw_view(SEXP value)
{
    if (Rf_isNull(value))
        return ByteView();

    const char* data = nullptr;
    R_len_t length = 0;
    ci::unwind_protect([&]() -> SEXP {
        length = LENGTH(value);
        data = reinterpret_cast<const char*>(RAW(value));
        return R_NilValue;
    });
    if (length == 0)
        return ByteView(empty_bytes, 0);
    // RAW() materializes an ALTREP raw vector when necessary. Its data pointer
    // remains valid while the argument (or its parent list) is rooted by the
    // active .Call, so the container can keep a view instead of another copy.
    return ByteView(data, length);
}

} // namespace raw_list_input

using namespace raw_list_input;

RawListInput::RawListInput()
    : storage_(new Storage()), shape_()
{
}

RawListInput::RawListInput(
    ci::ReaderContext& context, SEXP input
) : storage_(new Storage()), shape_()
{
    if (Rf_isNull(input)) {
        shape_.reset(1, 1);
        storage_->records.push_back(ByteView());
        return;
    }

    if (isRaw(input)) {
        shape_.reset(1, 1);
        storage_->records.push_back(raw_view(input));
        return;
    }

    if (Rf_isVectorList(input)) {
        const R_len_t length = ci::checked_r_len(
            XLENGTH(input), "lists"
        );
        shape_.reset(length, length);
        storage_->records.reserve(static_cast<std::size_t>(length));
        for (R_len_t i = 0; i < length; ++i) {
            SEXP value = ci::unwind_protect([&]() -> SEXP {
                return VECTOR_ELT(input, i);
            });
            storage_->records.push_back(raw_view(value));
        }
        return;
    }

    const R_len_t length = ci::checked_r_len(
        context.size(input), "character vectors"
    );
    shape_.reset(length, length);
    if (length == 0)
        return;
    storage_->borrow = context.acquire(input);
    const charport::StrViews& views = storage_->borrow->views();
    storage_->records.reserve(static_cast<std::size_t>(length));
    for (R_len_t i = 0; i < length; ++i) {
        const charport::StrView value = views[i];
        storage_->records.push_back(value.is_na()
            ? ByteView()
            : ByteView(value.ptr, value.len));
    }
}

bool RawListInput::isNA(R_len_t i) const
{
    return storage_->records[
        static_cast<std::size_t>(shape_.index(i))
    ].isNA();
}

const ByteView& RawListInput::get(R_len_t i) const
{
    const ByteView& value = storage_->records[
        static_cast<std::size_t>(shape_.index(i))
    ];
    if (value.isNA())
        throw StriException("cannot get a missing byte record");
    return value;
}

} // namespace io
} // namespace altrep_backend
} // namespace charr
