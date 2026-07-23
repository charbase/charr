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
#include "ci_container_utf8.h"
#include "ci_ucnv.h"

#include <memory>
#include <utility>
#include <vector>


namespace {

class CiUtf8Normalizer {
private:
    StriUcnv ucnv_latin1_;
    StriUcnv ucnv_native_;
    R_len_t outbuf_size_;
    std::vector<char> outbuf_;

public:
    CiUtf8Normalizer()
#if defined(_WIN32) || defined(_WIN64)
        : ucnv_latin1_("WINDOWS-1252"), ucnv_native_(NULL),
#else
        : ucnv_latin1_("ISO-8859-1"), ucnv_native_(NULL),
#endif
          outbuf_size_(-1), outbuf_()
    {
    }

    bool needs_conversion(const charport::StrView& value)
    {
        return !value.is_na() &&
            (value.enc == cetype_ext_t::CE_LATIN1 ||
             (value.enc == cetype_ext_t::CE_NATIVE &&
              !ucnv_native_.isUTF8()));
    }

    bool needs_buffer() const
    {
        return outbuf_size_ < 0;
    }

    bool normalize(
        String8& output, const charport::StrView& value,
        R_len_t conversion_max_length
    )
    {
        if (value.is_na())
            return false;

        if (value.enc == cetype_ext_t::CE_ASCII) {
            output.initialize(
                value.ptr, value.len, false, false, true
            );
            return true;
        }

        if (value.enc == cetype_ext_t::CE_UTF8 ||
                value.enc == cetype_ext_t::CE_ASCII_OR_UTF8) {
            // Deviation from stringi's CHARSXP path: only charport's
            // deliberately ambiguous mark needs an ASCII scan. The explicit
            // CE_ASCII and CE_UTF8 marks are authoritative.
            const bool payload_is_ascii =
                value.enc == cetype_ext_t::CE_ASCII_OR_UTF8 &&
                ci::is_ascii(value.ptr, value.len);
            output.initialize(
                value.ptr, value.len, false,
                !payload_is_ascii, payload_is_ascii
            );
            return true;
        }

        if (value.enc == cetype_ext_t::CE_BYTES)
            throw StriException(MSG__BYTESENC);

        UConverter* converter;
        if (value.enc == cetype_ext_t::CE_LATIN1) {
            converter = ucnv_latin1_.getConverter();
        }
        else if (value.enc == cetype_ext_t::CE_NATIVE) {
            if (ucnv_native_.isUTF8()) {
                output.initialize(
                    value.ptr, value.len, false, true, false
                );
                return true;
            }
            converter = ucnv_native_.getConverter();
        }
        else {
            throw StriException("unknown charport string encoding");
        }

        if (outbuf_size_ < 0) {
            outbuf_size_ = UCNV_GET_MAX_BYTES_FOR_STRING(
                conversion_max_length, 4
            );
            // Deviation from stringi: u_strToUTF8 receives explicit source
            // and destination lengths, and String8 stores no terminator.
            outbuf_.resize(static_cast<size_t>(outbuf_size_));
        }

        UErrorCode status = U_ZERO_ERROR;
        UnicodeString tmp(
            value.ptr, value.len, converter, status
        );
        STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

        int output_length = 0;
        u_strToUTF8(
            outbuf_.data(), outbuf_size_, &output_length,
            tmp.getBuffer(), tmp.length(), &status
        );
        STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

        output.initialize(
            outbuf_.data(), output_length, true, false, false
        );
        return false;
    }
};

} // namespace

/**
 * Default constructor
 *
 */
StriContainerUTF8::StriContainerUTF8()
    : StriContainerBase(), borrow(), str()
{
}


/**
 * Construct String Container from R character vector
 *
 * @param rstr R character vector
 * @param nrecycle extend length [vectorization]
 * @param shallowrecycle will \code{this->str} be ever modified?
 *
 * @version 1.0.6 (Marek Gagolewski, 2017-05-25)
 *    #270 latin-1 is windows-1252 on Windows
 *
 * @version charr 0.1.0
 *    Borrow one full charport view and copy only strings that need transcoding.
 */
StriContainerUTF8::StriContainerUTF8(
    ci::ReaderContext& context, SEXP rstr,
    R_len_t _nrecycle, bool _shallowrecycle
) : StriContainerBase(), borrow(), str()
{
    R_len_t nrstr = ci::checked_r_len(
        context.size(rstr), "character vectors"
    );
    this->init_Base(nrstr, _nrecycle, _shallowrecycle);

    if (this->n == 0)
        return; /* nothing more to do */

    STRI_ASSERT(this->n > 0);
    std::shared_ptr<ci::ReaderBorrow> new_borrow = context.acquire(rstr);
    const charport::StrViews& views = new_borrow->views();

    // Deviation from stringi: Reader access and conversion can throw, so keep
    // the partially initialized array under RAII until construction succeeds.
    std::unique_ptr<String8[]> new_str(new String8[this->n]);

    CiUtf8Normalizer normalizer;
    bool uses_borrowed_data = false;

    for (R_len_t i=0; i<nrstr; ++i) {
        const charport::StrView curs = views[i];
        R_len_t maxlen = curs.len;
        if (normalizer.needs_buffer() && normalizer.needs_conversion(curs)) {
            for (R_len_t z=i+1; z<nrstr; ++z) {
                const charport::StrView candidate = views[z];
                if (normalizer.needs_conversion(candidate) &&
                        maxlen < candidate.len)
                    maxlen = candidate.len;
            }
        }
        if (normalizer.normalize(new_str[i], curs, maxlen))
            uses_borrowed_data = true;
    }

    if (!_shallowrecycle) {
        for (R_len_t i=nrstr; i<this->n; ++i) {
            new_str[i] = new_str[i%nrstr];
        }
    }

    if (uses_borrowed_data)
        this->borrow = new_borrow;
    this->str = std::move(new_str);
}


StriContainerUTF8::StriContainerUTF8(
    const std::shared_ptr<ci::ReaderBorrow>& source_borrow,
    const charport::StrView& value,
    R_len_t _nrecycle, bool _shallowrecycle
) : StriContainerBase(), borrow(), str()
{
    this->init_Base(1, _nrecycle, _shallowrecycle);
    if (this->n == 0)
        return;

    // Deviation from stringi: ci_sub_all selects one record from an existing
    // full-vector Reader instead of materializing a one-element STRSXP.
    std::unique_ptr<String8[]> new_str(new String8[this->n]);
    CiUtf8Normalizer normalizer;
    const bool uses_borrowed_data = normalizer.normalize(
        new_str[0], value, value.len
    );

    if (!_shallowrecycle) {
        for (R_len_t i=1; i<this->n; ++i)
            new_str[i] = new_str[0];
    }

    if (uses_borrowed_data)
        this->borrow = source_borrow;
    this->str = std::move(new_str);
}


StriContainerUTF8::StriContainerUTF8(StriContainerUTF8& container)
    : StriContainerBase((StriContainerBase&)container),
      borrow(container.borrow), str()
{
    if (container.str) {
        this->str.reset(new String8[this->n]);
        for (int i=0; i<this->n; ++i) {
            this->str[i] = container.str[i];
        }
    }
}


StriContainerUTF8& StriContainerUTF8::operator=(StriContainerUTF8& container)
{
    if (this == &container)
        return *this;

    std::unique_ptr<String8[]> new_str;
    if (container.str) {
        new_str.reset(new String8[container.n]);
        for (int i=0; i<container.n; ++i) {
            new_str[i] = container.str[i];
        }
    }

    // Deviation from stringi: explicit destruction followed by member reuse is
    // invalid now that the container owns a non-trivial Reader lease.
    this->str.reset();
    (StriContainerBase&) (*this) = (StriContainerBase&)container;
    this->borrow = container.borrow;
    this->str = std::move(new_str);
    return *this;
}


StriContainerUTF8::~StriContainerUTF8() = default;
