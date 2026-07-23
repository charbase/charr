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
#include "ci_container_listint.h"


/**
 * Default constructor
 *
 */
StriContainerListInt::StriContainerListInt()
    : StriContainerBase(), data(NULL)
{
}


/**
 * Construct Container from R cobject
 * @param rstr R object
 *
 * if you want nrecycle > n, call set_nrecycle
 */
StriContainerListInt::StriContainerListInt(SEXP rstr)
    : StriContainerBase(), data(NULL)
{
    if (Rf_isNull(rstr)) {
        this->init_Base(1, 1, true);
        std::unique_ptr<IntVec[]> new_data(new IntVec[this->n]);
        this->data = new_data.release(); // 1 vector, NA/NULL
    }
    else if (Rf_isInteger(rstr)) {
        this->init_Base(1, 1, true);
        std::unique_ptr<IntVec[]> new_data(new IntVec[this->n]);
        const int* values = NULL;
        R_len_t values_length = 0;
        // Deviation from stringi: numeric ALTREP access can unwind. Keep the
        // partially built shallow-wrapper array under RAII and request only a
        // read-only pointer while translating that R boundary into C++.
        charport::unwind_protect([&]() -> SEXP {
            values_length = LENGTH(rstr);
            values = INTEGER_RO(rstr);
            return R_NilValue;
        });
        new_data[0].initialize(values, values_length);
        this->data = new_data.release();
    }
    else // if (Rf_isVectorList(rstr)) -- args already checked
    {
        R_len_t nv = 0;
        charport::unwind_protect([&]() -> SEXP {
            nv = LENGTH(rstr);
            return R_NilValue;
        });
        this->init_Base(nv, nv, true);
        std::unique_ptr<IntVec[]> new_data(new IntVec[this->n]);
        for (R_len_t i=0; i<this->n; ++i) {
            SEXP cur = R_NilValue;
            const int* values = NULL;
            R_len_t values_length = 0;
            charport::unwind_protect([&]() -> SEXP {
                cur = VECTOR_ELT(rstr, i);
                if (!Rf_isNull(cur)) {
                    values_length = LENGTH(cur);
                    values = INTEGER_RO(cur);
                }
                return R_NilValue;
            });
            if (!Rf_isNull(cur))
                new_data[i].initialize(values, values_length);
            // else leave as-is, i.e., NULL/NA
        }
        this->data = new_data.release();
    }
}


StriContainerListInt::StriContainerListInt(StriContainerListInt& container)
    : StriContainerBase((StriContainerBase&)container), data(NULL)
{
    if (container.data) {
        std::unique_ptr<IntVec[]> new_data(new IntVec[this->n]);
        for (int i=0; i<this->n; ++i) {
            new_data[i] = container.data[i];
        }
        this->data = new_data.release();
    }
}


StriContainerListInt& StriContainerListInt::operator=(StriContainerListInt& container)
{
    if (this == &container)
        return *this;

    std::unique_ptr<IntVec[]> new_data;
    if (container.data) {
        new_data.reset(new IntVec[container.n]);
        for (int i=0; i<container.n; ++i) {
            new_data[i] = container.data[i];
        }
    }

    delete [] this->data;
    (StriContainerBase&) (*this) = (StriContainerBase&)container;
    this->data = new_data.release();
    return *this;
}


StriContainerListInt::~StriContainerListInt()
{
    if (data) {
        delete [] data;
        data = NULL;
    }
}
