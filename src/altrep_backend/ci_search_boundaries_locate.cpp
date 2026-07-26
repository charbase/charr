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
#include "ci_reader.h"
#include "../altrep/utf8_input.h"

#include <cstring>
#include <utility>
#include <vector>


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

class BoundaryLocateOptions : public StriBrkIterOptions {
public:
    template <std::size_t N>
    BoundaryLocateOptions(
        SEXP options, const char (&default_type)[N],
        ci::DeferredWarnings& warnings
    ) : StriBrkIterOptions(options, default_type, warnings)
    {
        locale_state_ = locale_option_state(options, locale);
    }

    bool can_use_first_ascii_word() const noexcept
    {
        if (type != UBRK_WORD || !rules.isEmpty() || skip_size != 0 ||
                (locale && std::strchr(locale, '@'))) {
            return false;
        }

        if (!locale && locale_state_.reject_fast)
            return false;
        const char* locale_id = locale ? locale : uloc_getDefault();
        if (!locale_id)
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

    bool warn_default_locale() const noexcept
    {
        return locale_state_.warn_default;
    }

private:
    LocaleOptionState locale_state_;
};

struct BoundaryLocateElement {
    bool argument_na = true;
    std::size_t offset = 0;
    R_len_t count = 0;
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
    SEXP ret;
    {
    // Deviation from stringi: keep the option's ICU storage inside the
    // unwind-safe scope so it is released before warning replay.
    BoundaryLocateOptions opts_brkiter2(
        opts_brkiter, "line_break", STRI__DEFERRED_WARNINGS
    );
    const bool use_first_ascii_word = first &&
        opts_brkiter2.can_use_first_ascii_word();
    bool warn_default_locale = opts_brkiter2.warn_default_locale();
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    R_len_t str_length = ci::checked_r_len(
        context.size(str), "character vectors"
    );

    STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
        return Rf_allocMatrix(INTSXP, str_length, 2);
    }));
    int* ret_tab = INTEGER(ret);

    auto process = [&](auto record) {
        StriRuleBasedBreakIterator brkiter(opts_brkiter2);
        for (R_len_t i = 0; i < str_length; ++i)
        {
            ret_tab[i]            = NA_INTEGER;
            ret_tab[i+str_length] = NA_INTEGER;

            charport::StrView value = record(i);
            if (value.is_na())
                continue;
            if (value.enc == cetype_ext_t::CE_BYTES)
                throw StriException(MSG__BYTESENC);

            if (get_length1) {
                ret_tab[i]            = -1;
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

            if (warn_default_locale) {
                STRI__DEFERRED_WARNINGS.push(
                    ICUError::getICUerrorName(U_USING_DEFAULT_WARNING)
                );
                warn_default_locale = false;
            }
            brkiter.setupMatcher(
                value.ptr, value.len,
                STRI__DEFERRED_WARNINGS
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
                value.enc == cetype_ext_t::CE_ASCII
            );
            const R_len_t start = cursor.at(occurrence.first) + 1;
            const R_len_t end = cursor.at(occurrence.second);
            ret_tab[i] = start;
            ret_tab[i+str_length] = get_length1
                ? end-start+1 : end;
        }
    };

    {
        charr::altrep::Utf8Input input(context, str, str_length);
        process([&](R_len_t i) { return input.record(i).view(); });
    }

    charport::unwind_protect([&]() -> SEXP {
        ci__locate_set_dimnames_matrix(ret, get_length1);
        return R_NilValue;
    });
    }
    STRI__DEFERRED_WARNINGS.emit();

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

    std::vector<BoundaryLocateElement> elements(
        static_cast<std::size_t>(str_length)
    );
    std::vector< std::pair<R_len_t, R_len_t> > occurrences;
    occurrences.reserve(static_cast<std::size_t>(str_length));

    auto process = [&](auto record) {
        StriRuleBasedBreakIterator brkiter(opts_brkiter2);
        for (R_len_t i = 0; i < str_length; ++i)
        {
            BoundaryLocateElement& element = elements[
                static_cast<std::size_t>(i)
            ];
            element.offset = occurrences.size();

            charport::StrView value = record(i);
            if (value.is_na())
                continue;
            if (value.enc == cetype_ext_t::CE_BYTES)
                throw StriException(MSG__BYTESENC);
            element.argument_na = false;

            brkiter.setupMatcher(
                value.ptr, value.len,
                STRI__DEFERRED_WARNINGS
            );
            brkiter.first();

            Utf8PositionCursor cursor(
                value.ptr, value.len,
                value.enc == cetype_ext_t::CE_ASCII
            );
            pair<R_len_t, R_len_t> occurrence;
            while (brkiter.next(occurrence)) {
                const R_len_t start = cursor.at(occurrence.first) + 1;
                const R_len_t end = cursor.at(occurrence.second);
                occurrences.push_back(std::make_pair(
                    start, get_length1 ? end-start+1 : end
                ));
            }
            element.count = static_cast<R_len_t>(
                occurrences.size()-element.offset
            );
        }
    };

    {
        charr::altrep::Utf8Input input(context, str, str_length);
        process([&](R_len_t i) { return input.record(i).view(); });
    }

    STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
        return Rf_allocVector(VECSXP, str_length);
    }));

    charport::unwind_protect([&]() -> SEXP {
        for (R_len_t i = 0; i < str_length; ++i)
        {
            ci::UnwindCallbackProtector protector;
            const BoundaryLocateElement& element = elements[
                static_cast<std::size_t>(i)
            ];
            SEXP ans;
            if (element.argument_na) {
                ans = protector.hold(ci__matrix_NA_INTEGER(1, 2));
            }
            else if (element.count <= 0) {
                ans = protector.hold(ci__matrix_NA_INTEGER(
                    omit_no_match1?0:1, 2,
                    get_length1?-1:NA_INTEGER
                ));
            }
            else {
                ans = protector.hold(Rf_allocMatrix(
                    INTSXP, element.count, 2
                ));
                int* ans_tab = INTEGER(ans);
                for (R_len_t j = 0; j < element.count; ++j) {
                    const std::pair<R_len_t, R_len_t>& occurrence =
                        occurrences[element.offset+
                            static_cast<std::size_t>(j)];
                    ans_tab[j] = occurrence.first;
                    ans_tab[j+element.count] = occurrence.second;
                }
            }
            SET_VECTOR_ELT(ret, i, ans);
        }

        ci__locate_set_dimnames_list(ret, get_length1);
        return R_NilValue;
    });
    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END({ /* nothing special t.b.d. on error */ })
}
