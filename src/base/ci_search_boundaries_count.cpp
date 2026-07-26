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
#include "ci_brkiter.h"
#include "ci_utf8.h"
#include "utf8_views.h"

#include <cstdint>
#include <cstring>
#include <exception>


namespace charr { namespace base {

namespace {

class BoundaryCounter : public StriBrkIterOptions {
private:
    BreakIterator* iterator_;
    UText* text_;
    bool default_locale_warning_;

    void open()
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
                default_locale_warning_ = true;
        }
    }

public:
    explicit BoundaryCounter(const StriBrkIterOptions& options)
        : StriBrkIterOptions(options), iterator_(nullptr), text_(nullptr),
          default_locale_warning_(false)
    {
    }

    bool used_default_locale() const noexcept
    {
        return default_locale_warning_;
    }

    ~BoundaryCounter()
    {
        delete iterator_;
        if (text_)
            utext_close(text_);
    }

    R_len_t count(const char* value, R_len_t length)
    {
        if (!iterator_)
            open();

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
    StriBrkIterOptions opts_brkiter2(opts_brkiter, "line_break");

    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP ret;
    try {
        const R_len_t str_length = LENGTH(str);
        STRI__PROTECT(ret = Rf_allocVector(INTSXP, str_length));
        int* ret_tab = INTEGER(ret);
        bool default_locale_warning = false;

        Utf8Input input(str, str_length);
        BoundaryCounter counter(opts_brkiter2);
        for (R_len_t i = 0; i < str_length; ++i) {
            ret_tab[i] = input.isNA(i)
                ? NA_INTEGER
                : counter.count(
                    input.get(i).data(), input.get(i).length()
                );
        }
        default_locale_warning = counter.used_default_locale();
        if (default_locale_warning)
            Rf_warning("%s", ICUError::getICUerrorName(
                U_USING_DEFAULT_WARNING
            ));
    }
    catch (const StriException&) {
        throw;
    }
    catch (const std::exception& error) {
        throw StriException("%s", error.what());
    }

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END({ /* no action */  })
}

} } // namespace charr::base
