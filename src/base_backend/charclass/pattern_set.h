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


#ifndef CHARR_BASE_CHARCLASS_PATTERN_SET_H
#define CHARR_BASE_CHARCLASS_PATTERN_SET_H

#include "../ci_utf8.h"
#include "../io/vectorized_size.h"

#include <unicode/uniset.h>
#include <vector>


namespace charr { namespace base_backend { namespace charclass {

/**
 * A pattern set for character-class searches
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-15)
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-05)
 *          Convert pattern strings to UTF-8 in the constructor;
 *          Use UnicodeSet instead of CharClass
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-02)
 *          New method: locateAll
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-06-10)
 *          negate
 */
class PatternSet {
public:
    PatternSet() : shape_(), data_() {}

    PatternSet(SEXP rvec, R_len_t _nrecycle, bool negate=false)
        : shape_(), data_()
    {
#ifndef NDEBUG
        if (!Rf_isString(rvec))
            throw StriException("DEBUG: !Rf_isString in PatternSet::PatternSet(SEXP rvec)");
#endif
        const R_len_t _n = LENGTH(rvec);
        shape_.reset(_n, _nrecycle);
        if (_n > 0) {
            io::Utf8Input rvec_cont(rvec, _n);
            data_.resize(static_cast<std::size_t>(_n));
            for (int i=0; i<_n; ++i) {
                if (rvec_cont.isNA(i))
                    data_[static_cast<std::size_t>(i)].setToBogus();
                else {
                    UErrorCode status = U_ZERO_ERROR;
                    data_[static_cast<std::size_t>(i)].applyPattern(
                        UnicodeString::fromUTF8(StringPiece(
                            rvec_cont.get(i).data(),
                            rvec_cont.get(i).length()
                        )),
                        status
                    );
                    STRI__CHECKICUSTATUS_THROW(status, {})
                    if (negate)
                        data_[static_cast<std::size_t>(i)].complement();
                    data_[static_cast<std::size_t>(i)].freeze();
                }
            }
        }
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
        return data_[static_cast<std::size_t>(shape_.index(index))].isBogus();
    }

    const UnicodeSet& get(R_len_t index) const {
        const UnicodeSet& value = data_[
            static_cast<std::size_t>(shape_.index(index))
        ];
#ifndef NDEBUG
        if (value.isBogus())
            throw StriException("cannot get a missing character class");
#endif
        return value;
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

private:
    io::VectorizedSize shape_;
    std::vector<UnicodeSet> data_;
};


} } } // namespace charr::base_backend::charclass

#endif
