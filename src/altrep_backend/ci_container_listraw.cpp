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
#include "ci_container_listraw.h"


/**
 * Default constructor
 *
 */
StriContainerListRaw::StriContainerListRaw()
    : StriContainerBase(), borrow(), data(NULL)
{
}


/**
 * Construct String Container from R object
 * @param rstr R object
 *
 * if you want nrecycle > n, call set_nrecycle
 *
 * @version 1.6.2 (Marek Gagolewski, 2021-05-14)
 *    #354 Force the copying of ALTREP data
 */
StriContainerListRaw::StriContainerListRaw(
    ci::ReaderContext& context, SEXP rstr
)
    : StriContainerBase(), borrow(), data(NULL)
{
    if (Rf_isNull(rstr)) {
        this->init_Base(1, 1, true);
        std::unique_ptr<String8[]> new_data(new String8[this->n]);
        this->data = new_data.release(); // 1 string, NA
    }
    else if (isRaw(rstr)) {
        this->init_Base(1, 1, true);
        std::unique_ptr<String8[]> new_data(new String8[this->n]);
        bool memalloc = ALTREP(rstr);  // #354: force copying of ALTREP data
        const char* raw_data = NULL;
        R_len_t raw_length = 0;
        charport::unwind_protect([&]() -> SEXP {
            raw_length = LENGTH(rstr);
            raw_data = reinterpret_cast<const char*>(RAW(rstr));
            return R_NilValue;
        });
        new_data[0].initialize(raw_data, raw_length,
                               memalloc, false/*killbom*/, false/*isASCII*/); // shallow copy
        this->data = new_data.release();
    }
    else if (Rf_isVectorList(rstr)) {
        R_len_t nv = LENGTH(rstr);
        this->init_Base(nv, nv, true);
        std::unique_ptr<String8[]> new_data(new String8[this->n]);
        for (R_len_t i=0; i<this->n; ++i) {
            SEXP cur = charport::unwind_protect([&]() -> SEXP {
                return VECTOR_ELT(rstr, i);
            });
            if (!Rf_isNull(cur)) {
                bool memalloc = ALTREP(cur);  // #354: force copying of ALTREP data
                const char* raw_data = NULL;
                R_len_t raw_length = 0;
                charport::unwind_protect([&]() -> SEXP {
                    raw_length = LENGTH(cur);
                    raw_data = reinterpret_cast<const char*>(RAW(cur));
                    return R_NilValue;
                });
                new_data[i].initialize(raw_data, raw_length,
                                       memalloc, false/*killbom*/, false/*isASCII*/); // shallow copy
            }
            // else leave as-is, i.e., NA
        }
        this->data = new_data.release();
    }
    else { // it's surely a character vector (args have been checked)
        R_len_t nv = ci::checked_r_len(
            context.size(rstr), "character vectors"
        );
        this->init_Base(nv, nv, true);
        if (this->n == 0)
            return;

        std::shared_ptr<ci::ReaderBorrow> new_borrow = context.acquire(rstr);
        const charport::StrViews& views = new_borrow->views();
        // Deviation from stringi: borrow character bytes through charport
        // without interpreting their encoding or materializing CHARSXPs.
        std::unique_ptr<String8[]> new_data(new String8[this->n]);
        bool uses_borrowed_data = false;
        for (R_len_t i=0; i<this->n; ++i) {
            const charport::StrView cur = views[i];
            if (!cur.is_na()) {
                new_data[i].initialize(
                    cur.ptr, cur.len, false,
                    false/*killbom*/, false/*isASCII*/
                );
                uses_borrowed_data = true;
            }
            // else leave as-is, i.e., NA
        }
        if (uses_borrowed_data)
            this->borrow = new_borrow;
        this->data = new_data.release();
    }
}


StriContainerListRaw::StriContainerListRaw(StriContainerListRaw& container)
    : StriContainerBase((StriContainerBase&)container),
      borrow(container.borrow), data(NULL)
{
    if (container.data) {
        std::unique_ptr<String8[]> new_data(new String8[this->n]);
        for (int i=0; i<this->n; ++i) {
            new_data[i] = container.data[i];
        }
        this->data = new_data.release();
    }
}


StriContainerListRaw& StriContainerListRaw::operator=(StriContainerListRaw& container)
{
    if (this == &container)
        return *this;

    std::unique_ptr<String8[]> new_data;
    if (container.data) {
        new_data.reset(new String8[container.n]);
        for (int i=0; i<container.n; ++i) {
            new_data[i] = container.data[i];
        }
    }

    delete [] this->data;
    (StriContainerBase&) (*this) = (StriContainerBase&)container;
    this->borrow = container.borrow;
    this->data = new_data.release();
    return *this;
}


StriContainerListRaw::~StriContainerListRaw()
{
    if (data) {
        delete [] data;
        data = NULL;
    }
    borrow.reset();
}
