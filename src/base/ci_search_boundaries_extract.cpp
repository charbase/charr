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
#include "ci_container_integer.h"
#include "ci_brkiter.h"
#include "utf8_input.h"

#include <cstdint>
#include <cstring>
#include <exception>
#include <vector>


namespace charr { namespace base {

namespace {

bool boundary_ascii_word_locale(const char* locale) noexcept
{
    if (!locale)
        locale = uloc_getDefault();
    if (!locale)
        return false;
    if (ci__is_C_locale(locale))
        return true;
    if (locale[0] == '\0')
        return true;
    if (locale[0] == 'r' && locale[1] == 'o' && locale[2] == 'o' &&
            locale[3] == 't' && locale[4] == '\0') {
        return true;
    }
    if (locale[0] != 'e' || locale[1] != 'n')
        return false;
    if (locale[2] != '\0' && locale[2] != '_' && locale[2] != '-')
        return false;
    for (const char* current = locale + 2; *current; ++current) {
        if (*current == '@')
            return false;
    }
    return true;
}

class BoundaryExtractOptions : public StriBrkIterOptions {
public:
    BoundaryExtractOptions(SEXP options)
        : StriBrkIterOptions(options, "line_break") {}

    bool ascii_word_first() const noexcept
    {
        return type == UBRK_WORD && rules.isEmpty() && skip_size == 0 &&
            boundary_ascii_word_locale(locale);
    }
};

bool boundary_ascii_initial_word(
    const char* value, R_len_t length, R_len_t& end
) noexcept
{
    // This is deliberately narrower than ICU word iteration. A plain ASCII
    // letter run followed by ASCII whitespace (or the end) has the same first
    // boundary in the root and English rules; everything else stays on ICU.
    if (length <= 0)
        return false;
    R_len_t position = 0;
    while (position < length) {
        const unsigned char byte = static_cast<unsigned char>(value[position]);
        if (!((byte >= 'A' && byte <= 'Z') ||
                (byte >= 'a' && byte <= 'z'))) {
            break;
        }
        ++position;
    }
    if (position == 0)
        return false;
    if (position < length) {
        const unsigned char byte = static_cast<unsigned char>(value[position]);
        if (byte != ' ' && (byte < '\t' || byte > '\r'))
            return false;
    }
    end = position;
    return true;
}

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

    void first()
    {
        position_ = iterator_->first();
    }

    void last()
    {
        iterator_->first();
        position_ = iterator_->last();
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

    bool previous(std::pair<R_len_t, R_len_t>& occurrence)
    {
        do {
            if (!skip()) {
                occurrence.second = position_;
                position_ = iterator_->previous();
                if (position_ == BreakIterator::DONE)
                    return false;
                occurrence.first = position_;
                return true;
            }
            position_ = iterator_->previous();
        }
        while (position_ != BreakIterator::DONE);
        return false;
    }
};

}

/**
 * Extract first or last text between boundaries
 *
 * @param str character vector
 * @param opts_brkiter list
 * @param first looking for first or last match?
 * @return character vector
 *
 * @version 0.5-1 (Marek Gagolewski, 2014-12-19)
 */
template<bool First>
SEXP ci__extract_firstlast_boundaries(SEXP str, SEXP opts_brkiter)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    BoundaryExtractOptions opts_brkiter2(opts_brkiter);

    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP ret;
    try {
        const R_len_t str_length = LENGTH(str);
        STRI__PROTECT(ret = Rf_allocVector(STRSXP, str_length));
        Utf8Input input(str, str_length);
        bool default_locale_warning = false;
        {
            BoundaryIterator brkiter(opts_brkiter2);
            const bool ascii_word_first =
                First && opts_brkiter2.ascii_word_first();

            for (R_len_t i = 0; i < str_length; ++i) {
                const Utf8Record value = input.record(i);
                if (value.is_na()) {
                    SET_STRING_ELT(ret, i, NA_STRING);
                    continue;
                }
                if (value.len == 0) {
                    SET_STRING_ELT(ret, i, NA_STRING);
                    continue;
                }
                R_len_t ascii_word_end;
                if (ascii_word_first && boundary_ascii_initial_word(
                        value.ptr, value.len, ascii_word_end)) {
                    SET_STRING_ELT(
                        ret, i,
                        Rf_mkCharLenCE(value.ptr, ascii_word_end, CE_UTF8)
                    );
                    continue;
                }
                brkiter.set_text(value.ptr, value.len);
                pair<R_len_t, R_len_t> occurrence;
                bool matched;
                if constexpr (First) {
                    brkiter.first();
                    matched = brkiter.next(occurrence);
                }
                else {
                    brkiter.last();
                    matched = brkiter.previous(occurrence);
                }
                if (!matched) {
                    SET_STRING_ELT(ret, i, NA_STRING);
                    continue;
                }

                SET_STRING_ELT(
                    ret, i,
                    Rf_mkCharLenCE(
                        value.ptr + occurrence.first,
                        occurrence.second - occurrence.first, CE_UTF8
                    )
                );
            }
            default_locale_warning = brkiter.used_default_locale();
        }
        if (default_locale_warning)
            r_warning("%s", ICUError::getICUerrorName(
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
    STRI__ERROR_HANDLER_END( ;/* do nothing special on error */ )
}


/**
 * Extract first  text between boundaries
 *
 * @param str character vector
 * @param opts_brkiter list
 * @return character vector
 *
 * @version 0.5-1 (Marek Gagolewski, 2014-12-19)
 */
SEXP ci_extract_first_boundaries(SEXP str, SEXP opts_brkiter)
{
    return ci__extract_firstlast_boundaries<true>(str, opts_brkiter);
}


/**
 * Extract last  text between boundaries
 *
 * @param str character vector
 * @param opts_brkiter list
 * @return character vector
 *
 * @version 0.5-1 (Marek Gagolewski, 2014-12-19)
 */
SEXP ci_extract_last_boundaries(SEXP str, SEXP opts_brkiter)
{
    return ci__extract_firstlast_boundaries<false>(str, opts_brkiter);
}


/** Extract all  text between boundaries
 *
 * @param str character vector
 * @param simplify logical
 * @param omit_no_match logical
 * @param opts_brkiter named list
 * @return list or matrix
 *
 * @version 0.5-1 (Marek Gagolewski, 2014-12-19)
 */
SEXP ci_extract_all_boundaries(SEXP str, SEXP simplify, SEXP omit_no_match, SEXP opts_brkiter)
{
    bool omit_no_match1 = ci__prepare_arg_logical_1_notNA(omit_no_match, "omit_no_match");
    PROTECT(simplify = ci__prepare_arg_logical_1(simplify, "simplify"));
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    StriBrkIterOptions opts_brkiter2(opts_brkiter, "line_break");

    STRI__ERROR_HANDLER_BEGIN(2)
    SEXP ret;
    try {
        const R_len_t str_length = LENGTH(str);
        STRI__PROTECT(ret = Rf_allocVector(VECSXP, str_length));
        Utf8Input input(str, str_length);
        bool default_locale_warning = false;
        {
            BoundaryIterator brkiter(opts_brkiter2);
            std::vector<pair<R_len_t, R_len_t>> occurrences;

            for (R_len_t i = 0; i < str_length; ++i) {
                const Utf8Record value = input.record(i);
                if (value.is_na()) {
                    SET_VECTOR_ELT(ret, i, ci__vector_NA_strings(1));
                    continue;
                }
                brkiter.set_text(value.ptr, value.len);
                brkiter.first();
                occurrences.clear();
                pair<R_len_t, R_len_t> occurrence;
                while (brkiter.next(occurrence))
                    occurrences.push_back(occurrence);

                const R_len_t occurrence_count =
                    static_cast<R_len_t>(occurrences.size());
                if (occurrence_count == 0) {
                    SET_VECTOR_ELT(
                        ret, i,
                        ci__vector_NA_strings(omit_no_match1 ? 0 : 1)
                    );
                    continue;
                }

                SEXP current;
                STRI__PROTECT(current = Rf_allocVector(
                    STRSXP, occurrence_count
                ));
                for (R_len_t j = 0; j < occurrence_count; ++j) {
                    const pair<R_len_t, R_len_t>& item =
                        occurrences[static_cast<std::size_t>(j)];
                    SET_STRING_ELT(
                        current, j,
                        Rf_mkCharLenCE(
                            value.ptr + item.first,
                            item.second - item.first, CE_UTF8
                        )
                    );
                }
                SET_VECTOR_ELT(ret, i, current);
                STRI__UNPROTECT(1);
            }
            default_locale_warning = brkiter.used_default_locale();
        }
        if (default_locale_warning)
            r_warning("%s", ICUError::getICUerrorName(
                U_USING_DEFAULT_WARNING
            ));

        if (LOGICAL(simplify)[0] == NA_LOGICAL || LOGICAL(simplify)[0]) {
            SEXP robj_TRUE, robj_zero, robj_na_strings, robj_empty_strings;
            STRI__PROTECT(robj_TRUE = Rf_ScalarLogical(TRUE));
            STRI__PROTECT(robj_zero = Rf_ScalarInteger(0));
            STRI__PROTECT(robj_na_strings = ci__vector_NA_strings(1));
            STRI__PROTECT(robj_empty_strings = ci__vector_empty_strings(1));
            STRI__PROTECT(ret = ci_list2matrix(
                ret, robj_TRUE,
                (LOGICAL(simplify)[0] == NA_LOGICAL)
                    ? robj_na_strings
                    : robj_empty_strings,
                robj_zero
            ));
        }
    }
    catch (const StriException&) {
        throw;
    }
    catch (const std::exception& error) {
        throw StriException("%s", error.what());
    }

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END({/* no-op */})
}

} } // namespace charr::base
