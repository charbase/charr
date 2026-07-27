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
#include "native_to_utf8.h"
#include "utf8_input.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <vector>


namespace charr { namespace base {

namespace {

class Utf8PositionCursor {
public:
    Utf8PositionCursor(
        const char* data, R_len_t length, bool ascii
    ) noexcept
        : data_(data), length_(length), byte_(0), position_(0),
          ascii_(ascii)
    {
    }

    R_len_t at(R_len_t target) noexcept
    {
        if (ascii_)
            return target;
        while (byte_ < target) {
            U8_FWD_1(data_, byte_, length_);
            ++position_;
        }
        return position_;
    }

private:
    const char* data_;
    R_len_t length_;
    R_len_t byte_;
    R_len_t position_;
    bool ascii_;
};

bool first_ascii_word(
    const char* data, R_len_t length, R_len_t& word_end
) noexcept
{
    // This is deliberately narrower than ICU word iteration. A plain ASCII
    // letter run followed by ASCII whitespace (or the end) has the same first
    // boundary in the root and English rules; everything else stays on ICU.
    if (length <= 0)
        return false;

    const auto is_letter = [](unsigned char value) {
        return (value >= 'A' && value <= 'Z') ||
            (value >= 'a' && value <= 'z');
    };
    const auto is_whitespace = [](unsigned char value) {
        return value == ' ' || (value >= '\t' && value <= '\r');
    };

    if (!is_letter(static_cast<unsigned char>(data[0])))
        return false;

    R_len_t i = 1;
    while (i < length && is_letter(static_cast<unsigned char>(data[i])))
        ++i;
    if (i < length && !is_whitespace(static_cast<unsigned char>(data[i])))
        return false;

    word_end = i;
    return true;
}

struct LocaleOptionState {
    bool reject_fast = false;
    bool warn_default = false;
};

// ICU canonicalizes root and a few malformed locale IDs to the same null
// pointer. Keep just enough of the raw option to distinguish an intentional
// root/default request from a fallback that must still warn.
LocaleOptionState locale_option_state(SEXP options, const char* locale)
{
    LocaleOptionState state;
    if (locale || !Rf_isVectorList(options))
        return state;

    SEXP names = Rf_getAttrib(options, R_NamesSymbol);
    for (R_len_t i = 0; i < LENGTH(options); ++i) {
        if (std::strcmp(CHAR(STRING_ELT(names, i)), "locale"))
            continue;

        SEXP value = VECTOR_ELT(options, i);
        if (Rf_isNull(value))
            return state;
        state.reject_fast = true;
        if (TYPEOF(value) != STRSXP || XLENGTH(value) <= 0 ||
                STRING_ELT(value, 0) == NA_STRING) {
            return state;
        }

        const char* begin = CHAR(STRING_ELT(value, 0));
        const char* end = begin+std::strlen(begin);
        while (begin < end && (*begin == ' ' || *begin == '\t' ||
                *begin == '\n' || *begin == '\r')) {
            ++begin;
        }
        while (begin < end && (end[-1] == ' ' || end[-1] == '\t' ||
                end[-1] == '\n' || end[-1] == '\r')) {
            --end;
        }
        const bool is_root = end-begin == 4 &&
            (begin[0] == 'r' || begin[0] == 'R') &&
            (begin[1] == 'o' || begin[1] == 'O') &&
            (begin[2] == 'o' || begin[2] == 'O') &&
            (begin[3] == 't' || begin[3] == 'T');
        if (begin == end || is_root)
            state.reject_fast = false;
        else
            state.warn_default = true;
        return state;
    }
    return state;
}

class BoundaryLocateIterator : public StriBrkIterOptions {
public:
    BoundaryLocateIterator(SEXP options, const char* default_type)
        : StriBrkIterOptions(options, default_type), text_(nullptr),
          position_(BreakIterator::DONE)
    {
        locale_state_ = locale_option_state(options, locale);
    }

    ~BoundaryLocateIterator()
    {
        iterator_.reset();
        if (text_)
            utext_close(text_);
    }

    bool can_use_first_ascii_word() const noexcept
    {
        return type == UBRK_WORD && rules.isEmpty() && skip_size == 0 &&
            has_direct_locale();
    }

    void setup_matcher(
        const char* data, R_len_t length, UErrorCode& locale_warning
    )
    {
        if (!iterator_)
            open(locale_warning);

        UErrorCode status = U_ZERO_ERROR;
        text_ = utext_openUTF8(text_, data, length, &status);
        STRI__CHECKICUSTATUS_THROW(status, {})

        status = U_ZERO_ERROR;
        iterator_->setText(text_, status);
        STRI__CHECKICUSTATUS_THROW(status, {})
        position_ = BreakIterator::DONE;
    }

    void first()
    {
        position_ = iterator_->first();
    }

    bool next(std::pair<R_len_t, R_len_t>& boundary)
    {
        R_len_t previous = position_;
        while ((position_ = iterator_->next()) != BreakIterator::DONE) {
            if (!ignore_boundary()) {
                boundary.first = previous;
                boundary.second = position_;
                return true;
            }
            previous = position_;
        }
        return false;
    }

    void last()
    {
        iterator_->first();
        position_ = iterator_->last();
    }

    bool previous(std::pair<R_len_t, R_len_t>& boundary)
    {
        do {
            if (!ignore_boundary()) {
                boundary.second = position_;
                position_ = iterator_->previous();
                if (position_ == BreakIterator::DONE)
                    return false;
                boundary.first = position_;
                return true;
            }
            position_ = iterator_->previous();
        }
        while (position_ != BreakIterator::DONE);
        return false;
    }

private:
    void open(UErrorCode& locale_warning)
    {
        UErrorCode status = U_ZERO_ERROR;
        if (!rules.isEmpty()) {
            UParseError parse_error;
            iterator_.reset(new RuleBasedBreakIterator(
                UnicodeString(rules), parse_error, status
            ));
        }
        else {
            Locale locale_value = Locale::createFromName(locale);
            switch (type) {
            case UBRK_CHARACTER:
                iterator_.reset(BreakIterator::createCharacterInstance(
                    locale_value, status
                ));
                break;
            case UBRK_LINE:
                iterator_.reset(BreakIterator::createLineInstance(
                    locale_value, status
                ));
                break;
            case UBRK_SENTENCE:
                iterator_.reset(BreakIterator::createSentenceInstance(
                    locale_value, status
                ));
                break;
            case UBRK_WORD:
                iterator_.reset(BreakIterator::createWordInstance(
                    locale_value, status
                ));
                break;
            default:
                throw StriException(MSG__INTERNAL_ERROR);
            }
        }
        STRI__CHECKICUSTATUS_THROW(status, { iterator_.reset(); })

        UErrorCode locale_status = U_ZERO_ERROR;
        const char* valid_locale = iterator_->getLocaleID(
            ULOC_VALID_LOCALE, locale_status
        );
        if (status == U_USING_DEFAULT_WARNING && locale && valid_locale &&
                !std::strcmp(valid_locale, "root")) {
            locale_warning = status;
        }
        if (locale_state_.warn_default) {
            locale_warning = U_USING_DEFAULT_WARNING;
            locale_state_.warn_default = false;
        }
    }

    bool ignore_boundary() const
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

    bool has_direct_locale() const noexcept
    {
        if (!locale && locale_state_.reject_fast)
            return false;
        const char* locale_id = locale ? locale : uloc_getDefault();
        if (!locale_id || std::strchr(locale_id, '@'))
            return false;
        if (ci__is_C_locale(locale_id))
            locale_id = "en_US_POSIX";
        if (!std::strcmp(locale_id, "root"))
            return true;

        char language[ULOC_LANG_CAPACITY];
        UErrorCode status = U_ZERO_ERROR;
        const int32_t size = uloc_getLanguage(
            locale_id, language, ULOC_LANG_CAPACITY, &status
        );
        return U_SUCCESS(status) && size > 0 &&
            !std::strcmp(language, "en");
    }

    std::unique_ptr<BreakIterator> iterator_;
    UText* text_;
    R_len_t position_;
    LocaleOptionState locale_state_;
};

}

/**
 * Locate first or last boundaries
 *
 * @param str character vector
 * @param opts_brkiter list
 * @param first looking for first or last match?
 *
 * @return integer matrix (2 columns)
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-05)
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29)
 *     get_length
 */
SEXP ci__locate_firstlast_boundaries(
    SEXP str, SEXP opts_brkiter, bool first, bool get_length1
) {
    PROTECT(str = ci__prepare_arg_string(str, "str"));

    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP ret = R_NilValue;
    UErrorCode locale_warning = U_ZERO_ERROR;
    char staged_error[StriException_BUFSIZE] = {0};
    try {
        BoundaryLocateIterator brkiter(opts_brkiter, "line_break");
        const bool use_first_ascii_word = first &&
            brkiter.can_use_first_ascii_word();
        const R_len_t str_length = LENGTH(str);
        STRI__PROTECT(ret = Rf_allocMatrix(INTSXP, str_length, 2));
        int* ret_tab = INTEGER(ret);

        auto process = [&](auto record) {
            for (R_len_t i = 0; i < str_length; ++i)
            {
                ret_tab[i] = NA_INTEGER;
                ret_tab[i+str_length] = NA_INTEGER;

                Utf8Record value = record(i);
                if (value.is_na())
                    continue;

                if (get_length1) {
                    ret_tab[i] = -1;
                    ret_tab[i+str_length] = -1;
                }

                if (value.len == 0)
                    continue;

                R_len_t word_end = 0;
                if (use_first_ascii_word && first_ascii_word(
                        value.ptr, value.len, word_end
                )) {
                    ret_tab[i] = 1;
                    ret_tab[i+str_length] = word_end;
                    continue;
                }

                brkiter.setup_matcher(
                    value.ptr, value.len, locale_warning
                );
                pair<R_len_t, R_len_t> occurrence;
                bool matched;
                if (first) {
                    brkiter.first();
                    matched = brkiter.next(occurrence);
                }
                else {
                    brkiter.last();
                    matched = brkiter.previous(occurrence);
                }
                if (!matched)
                    continue;

                Utf8PositionCursor cursor(
                    value.ptr, value.len,
                    value.state == Utf8RecordState::ascii
                );
                const R_len_t start = cursor.at(occurrence.first) + 1;
                const R_len_t end = cursor.at(occurrence.second);
                ret_tab[i] = start;
                ret_tab[i+str_length] = get_length1
                    ? end-start+1 : end;
            }
        };

        Utf8Input input(str, str_length);
        process([&](R_len_t i) {
            return input.record(i);
        });
    }
    catch (const StriException& error) {
        std::snprintf(
            staged_error, sizeof(staged_error), "%s", error.getMessage()
        );
    }
    catch (const std::exception& error) {
        std::snprintf(
            staged_error, sizeof(staged_error), "%s", error.what()
        );
    }

    if (locale_warning != U_ZERO_ERROR)
        r_warning("%s", ICUError::getICUerrorName(locale_warning));
    if (staged_error[0])
        throw StriException("%s", staged_error);

    ci__locate_set_dimnames_matrix(ret, get_length1);

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END( ;/* do nothing special on error */ )
}


/**
 * Locate first boundary
 *
 * @param str character vector
 * @param opts_brkiter list
 * @return integer matrix (2 columns)
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-05)
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29)
 *     get_length
 */
SEXP ci_locate_first_boundaries(SEXP str, SEXP opts_brkiter, SEXP get_length)
{
    bool get_length1 = ci__prepare_arg_logical_1_notNA(get_length, "get_length");
    return ci__locate_firstlast_boundaries(str, opts_brkiter, true, get_length1);
}


/**
 * Locate last boundary
 *
 * @param str character vector
 * @param opts_brkiter list
 * @return integer matrix (2 columns)
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-05)
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29)
 *     get_length
 */
SEXP ci_locate_last_boundaries(SEXP str, SEXP opts_brkiter, SEXP get_length)
{
    bool get_length1 = ci__prepare_arg_logical_1_notNA(get_length, "get_length");
    return ci__locate_firstlast_boundaries(str, opts_brkiter, false, get_length1);
}


/** Locate all BreakIterator boundaries
 *
 * @param str character vector
 * @param omit_no_match logical
 * @param opts_brkiter named list
 * @return list
 *
 * @version 0.2-2 (Marek Gagolewski, 2014-04-22)
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
 *          new args: omit_no_match
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-02)
 *          use StriRuleBasedBreakIterator
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-29)
 *     get_length
 */
SEXP ci_locate_all_boundaries(SEXP str, SEXP omit_no_match, SEXP opts_brkiter, SEXP get_length)
{
    bool omit_no_match1 = ci__prepare_arg_logical_1_notNA(omit_no_match, "omit_no_match");
    bool get_length1 = ci__prepare_arg_logical_1_notNA(get_length, "get_length");
    PROTECT(str = ci__prepare_arg_string(str, "str"));
    StriBrkIterOptions opts_brkiter2(opts_brkiter, "line_break");

    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP ret;
    try {
        const R_len_t str_length = LENGTH(str);
        std::vector< pair<R_len_t, R_len_t> > occurrences;

        STRI__PROTECT(ret = Rf_allocVector(VECSXP, str_length));

        auto process = [&](auto record) {
            StriRuleBasedBreakIterator brkiter(opts_brkiter2);
            for (R_len_t i = 0; i < str_length; ++i)
            {
                occurrences.clear();
                Utf8Record value = record(i);
                if (value.is_na()) {
                    SET_VECTOR_ELT(ret, i, ci__matrix_NA_INTEGER(1, 2));
                    continue;
                }

                brkiter.setupMatcher(value.ptr, value.len);
                brkiter.first();

                Utf8PositionCursor cursor(
                    value.ptr, value.len,
                    value.state == Utf8RecordState::ascii
                );
                pair<R_len_t, R_len_t> occurrence;
                while (brkiter.next(occurrence)) {
                    const R_len_t start = cursor.at(occurrence.first) + 1;
                    const R_len_t end = cursor.at(occurrence.second);
                    occurrences.push_back(std::make_pair(
                        start, get_length1 ? end-start+1 : end
                    ));
                }

                const R_len_t noccurrences = static_cast<R_len_t>(
                    occurrences.size()
                );
                if (noccurrences <= 0) {
                    SET_VECTOR_ELT(
                        ret, i,
                        ci__matrix_NA_INTEGER(
                            omit_no_match1?0:1, 2,
                            get_length1?-1:NA_INTEGER
                        )
                    );
                    continue;
                }

                SEXP ans;
                STRI__PROTECT(ans = Rf_allocMatrix(
                    INTSXP, noccurrences, 2
                ));
                int* ans_tab = INTEGER(ans);
                for (R_len_t j = 0; j < noccurrences; ++j) {
                    ans_tab[j] =
                        occurrences[static_cast<std::size_t>(j)].first;
                    ans_tab[j+noccurrences] =
                        occurrences[static_cast<std::size_t>(j)].second;
                }

                SET_VECTOR_ELT(ret, i, ans);
                STRI__UNPROTECT(1);
            }
        };

        Utf8Input input(str, str_length);
        process([&](R_len_t i) {
            return input.record(i);
        });

        ci__locate_set_dimnames_list(ret, get_length1);
    }
    catch (const StriException&) {
        throw;
    }
    catch (const std::exception& error) {
        throw StriException("%s", error.what());
    }
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END({ /* nothing special t.b.d. on error */ })
}

} } // namespace charr::base
