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
#include "ci_container_integer.h"
#include "ci_brkiter.h"
#include "ci_reader.h"
#include "../altrep/utf8_input.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>


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
    BoundaryExtractOptions(
        SEXP options, ci::DeferredWarnings& warnings
    ) : StriBrkIterOptions(options, "line_break", warnings) {}

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


cetype_ext_t boundary_output_encoding(
    const char* value, std::size_t length, cetype_ext_t source_encoding
)
{
    // Reader marks are authoritative except for the deliberately ambiguous
    // ASCII-or-UTF-8 mark, which is the only case that needs a payload scan.
    if (source_encoding == cetype_ext_t::CE_ASCII)
        return cetype_ext_t::CE_ASCII;
    if (source_encoding == cetype_ext_t::CE_ASCII_OR_UTF8) {
        return ci::output_encoding(
            value, length, cetype_ext_t::CE_ASCII_OR_UTF8
        );
    }
    return cetype_ext_t::CE_UTF8;
}


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


void add_payload_length(std::size_t& total, R_len_t length)
{
    const std::size_t amount = static_cast<std::size_t>(length);
    if (amount > std::numeric_limits<std::size_t>::max() - total)
        throw std::length_error("character output payload is too large");
    total += amount;
}


charport::charvec::Store boundary_store(
    const charport::StrView& value,
    const std::vector<pair<R_len_t, R_len_t>>& occurrences
)
{
    std::size_t payload_length = 0;
    for (const pair<R_len_t, R_len_t>& occurrence : occurrences)
        add_payload_length(
            payload_length, occurrence.second - occurrence.first
        );

    charport::charvec::Store output(occurrences.size(), payload_length);
    char* destination = payload_length == 0
        ? nullptr
        : output.slices.front_data();
    for (std::size_t i = 0; i < occurrences.size(); ++i) {
        const pair<R_len_t, R_len_t>& occurrence = occurrences[i];
        const R_len_t length = occurrence.second - occurrence.first;
        const char* source = value.ptr + occurrence.first;
        const cetype_ext_t encoding = boundary_output_encoding(
            source, static_cast<std::size_t>(length), value.enc
        );
        if (length == 0) {
            output.records.set(
                i, charport::charvec::components::empty_data(), 0,
                encoding
            );
            continue;
        }
        std::memcpy(
            destination, source, static_cast<std::size_t>(length)
        );
        output.records.set(i, destination, length, encoding);
        destination += length;
    }
    return output;
}

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

    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP ret;
    {
    // Deviation from stringi: keep option ICU storage and the lazy Builder
    // inside the unwind-safe scope so both die before warning replay.
    BoundaryExtractOptions opts_brkiter2(
        opts_brkiter, STRI__DEFERRED_WARNINGS
    );
    R_len_t str_length = 0;
    charport::charvec::Store output = [&]() {
        ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
        str_length = ci::checked_r_len(
            context.size(str), "character vectors"
        );
        charr::altrep::Utf8Input input(context, str, str_length);
        charport::charvec::Builder builder(str_length);
        BoundaryIterator brkiter(opts_brkiter2);
        const bool ascii_word_first =
            First && opts_brkiter2.ascii_word_first();

        for (R_len_t i = 0; i < str_length; ++i) {
            charport::StrView value = input.record(i).view();
            if (value.is_na())
                continue;
            if (value.len == 0)
                continue;
            R_len_t ascii_word_end;
            if (ascii_word_first && boundary_ascii_initial_word(
                    value.ptr, value.len, ascii_word_end)) {
                builder.set(
                    i, value.ptr, static_cast<std::size_t>(ascii_word_end),
                    cetype_ext_t::CE_ASCII
                );
                continue;
            }
            brkiter.set_text(
                value.ptr, value.len,
                STRI__DEFERRED_WARNINGS
            );
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
            if (!matched)
                continue;
            const char* output = value.ptr + occurrence.first;
            const std::size_t output_length = static_cast<std::size_t>(
                occurrence.second - occurrence.first
            );
            builder.set(
                i, output, output_length,
                boundary_output_encoding(
                    output, output_length, value.enc
                )
            );
        }
        return builder.release_store();
    }();

    STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
        return charport::charvec::wrap(std::move(output));
    }));
    }
    STRI__DEFERRED_WARNINGS.emit();
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
    int simplify1 = NA_LOGICAL;

    STRI__ERROR_HANDLER_BEGIN(2)
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
    R_len_t str_length = ci::checked_r_len(
        context.size(str), "character vectors"
    );
    vector<charport::charvec::Store> stores;
    stores.reserve(static_cast<size_t>(str_length));
    for (R_len_t i=0; i<str_length; ++i)
        stores.push_back(charport::charvec::Store(0, 0));
    size_t max_columns = 0;
    {
        charr::altrep::Utf8Input input(context, str, str_length);
        BoundaryIterator brkiter(opts_brkiter2);
        std::vector<pair<R_len_t, R_len_t>> occurrences;

        for (R_len_t i = 0; i < str_length; ++i) {
            charport::charvec::Store& output = stores[
                static_cast<size_t>(i)
            ];
            charport::StrView value = input.record(i).view();
            if (value.is_na()) {
                output = charport::charvec::Store::scalar(
                    nullptr, 0, cetype_ext_t::CE_NA
                );
                max_columns = std::max(max_columns, output.size());
                continue;
            }
            brkiter.set_text(
                value.ptr, value.len,
                STRI__DEFERRED_WARNINGS
            );
            brkiter.first();

            occurrences.clear();
            pair<R_len_t, R_len_t> occurrence;
            while (brkiter.next(occurrence))
                occurrences.push_back(occurrence);

            if (occurrences.empty()) {
                if (!omit_no_match1)
                    output = charport::charvec::Store::scalar(
                        nullptr, 0, cetype_ext_t::CE_NA
                    );
                max_columns = std::max(max_columns, output.size());
                continue;
            }

            output = boundary_store(value, occurrences);
            max_columns = std::max(max_columns, output.size());
        }
    }

    if (simplify1 != NA_LOGICAL && !simplify1) {
        STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
            return Rf_allocVector(VECSXP, str_length);
        }));
        for (R_len_t i=0; i<str_length; ++i) {
            SEXP current;
            STRI__PROTECT(current = charport::charvec::wrap(
                std::move(stores[i])
            ));
            SET_VECTOR_ELT(ret, i, current);
            STRI__UNPROTECT(1);
        }
    }
    else {
        // Deviation from stringi: the direct Store-to-Builder matrix path
        // checks its dimensions before narrowing or multiplying them.
        if (max_columns > static_cast<size_t>(R_LEN_T_MAX))
            throw length_error("matrix columns exceed R's integer limit");
        const R_xlen_t rows = str_length;
        const R_xlen_t columns = static_cast<R_xlen_t>(max_columns);
        if (rows > 0 && columns > R_XLEN_T_MAX/rows)
            throw length_error("matrix length exceeds R's vector limit");

        charport::charvec::Builder matrix_builder(rows*columns);
        for (R_xlen_t i=0; i<rows; ++i) {
            const charport::charvec::Store& current = stores[i];
            const R_xlen_t current_size = static_cast<R_xlen_t>(
                current.size()
            );
            R_xlen_t j = 0;
            for (; j<current_size; ++j)
                matrix_builder.set(i+j*rows, current.view(j));
            for (; j<columns; ++j) {
                if (simplify1 == NA_LOGICAL) {
                    matrix_builder.set_na(i+j*rows);
                }
                else {
                    ci::builder_set(
                        matrix_builder, i+j*rows, "", 0,
                        cetype_ext_t::CE_ASCII
                    );
                }
            }
        }

        STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
            return matrix_builder.to_sexp();
        }));
        ret = charport::unwind_protect([&]() -> SEXP {
            SEXP dim;
            PROTECT(dim = Rf_allocVector(INTSXP, 2));
            INTEGER(dim)[0] = str_length;
            INTEGER(dim)[1] = static_cast<R_len_t>(max_columns);
            SEXP result = Rf_setAttrib(ret, R_DimSymbol, dim);
            UNPROTECT(1);
            return result;
        });
    }
    }
    STRI__DEFERRED_WARNINGS.emit();

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END({/* no-op */})
}
