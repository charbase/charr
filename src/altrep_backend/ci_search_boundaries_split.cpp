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
#include "ci_builder.h"
#include "ci_utf8.h"
#include "ci_container_integer.h"
#include "ci_brkiter.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>


namespace {

class BoundaryIterator : public StriBrkIterOptions {
private:
    BreakIterator* iterator_;
    UText* text_;
    R_len_t position_;

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
          position_(0)
    {
    }

    ~BoundaryIterator()
    {
        delete iterator_;
        if (text_)
            utext_close(text_);
    }

    void set_text(
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
    int simplify1 = NA_LOGICAL;

    STRI__ERROR_HANDLER_BEGIN(3)
    SEXP ret;
    {
    // Deviation from stringi: keep the option's ICU storage inside the
    // unwind-safe staging scope so it is released before warning replay.
    StriBrkIterOptions opts_brkiter2(
        opts_brkiter, "line_break", STRI__DEFERRED_WARNINGS
    );
    charport::unwind_protect([&]() -> SEXP {
        simplify1 = LOGICAL_RO(simplify)[0];
        return R_NilValue;
    });
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    R_len_t str_n = ci::checked_r_len(
        context.size(str), "character vectors"
    );
    R_len_t n_n = 0;
    R_len_t vectorize_length = 0;
    charport::unwind_protect([&]() -> SEXP {
        n_n = LENGTH(n);
        vectorize_length = ci__recycling_rule(
            false, 2, str_n, n_n
        );
        return R_NilValue;
    });
    // Deviation from stringi: queue the controllable recycling warning until
    // Reader, break-iterator, and lazy-output owners have been released.
    if (vectorize_length > 0 &&
            (vectorize_length % str_n != 0 ||
             vectorize_length % n_n != 0))
        context.warn(MSG__WARN_RECYCLING_RULE);

    // Deviation from stringi: preinitialize lazy empty vectors, then replace
    // each slot with a scalar or exact-size Store once its boundary count is
    // known.
    vector<charport::charvec::Store> stores;
    stores.reserve(static_cast<size_t>(vectorize_length));
    for (R_len_t i=0; i<vectorize_length; ++i)
        stores.emplace_back(0, 0);
    {
        StriContainerInteger n_cont(n, vectorize_length);
        Utf8Input str_cont(context, str, vectorize_length);
        BoundaryIterator brkiter(opts_brkiter2);
        charport::charvec::Builder output(0);

        for (R_len_t i = 0; i < vectorize_length; ++i)
        {
            if (n_cont.isNA(i)) {
                stores[i] = charport::charvec::Store::scalar(
                    nullptr, 0, cetype_ext_t::CE_NA
                );
                continue;
            }
            int  n_cur = n_cont.get(i);

            if (str_cont.isNA(i)) {
                stores[i] = charport::charvec::Store::scalar(
                    nullptr, 0, cetype_ext_t::CE_NA
                );
                continue;
            }

            if (n_cur >= INT_MAX-1)
                throw StriException(MSG__INCORRECT_NAMED_ARG "; " MSG__EXPECTED_SMALLER, "n");
            else if (n_cur < 0)
                n_cur = INT_MAX;
            else if (n_cur == 0) {
                continue;
            }

            const R_len_t str_cur_n = str_cont.get(i).length();
            const char* str_cur_s = str_cont.get(i).data();
            BoundaryRanges occurrences;
            brkiter.set_text(
                str_cur_s, str_cur_n, STRI__DEFERRED_WARNINGS
            );

            pair<R_len_t,R_len_t> curpair;
            R_len_t k = 0;
            while (k < n_cur && brkiter.next(curpair)) {
                occurrences.push_back(curpair);
                ++k; // another field
            }

            R_len_t noccurrences = occurrences.size();
            if (noccurrences <= 0) {
                continue; // @TODO: Should it be a NA? Hard to say...
            }
            if (k == n_cur && !tokens_only1)
                occurrences.back().second = str_cur_n;

            if (noccurrences == 1) {
                const std::pair<R_len_t, R_len_t>& curoccur =
                    occurrences[0];
                const char* value = str_cur_s+curoccur.first;
                size_t value_length = static_cast<size_t>(
                    curoccur.second-curoccur.first
                );
                stores[i] = ci::scalar_store(
                    value, value_length,
                    cetype_ext_t::CE_ASCII_OR_UTF8
                );
            }
            else {
                output.reset(noccurrences);
                for (R_len_t j=0; j<noccurrences; ++j) {
                    const std::pair<R_len_t, R_len_t>& occurrence =
                        occurrences[j];
                    ci::builder_set(
                        output, j, str_cur_s+occurrence.first,
                        occurrence.second-occurrence.first,
                        cetype_ext_t::CE_ASCII_OR_UTF8
                    );
                }
                stores[i] = output.release_store();
            }
        }
    }

    if (simplify1 != NA_LOGICAL && !simplify1) {
        STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
            return Rf_allocVector(VECSXP, vectorize_length);
        }));
        for (R_len_t i=0; i<vectorize_length; ++i) {
            SEXP ans;
            STRI__PROTECT(ans = charport::charvec::wrap(
                std::move(stores[i])
            ));
            SET_VECTOR_ELT(ret, i, ans);
            STRI__UNPROTECT(1);
        }
    }
    else {
        R_len_t n_min = 0;
        charport::unwind_protect([&]() -> SEXP {
            R_len_t n_length = LENGTH(n);
            const int* n_tab = INTEGER_RO(n);
            for (R_len_t i=0; i<n_length; ++i) {
                if (n_tab[i] != NA_INTEGER && n_min < n_tab[i])
                    n_min = n_tab[i];
            }
            return R_NilValue;
        });

        R_len_t matrix_ncol = n_min;
        for (R_len_t i=0; i<vectorize_length; ++i) {
            R_len_t current_size = ci::checked_r_len(
                static_cast<R_xlen_t>(stores[i].size()),
                "split results"
            );
            if (matrix_ncol < current_size)
                matrix_ncol = current_size;
        }

        // Deviation from stringi: reject a matrix that cannot be represented
        // before passing an overflowed product to the flat Builder.
        if (vectorize_length > 0 &&
                matrix_ncol > R_XLEN_T_MAX/vectorize_length)
            throw length_error("matrix length exceeds R's vector limit");
        R_xlen_t matrix_size =
            static_cast<R_xlen_t>(vectorize_length) * matrix_ncol;
        charport::charvec::Builder matrix(matrix_size);
        for (R_len_t i=0; i<vectorize_length; ++i) {
            R_len_t current_size = static_cast<R_len_t>(stores[i].size());
            R_len_t j = 0;
            for (; j<current_size; ++j) {
                matrix.set(
                    i+static_cast<R_xlen_t>(j)*vectorize_length,
                    stores[i].view(static_cast<size_t>(j))
                );
            }
            for (; j<matrix_ncol; ++j) {
                R_xlen_t output_i =
                    i+static_cast<R_xlen_t>(j)*vectorize_length;
                if (simplify1 == NA_LOGICAL)
                    matrix.set_na(output_i);
                else
                    ci::builder_set(
                        matrix, output_i, "", 0,
                        cetype_ext_t::CE_ASCII
                    );
            }
        }

        STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
            return matrix.to_sexp();
        }));
        ret = charport::unwind_protect([&]() -> SEXP {
            SEXP dim;
            PROTECT(dim = Rf_allocVector(INTSXP, 2));
            INTEGER(dim)[0] = vectorize_length;
            INTEGER(dim)[1] = matrix_ncol;
            SEXP result = Rf_setAttrib(ret, R_DimSymbol, dim);
            UNPROTECT(1);
            return result;
        });
    }
    }
    STRI__DEFERRED_WARNINGS.emit();

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END({ /* no action */ })
}
