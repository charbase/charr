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
#include "io/string_view.h"
#include "../shared/entrypoint.h"
#include "../shared/protect.h"
#include "../shared/title_case.h"
#include "../shared/unwind.h"

#include <cstddef>
#include <cstring>
#include <exception>
#include <vector>


namespace charr { namespace base_backend {

namespace trans_title {

CHARR_R_HELPER SEXP option_names_r(
    SEXP options, R_len_t& count
) noexcept
{
    if (!Rf_isVectorList(options))
        Rf_error(MSG__INCORRECT_BRKITER_OPTION_SPEC);

    count = LENGTH(options);
    SEXP names = Rf_getAttrib(options, R_NamesSymbol);
    if (names == R_NilValue || LENGTH(names) != count)
        Rf_error(MSG__INCORRECT_BRKITER_OPTION_SPEC);
    return names;
}


CHARR_R_HELPER const char* prepare_locale_r(SEXP options) noexcept
{
    if (Rf_isNull(options))
        return ci__prepare_arg_locale_r(
            R_NilValue, "locale", true, true
        );

    R_len_t count = 0;
    const SEXP names = option_names_r(options, count);
    for (R_len_t i = 0; i < count; ++i) {
        const SEXP name = STRING_ELT(names, i);
        if (name == NA_STRING)
            Rf_error(MSG__INCORRECT_BRKITER_OPTION_SPEC);
        if (std::strcmp(CHAR(name), "locale") == 0) {
            return ci__prepare_arg_locale_r(
                VECTOR_ELT(options, i), "locale", true, true
            );
        }
    }

    return ci__prepare_arg_locale_r(
        R_NilValue, "locale", true, true
    );
}


CHARR_NEUTRAL_HELPER bool is_skip_option(const char* name) noexcept
{
    return std::strcmp(name, "skip_word_none") == 0 ||
        std::strcmp(name, "skip_word_number") == 0 ||
        std::strcmp(name, "skip_word_letter") == 0 ||
        std::strcmp(name, "skip_word_kana") == 0 ||
        std::strcmp(name, "skip_word_ideo") == 0 ||
        std::strcmp(name, "skip_line_soft") == 0 ||
        std::strcmp(name, "skip_line_hard") == 0 ||
        std::strcmp(name, "skip_sentence_term") == 0 ||
        std::strcmp(name, "skip_sentence_sep") == 0;
}


CHARR_NEUTRAL_HELPER std::size_t string_length(
    const char* value
) noexcept
{
    std::size_t length = 0;
    while (value[length] != '\0')
        ++length;
    return length;
}


CHARR_R_HELPER bool prepare_skip_options_r(SEXP options) noexcept
{
    if (Rf_isNull(options))
        return false;

    R_len_t count = 0;
    const SEXP names = option_names_r(options, count);
    bool has_skip_rules = false;
    for (R_len_t i = 0; i < count; ++i) {
        const SEXP name = STRING_ELT(names, i);
        if (name == NA_STRING)
            Rf_error(MSG__INCORRECT_BRKITER_OPTION_SPEC);

        const char* text = CHAR(name);
        if (is_skip_option(text) &&
                ci__prepare_arg_logical_1_notNA_r(
                    VECTOR_ELT(options, i), text
                )) {
            has_skip_rules = true;
        }
    }
    return has_skip_rules;
}


CHARR_NEUTRAL_HELPER int match_type(const char* option) noexcept
{
    static const char* const values[] = {
        "character", "line_break", "sentence", "word"
    };
    const std::size_t option_length = string_length(option);
    int match = -1;
    for (int i = 0; i < 4; ++i) {
        if (std::strcmp(option, values[i]) == 0)
            return i;

        bool prefix = true;
        for (std::size_t k = 0; k < option_length; ++k) {
            if (values[i][k] == '\0' || values[i][k] != option[k]) {
                prefix = false;
                break;
            }
        }
        if (prefix) {
            if (match >= 0)
                return -1;
            match = i;
        }
    }
    return match;
}


CHARR_R_HELPER shared::TitleCaseOptions prepare_type_r(
    SEXP options, shared::TitleCaseOptions result
) noexcept
{
    SEXP type = R_NilValue;
    bool found_type = false;
    if (!Rf_isNull(options)) {
        R_len_t count = 0;
        const SEXP names = option_names_r(options, count);
        for (R_len_t i = 0; i < count; ++i) {
            const SEXP name = STRING_ELT(names, i);
            if (name == NA_STRING)
                Rf_error(MSG__INCORRECT_BRKITER_OPTION_SPEC);
            if (std::strcmp(CHAR(name), "type") == 0) {
                type = VECTOR_ELT(options, i);
                found_type = true;
                break;
            }
        }
    }
    if (!found_type)
        return result;

    PROTECT(type = ci__prepare_arg_string_r(type, "type"));
    const R_xlen_t count = XLENGTH(type);
    if (count <= 0) {
        UNPROTECT(1);
        Rf_error(MSG__ARG_EXPECTED_NOT_EMPTY, "type");
    }

    for (R_xlen_t i = 0; i < count; ++i) {
        const SEXP value = STRING_ELT(type, i);
        if (value == NA_STRING)
            continue;
        if (IS_BYTES(value)) {
            UNPROTECT(1);
            Rf_error(MSG__BYTESENC);
        }
        Rf_translateCharUTF8(value);
    }

    if (count > 1)
        Rf_warning(MSG__ARG_EXPECTED_1_STRING, "type");

    const SEXP selected = STRING_ELT(type, 0);
    if (selected == NA_STRING) {
        UNPROTECT(1);
        Rf_error(MSG__INCORRECT_MATCH_OPTION, "type");
    }

    const char* utf8 = Rf_translateCharUTF8(selected);
    const std::size_t length = string_length(utf8);
    char* rules = static_cast<char*>(R_alloc(length + 1, sizeof(char)));
    utf8 = Rf_translateCharUTF8(selected);
    std::memcpy(rules, utf8, length + 1);
    UNPROTECT(1);

    const int matched = match_type(rules);
    if (matched >= 0) {
        static const UBreakIteratorType values[] = {
            UBRK_CHARACTER, UBRK_LINE, UBRK_SENTENCE, UBRK_WORD
        };
        result.type = values[matched];
        result.rules = nullptr;
        result.rules_length = 0;
        result.custom_rules = false;
    }
    else {
        result.rules = rules;
        result.rules_length = static_cast<std::int32_t>(length);
        result.custom_rules = true;
    }
    return result;
}


CHARR_R_HELPER shared::TitleCaseOptions prepare_options_r(
    SEXP options
) noexcept
{
    shared::TitleCaseOptions result{
        nullptr, nullptr, 0, UBRK_WORD, false, false
    };
    result.locale = prepare_locale_r(options);
    result.has_skip_rules = prepare_skip_options_r(options);
    return prepare_type_r(options, result);
}


CHARR_CXX_HELPER void require_icu_success(UErrorCode status)
{
    if (U_FAILURE(status))
        throw StriException(status);
}

} // namespace trans_title

using namespace trans_title;


/**
 * Convert case (TitleCase)
 *
 * @param str character vector
 * @param opts_brkiter list
 * @return character vector
 *
 */
CHARR_ENTRYPOINT SEXP ci_trans_totitle(
    SEXP str, SEXP opts_brkiter
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const shared::TitleCaseOptions options =
        prepare_options_r(opts_brkiter);
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));

    bool root_fallback_warning = false;

    try {
        shared::TitleCaseMapper mapper;
        std::vector<char> ascii_output;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const shared::TitleCaseOpenResult open_result =
                    mapper.reset(options);
                root_fallback_warning = open_result.root_fallback;
                require_icu_success(open_result.status);

                const R_xlen_t str_length = XLENGTH(str);
                result = entry_protections.reprotect_one(
                    Rf_allocVector(STRSXP, str_length), result_index
                );
                const SEXP* values = str_length > 0
                    ? STRING_PTR_RO(str)
                    : nullptr;

                for (R_xlen_t i = 0; i < str_length; ++i) {
                    const SEXP value = values[i];
                    if (value == NA_STRING) {
                        SET_STRING_ELT(result, i, NA_STRING);
                        continue;
                    }

                    const shared::StringView source =
                        io::as_shared_view(value);
                    const shared::TitleCaseInput input =
                        mapper.prepare(source);

                    const char* output = nullptr;
                    int output_length = 0;
                    if (mapper.has_ascii_fast_path(input)) {
                        ascii_output.resize(
                            static_cast<std::size_t>(input.length)
                        );
                        mapper.map_ascii(input, ascii_output.data());
                        output = input.length > 0
                            ? ascii_output.data()
                            : "";
                        output_length = input.length;
                    }
                    else {
                        UErrorCode status = U_ZERO_ERROR;
                        const shared::StringView mapped =
                            mapper.map_icu(input, status);
                        require_icu_success(status);
                        output = mapped.ptr;
                        output_length = mapped.len;
                    }

                    SET_STRING_ELT(
                        result, i,
                        Rf_mkCharLenCE(output, output_length, CE_UTF8)
                    );
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        if (root_fallback_warning) {
            Rf_warning(
                "%s", ICUError::getICUerrorName(U_USING_DEFAULT_WARNING)
            );
        }
    );
}

} } // namespace charr::base_backend
