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
#include "ci_utf8.h"
#include "ci_brkiter.h"
#include "utf8_views.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>


namespace charr { namespace base {

namespace {

class BoundaryIterator : public StriBrkIterOptions {
private:
    BreakIterator* iterator_;
    UText* text_;
    R_len_t position_;
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

    bool skip() const
    {
        if (skip_size <= 0)
            return false;
        const int rule = iterator_->getRuleStatus();
        for (R_len_t i = 0; i < skip_size; i += 2) {
            if (rule >= skip_rules[i] && rule < skip_rules[i+1])
                return true;
        }
        return false;
    }

public:
    explicit BoundaryIterator(const StriBrkIterOptions& options)
        : StriBrkIterOptions(options), iterator_(nullptr), text_(nullptr),
          position_(0), default_locale_warning_(false)
    {
    }

    ~BoundaryIterator()
    {
        delete iterator_;
        if (text_)
            utext_close(text_);
    }

    bool used_default_locale() const noexcept
    {
        return default_locale_warning_;
    }

    void set_text(const char* value, R_len_t length)
    {
        if (!iterator_)
            open();

        UErrorCode status = U_ZERO_ERROR;
        text_ = utext_openUTF8(text_, value, length, &status);
        STRI__CHECKICUSTATUS_THROW(status, {})
        status = U_ZERO_ERROR;
        iterator_->setText(text_, status);
        STRI__CHECKICUSTATUS_THROW(status, {})
        position_ = 0;
    }

    bool next(std::pair<R_len_t, R_len_t>& occurrence)
    {
        R_len_t start = position_;
        for (;;) {
            const R_len_t end = iterator_->next();
            if (end == BreakIterator::DONE)
                return false;
            position_ = end;
            if (!skip()) {
                occurrence.first = start;
                occurrence.second = end;
                return true;
            }
            start = end;
        }
    }
};

class BoundaryRanges {
private:
    static constexpr R_len_t inline_capacity_ = 32;
    std::array<std::pair<R_len_t, R_len_t>, inline_capacity_> inline_;
    std::vector<std::pair<R_len_t, R_len_t> > overflow_;
    R_len_t size_;

public:
    BoundaryRanges() : inline_(), overflow_(), size_(0)
    {
    }

    void push_back(const std::pair<R_len_t, R_len_t>& value)
    {
        if (size_ < inline_capacity_ && overflow_.empty()) {
            inline_[static_cast<std::size_t>(size_)] = value;
        }
        else {
            if (overflow_.empty()) {
                overflow_.reserve(inline_capacity_ * 2);
                overflow_.insert(
                    overflow_.end(), inline_.begin(), inline_.end()
                );
            }
            overflow_.push_back(value);
        }
        ++size_;
    }

    R_len_t size() const noexcept
    {
        return size_;
    }

    std::pair<R_len_t, R_len_t>& back()
    {
        return overflow_.empty()
            ? inline_[static_cast<std::size_t>(size_-1)]
            : overflow_.back();
    }

    const std::pair<R_len_t, R_len_t>& operator[](R_len_t i) const
    {
        return overflow_.empty()
            ? inline_[static_cast<std::size_t>(i)]
            : overflow_[static_cast<std::size_t>(i)];
    }
};

}

/** Split a string at BreakIterator boundaries
 *
 * @param str character vector
 * @param n integer
 * @param tokens_only logical
 * @param simplify logical
 * @param opts_brkiter named list
 * @return list
 *
 * @version 0.2-2 (Marek Gagolewski, 2014-04-21)
 *
 * @version 0.2-2 (Marek Gagolewski, 2014-04-23)
 *          removed "title": For Unicode 4.0 and above title boundary
 *          iteration, please use Word Boundary iterator.
 *
 * @version 0.2-2 (Marek Gagolewski, 2014-04-25)
 *          use ci__split_or_locate_boundaries
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-10-29)
 *          use opts_brkiter
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-28)
 *          new args: n, tokens_only, simplify
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-02)
 *          use StriRuleBasedBreakIterator
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-04)
 *    allow `simplify=NA`; FR #126: pass n to ci_list2matrix
 */
SEXP ci_split_boundaries(SEXP str, SEXP n, SEXP tokens_only, SEXP simplify, SEXP opts_brkiter)
{
    bool tokens_only1 = ci__prepare_arg_logical_1_notNA(tokens_only, "tokens_only");
    PROTECT(simplify = ci__prepare_arg_logical_1(simplify, "simplify"));
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    PROTECT(n = ci__prepare_arg_integer(n, "n"));

    STRI__ERROR_HANDLER_BEGIN(3)
    SEXP ret;
    const R_len_t n_length = LENGTH(n);
    bool default_locale_warning = false;
    bool deferred_error = false;
    char deferred_error_message[StriException_BUFSIZE];
    try {
    StriBrkIterOptions opts_brkiter2(opts_brkiter, "line_break");
    R_len_t vectorize_length = ci__recycling_rule(true, 2,
                               LENGTH(str), LENGTH(n));
    IndexedUtf8Input str_cont(str, vectorize_length);
    const int* n_values = INTEGER(n);
    // The usual unlimited split has one negative n. Hoist it so the hot loop
    // avoids both recycling bookkeeping and the sentinel range check.
    const bool scalar_unlimited_n = n_length == 1 &&
        n_values[0] != NA_INTEGER && n_values[0] < 0;
    R_len_t n_index = 0;
    BoundaryIterator brkiter(opts_brkiter2);
    bool locale_status_recorded = false;

    STRI__PROTECT(ret = Rf_allocVector(VECSXP, vectorize_length));

    for (R_len_t i = 0; i < vectorize_length; ++i)
    {
        int n_cur;
        if (scalar_unlimited_n) {
            n_cur = INT_MAX;
        }
        else {
            n_cur = n_values[n_index];
            if (++n_index == n_length)
                n_index = 0;
        }
        if (n_cur == NA_INTEGER) {
            SET_VECTOR_ELT(ret, i, ci__vector_NA_strings(1));
            continue;
        }

        if (str_cont.isNA(i)) {
            SET_VECTOR_ELT(ret, i, ci__vector_NA_strings(1));
            continue;
        }

        if (!scalar_unlimited_n && n_cur >= INT_MAX-1)
            throw StriException(MSG__INCORRECT_NAMED_ARG "; " MSG__EXPECTED_SMALLER, "n");
        else if (n_cur < 0)
            n_cur = INT_MAX;
        else if (n_cur == 0) {
            SET_VECTOR_ELT(ret, i, Rf_allocVector(STRSXP, 0));
            continue;
        }

        const R_len_t str_cur_n = str_cont.get(i).length();
        const char* str_cur_s = str_cont.get(i).data();
        BoundaryRanges occurrences;
        brkiter.set_text(str_cur_s, str_cur_n);
        if (!locale_status_recorded) {
            default_locale_warning = brkiter.used_default_locale();
            locale_status_recorded = true;
        }

        pair<R_len_t,R_len_t> curpair;
        R_len_t k = 0;
        while (k < n_cur && brkiter.next(curpair)) {
            occurrences.push_back(curpair);
            ++k; // another field
        }


        R_len_t noccurrences = occurrences.size();
        if (noccurrences <= 0) {
            SET_VECTOR_ELT(ret, i, ci__vector_empty_strings(0)); // @TODO: Should it be a NA? Hard to say...
            continue;
        }
        if (k == n_cur && !tokens_only1)
            occurrences.back().second = str_cur_n;

        SEXP ans;
        STRI__PROTECT(ans = Rf_allocVector(STRSXP, noccurrences));
        for (R_len_t j = 0; j < noccurrences; ++j) {
            const std::pair<R_len_t, R_len_t>& occurrence = occurrences[j];
            SET_STRING_ELT(ans, j, Rf_mkCharLenCE(
                str_cur_s+occurrence.first,
                occurrence.second-occurrence.first, CE_UTF8
            ));
        }
        SET_VECTOR_ELT(ret, i, ans);
        STRI__UNPROTECT(1);
    }
    }
    catch (const StriException& error) {
        // A promoted warning must win over the later operation error, but all
        // iterator and conversion owners must be gone before R handles it.
        if (!default_locale_warning)
            throw;
        std::strncpy(
            deferred_error_message, error.getMessage(),
            StriException_BUFSIZE-1
        );
        deferred_error_message[StriException_BUFSIZE-1] = '\0';
        deferred_error = true;
    }

    if (default_locale_warning)
        Rf_warning("%s", ICUError::getICUerrorName(
            U_USING_DEFAULT_WARNING
        ));
    if (deferred_error)
        throw StriException("%s", deferred_error_message);

    if (LOGICAL(simplify)[0] == NA_LOGICAL || LOGICAL(simplify)[0]) {
        R_len_t n_min = 0;
        int* n_tab = INTEGER(n);
        for (R_len_t i=0; i<n_length; ++i) {
            if (n_tab[i] != NA_INTEGER && n_min < n_tab[i])
                n_min = n_tab[i];
        }
        SEXP robj_TRUE, robj_n_min, robj_na_strings, robj_empty_strings;
        STRI__PROTECT(robj_TRUE = Rf_ScalarLogical(TRUE));
        STRI__PROTECT(robj_n_min = Rf_ScalarInteger(n_min));
        STRI__PROTECT(robj_na_strings = ci__vector_NA_strings(1));
        STRI__PROTECT(robj_empty_strings = ci__vector_empty_strings(1));
        STRI__PROTECT(ret = ci_list2matrix(ret, robj_TRUE,
                                             (LOGICAL(simplify)[0] == NA_LOGICAL)?robj_na_strings
                                             :robj_empty_strings,
                                             robj_n_min))
    }

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END({ /* no action */ })
}

} } // namespace charr::base
