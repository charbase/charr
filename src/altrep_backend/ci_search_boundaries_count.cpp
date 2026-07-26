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
#include "ci_reader.h"
#include "ci_brkiter.h"
#include "ci_utf8.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>


namespace {

bool has_direct_utf8_views(const charport::StrViews& values)
{
    bool direct = true;
    for (R_xlen_t i = 0; i < values.size(); ++i) {
        const charport::StrView value = values[i];
        if (value.is_na())
            continue;
        if (value.ptr == nullptr || value.len < 0)
            throw std::runtime_error("Reader returned an invalid string view");
        if (value.enc == cetype_ext_t::CE_BYTES)
            throw StriException(MSG__BYTESENC);
        if (value.enc != cetype_ext_t::CE_ASCII &&
                value.enc != cetype_ext_t::CE_UTF8 &&
                value.enc != cetype_ext_t::CE_ASCII_OR_UTF8) {
            direct = false;
        }
    }
    return direct;
}

charport::StrView direct_utf8_view(charport::StrView value) noexcept
{
    if (value.enc != cetype_ext_t::CE_ASCII && value.len >= 3 &&
            static_cast<uint8_t>(value.ptr[0]) == UTF8_BOM_BYTE1 &&
            static_cast<uint8_t>(value.ptr[1]) == UTF8_BOM_BYTE2 &&
            static_cast<uint8_t>(value.ptr[2]) == UTF8_BOM_BYTE3) {
        value.ptr += 3;
        value.len -= 3;
    }
    return value;
}

class BoundaryCounter : public StriBrkIterOptions {
private:
    BreakIterator* iterator_;
    UText* text_;

    void open(ci::DeferredWarnings& warnings)
    {
        UErrorCode status = U_ZERO_ERROR;
        const Locale locale_value = Locale::createFromName(locale);
        if (!rules.isEmpty()) {
            UParseError parse_error;
            iterator_ = new RuleBasedBreakIterator(
                UnicodeString(rules), parse_error, status
            );
        }
        else {
            switch (type) {
            case UBRK_CHARACTER:
                iterator_ = BreakIterator::createCharacterInstance(
                    locale_value, status
                );
                break;
            case UBRK_LINE:
                iterator_ = BreakIterator::createLineInstance(
                    locale_value, status
                );
                break;
            case UBRK_SENTENCE:
                iterator_ = BreakIterator::createSentenceInstance(
                    locale_value, status
                );
                break;
            case UBRK_WORD:
                iterator_ = BreakIterator::createWordInstance(
                    locale_value, status
                );
                break;
            default:
                throw StriException(MSG__INTERNAL_ERROR);
            }
        }
        STRI__CHECKICUSTATUS_THROW(status, {})

        if (status == U_USING_DEFAULT_WARNING && iterator_ && locale) {
            UErrorCode locale_status = U_ZERO_ERROR;
            const char* valid_locale = iterator_->getLocaleID(
                ULOC_VALID_LOCALE, locale_status
            );
            if (valid_locale && !std::strcmp(valid_locale, "root"))
                warnings.push(ICUError::getICUerrorName(status));
        }
    }

public:
    explicit BoundaryCounter(const StriBrkIterOptions& options)
        : StriBrkIterOptions(options), iterator_(nullptr), text_(nullptr)
    {
    }

    ~BoundaryCounter()
    {
        delete iterator_;
        if (text_)
            utext_close(text_);
    }

    R_len_t count(
        const char* value, R_len_t length, ci::DeferredWarnings& warnings
    )
    {
        if (!iterator_)
            open(warnings);

        UErrorCode status = U_ZERO_ERROR;
        text_ = utext_openUTF8(text_, value, length, &status);
        STRI__CHECKICUSTATUS_THROW(status, {})
        status = U_ZERO_ERROR;
        iterator_->setText(text_, status);
        STRI__CHECKICUSTATUS_THROW(status, {})

        R_len_t count = 0;
        if (skip_size <= 0) {
            while (iterator_->next() != BreakIterator::DONE)
                ++count;
            return count;
        }

        while (iterator_->next() != BreakIterator::DONE) {
            const int rule = iterator_->getRuleStatus();
            R_len_t skip = 0;
            for (; skip < skip_size; skip += 2) {
                if (rule >= skip_rules[skip] &&
                        rule < skip_rules[skip+1])
                    break;
            }
            if (skip == skip_size)
                ++count;
        }
        return count;
    }
};

}


/** Count the number of BreakIterator boundaries
 *
 * @param str character vector
 * @param opts_brkiter identifier
 * @return character vector
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-10-30)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-02)
 *          use StriRuleBasedBreakIterator
 */
SEXP ci_count_boundaries(SEXP str, SEXP opts_brkiter)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));

    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP ret;
    {
    // Deviation from stringi: keep the option's ICU storage inside the
    // unwind-safe scope so it is released before warning replay.
    StriBrkIterOptions opts_brkiter2(
        opts_brkiter, "line_break", STRI__DEFERRED_WARNINGS
    );
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    R_len_t str_length = ci::checked_r_len(
        context.size(str), "character vectors"
    );

    STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
        return Rf_allocVector(INTSXP, str_length);
    }));
    int* ret_tab = INTEGER(ret);

    {
        std::shared_ptr<ci::ReaderBorrow> borrow = context.acquire(str);
        const charport::StrViews& views = borrow->views();
        BoundaryCounter counter(opts_brkiter2);
        if (has_direct_utf8_views(views)) {
            for (R_len_t i = 0; i < str_length; ++i) {
                const charport::StrView value = direct_utf8_view(views[i]);
                ret_tab[i] = value.is_na()
                    ? NA_INTEGER
                    : counter.count(
                        value.ptr, value.len, STRI__DEFERRED_WARNINGS
                    );
            }
        }
        else {
            Utf8Input input(borrow, str_length);
            const Utf8Record* records = input.source_data();
            for (R_len_t i = 0; i < str_length; ++i) {
                const Utf8Record& value = records[i];
                ret_tab[i] = value.isNA()
                    ? NA_INTEGER
                    : counter.count(
                        value.data(), value.length(),
                        STRI__DEFERRED_WARNINGS
                    );
            }
        }
    }

    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END({ /* no action */  })
}
