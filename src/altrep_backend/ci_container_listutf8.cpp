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
#include "ci_container_listutf8.h"

#include <memory>
#include <vector>


namespace {

void delete_containers(StriContainerUTF8** data, R_len_t n) noexcept
{
    if (!data)
        return;
    for (R_len_t i=0; i<n; ++i)
        delete data[i];
    delete [] data;
}

} // namespace


/**
 * Default constructor
 *
 */
StriContainerListUTF8::StriContainerListUTF8()
    : StriContainerBase(), data(NULL)
{
}


/**
 * Construct the Container from an R list
 * @param rvec R list vector
 * @param nrecycle extend length of each character vector stored [vectorization]
 * @param shallowrecycle will stored character vectors be ever modified?
 */
StriContainerListUTF8::StriContainerListUTF8(
    ci::ReaderContext& context, SEXP rvec,
    R_len_t _nrecycle, bool _shallowrecycle
) : StriContainerBase(), data(NULL)
{
#ifndef NDEBUG
    if (!Rf_isVectorList(rvec))
        throw StriException("DEBUG: !isVectorList in StriContainerListUTF8::StriContainerListUTF8(SEXP rvec)");
#endif
    R_len_t rvec_length = 0;
    std::vector<SEXP> elements;
    charport::unwind_protect([&]() -> SEXP {
        rvec_length = ci::checked_r_len(XLENGTH(rvec), "lists");
        elements.resize(static_cast<size_t>(rvec_length));
        for (R_len_t i=0; i<rvec_length; ++i)
            elements[static_cast<size_t>(i)] = VECTOR_ELT(rvec, i);
        return R_NilValue;
    });
    this->init_Base(rvec_length, rvec_length, true);

    if (this->n > 0) {
        std::vector<std::unique_ptr<StriContainerUTF8> > containers;
        containers.reserve(static_cast<size_t>(this->n));
        bool recycling_warning = false;
        for (R_len_t i=0; i<this->n; ++i) {
            SEXP element = elements[static_cast<size_t>(i)];
            R_len_t element_length = ci::checked_r_len(
                context.size(element), "character vectors"
            );
            // Deviation from stringi: an empty element must not trigger
            // integer division by zero in the recycling check.
            if (!recycling_warning && element_length > 0 &&
                    _nrecycle % element_length != 0) {
                context.warn(MSG__WARN_RECYCLING_RULE);
                recycling_warning = true;
            }
        }

        for (R_len_t i=0; i<this->n; ++i) {
            SEXP element = elements[static_cast<size_t>(i)];
            containers.emplace_back(new StriContainerUTF8(
                context, element,
                _nrecycle, _shallowrecycle
            ));
        }

        std::unique_ptr<StriContainerUTF8*[]> new_data(
            new StriContainerUTF8*[this->n]
        );
        for (R_len_t i=0; i<this->n; ++i)
            new_data[i] = containers[static_cast<size_t>(i)].release();
        this->data = new_data.release();
    }
}


StriContainerListUTF8::StriContainerListUTF8(StriContainerListUTF8& container)
    : StriContainerBase((StriContainerBase&)container), data(NULL)
{
    if (container.data) {
        std::vector<std::unique_ptr<StriContainerUTF8> > containers;
        containers.reserve(static_cast<size_t>(this->n));
        for (int i=0; i<container.n; ++i) {
            if (container.data[i])
                containers.emplace_back(new StriContainerUTF8(*container.data[i]));
            else
                containers.emplace_back();
        }

        std::unique_ptr<StriContainerUTF8*[]> new_data(
            new StriContainerUTF8*[this->n]
        );
        for (R_len_t i=0; i<this->n; ++i)
            new_data[i] = containers[static_cast<size_t>(i)].release();
        this->data = new_data.release();
    }
}


StriContainerListUTF8& StriContainerListUTF8::operator=(StriContainerListUTF8& container)
{
    if (this == &container)
        return *this;

    std::vector<std::unique_ptr<StriContainerUTF8> > containers;
    if (container.data) {
        containers.reserve(static_cast<size_t>(container.n));
        for (int i=0; i<container.n; ++i) {
            if (container.data[i])
                containers.emplace_back(new StriContainerUTF8(*container.data[i]));
            else
                containers.emplace_back();
        }
    }

    std::unique_ptr<StriContainerUTF8*[]> new_data;
    if (container.data) {
        new_data.reset(new StriContainerUTF8*[container.n]);
        for (R_len_t i=0; i<container.n; ++i)
            new_data[i] = containers[static_cast<size_t>(i)].release();
    }

    // Deviation from stringi: replace the old array without explicitly ending
    // and then reusing this object's lifetime.
    delete_containers(this->data, this->n);
    (StriContainerBase&) (*this) = (StriContainerBase&)container;
    this->data = new_data.release();
    return *this;
}


StriContainerListUTF8::~StriContainerListUTF8()
{
    delete_containers(data, n);
    data = NULL;
}
