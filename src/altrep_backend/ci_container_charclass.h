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


#ifndef __ci_container_charclass_h
#define __ci_container_charclass_h

#include "ci_container_base.h"
#include "ci_container_utf8.h"
#include <memory>
#include <unicode/uniset.h>


/**
 * A container handling charclass searches
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-15)
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-05)
 *          Use StriContainerUTF8 to convert pattern strings in a constructor;
 *          Use UnicodeSet instead of CharClass
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-02)
 *          New method: locateAll
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-06-10)
 *          negate
 */
class StriContainerCharClass : public StriContainerBase {

private:

    UnicodeSet* data; // array

public:

    StriContainerCharClass() : StriContainerBase()
    {
        data = NULL;
    }

    StriContainerCharClass(
        ci::ReaderContext& context, SEXP rvec,
        R_len_t _nrecycle, bool negate=false
    )
    {
#ifndef NDEBUG
        if (!Rf_isString(rvec))
            throw StriException("DEBUG: !Rf_isString in StriContainerCharClass::StriContainerCharClass(SEXP rvec)");
#endif
        R_len_t _n = ci::checked_r_len(
            context.size(rvec), "character vectors"
        );
        this->init_Base(_n, _nrecycle, true);

        this->data = NULL;
        if (_n > 0) {
            StriContainerUTF8 rvec_cont(context, rvec, _n, true);
            std::unique_ptr<UnicodeSet[]> new_data(new UnicodeSet[_n]);
            for (int i=0; i<_n; ++i) {
                if (rvec_cont.isNA(i))
                    new_data[i].setToBogus();
                else {
                    UErrorCode status = U_ZERO_ERROR;
                    new_data[i].applyPattern(
                        UnicodeString::fromUTF8(StringPiece(
                            rvec_cont.get(i).data(), rvec_cont.get(i).length()
                        )),
                        status
                    );
                    STRI__CHECKICUSTATUS_THROW(status, {/* RAII owns new_data */})
                    if (negate)
                        new_data[i].complement();
                    new_data[i].freeze();
                }
            }
            this->data = new_data.release();
        }
    }

    StriContainerCharClass(StriContainerCharClass& container)
        :StriContainerBase((StriContainerBase&)container)
    {
        this->data = NULL;
        if (container.data) {
            std::unique_ptr<UnicodeSet[]> new_data(new UnicodeSet[container.n]);
            for (int i=0; i<container.n; ++i)
                new_data[i] = container.data[i];
            this->data = new_data.release();
        }
    }

    ~StriContainerCharClass() {
        if (data) delete [] data;
    }

    StriContainerCharClass& operator=(StriContainerCharClass& container)
    {
        if (this == &container)
            return *this;

        std::unique_ptr<UnicodeSet[]> new_data;
        if (container.data) {
            new_data.reset(new UnicodeSet[container.n]);
            for (int i=0; i<container.n; ++i)
                new_data[i] = container.data[i];
        }

        // Deviation from stringi: do not reuse an object after explicitly
        // ending its lifetime.
        delete [] this->data;
        (StriContainerBase&) (*this) = (StriContainerBase&)container;
        this->data = new_data.release();
        return *this;
    }


    /** check if the vectorized ith element is NA
     * @param i index
     * @return true if is NA
     */
    inline bool isNA(R_len_t i) const {
#ifndef NDEBUG
        if (i < 0 || i >= nrecycle)
            throw StriException("StriContainerCharClass::isNA(): INDEX OUT OF BOUNDS");
#endif
        return data[i%n].isBogus();
    }


    /** get the vectorized ith element
     * @param i index
     * @return integer
     */
    inline const UnicodeSet& get(R_len_t i) const {
#ifndef NDEBUG
        if (i < 0 || i >= nrecycle)
            throw StriException("StriContainerCharClass::get(): INDEX OUT OF BOUNDS");
        if (data[i%n].isBogus())
            throw StriException("StriContainerCharClass::get(): isNA");
#endif
        return (data[i%n]);
    }


    /** Locate all occurrences of a charclass
     *
     * @return total number of bytes @ pattern matches (idx_codepoint==false)
     * or total number of codepoints matched (idx_codepoint==true)
     */
    static R_len_t locateAll(deque< pair<R_len_t, R_len_t> >& occurrences,
                             const UnicodeSet* pattern_cur,
                             const char* str_cur_s, R_len_t str_cur_n,
                             bool merge_cur, bool idx_codepoint)
    {
        if (idx_codepoint) {
            R_len_t j, k;
            UChar32 chr;
            R_len_t sumcodepoints = 0;
            for (k=j=0; j<str_cur_n; ) {
                ++k;
                U8_NEXT(str_cur_s, j, str_cur_n, chr);
                if (chr < 0) // invalid utf-8 sequence
                    throw StriException(MSG__INVALID_UTF8);
                if (pattern_cur->contains(chr)) {
                    if (merge_cur && occurrences.size() > 0 &&
                            occurrences.back().second == k-1)
                        occurrences.back().second = k;
                    else
                        occurrences.push_back(pair<R_len_t, R_len_t>(k-1, k));
                    ++sumcodepoints;
                }
            }
            return sumcodepoints;
        }
        else {
            R_len_t j, jlast;
            UChar32 chr;
            R_len_t sumbytes = 0;
            for (jlast=j=0; j<str_cur_n; ) {
                U8_NEXT(str_cur_s, j, str_cur_n, chr);
                if (chr < 0) // invalid utf-8 sequence
                    throw StriException(MSG__INVALID_UTF8);
                if (pattern_cur->contains(chr)) {
                    if (merge_cur && occurrences.size() > 0 &&
                            occurrences.back().second == jlast)
                        occurrences.back().second = j;
                    else
                        occurrences.push_back(pair<R_len_t, R_len_t>(jlast, j));
                    sumbytes += j-jlast;
                }
                jlast = j;
            }
            return sumbytes;
        }
    }
};

#endif
