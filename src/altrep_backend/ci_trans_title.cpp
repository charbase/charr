// Copied from stringi 19e9586ba39b3320df49355e32bd18d74ed6098f;
// stri_* renamed to ci_*. See inst/COPYRIGHTS.
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
#include "io/reader_utils.h"
#include "../shared/entrypoint.h"
#include "../shared/protect.h"
#include "../shared/title_case.h"
#include "../shared/unwind.h"
#include "io/string_view.h"
#include "io/utf8_output.h"

#include <charport.h>

#include <cstddef>
#include <cstring>
#include <exception>
#include <stdexcept>

namespace charr { namespace altrep_backend {


namespace trans_title {

CHARR_NEUTRAL_HELPER bool option_name_is(
    const char* name, const char* expected
) noexcept {
    return std::strcmp(name, expected) == 0;
}


CHARR_NEUTRAL_HELPER std::size_t string_length(
    const char* value
) noexcept {
    std::size_t length = 0;
    while (value[length] != '\0')
        ++length;
    return length;
}


CHARR_NEUTRAL_HELPER int match_type(
    const char* value, std::size_t length
) noexcept {
    const char* choices[] = {
        "character", "line_break", "sentence", "word"
    };
    int match = -1;
    for (int i = 0; i < 4; ++i) {
        const std::size_t choice_length = string_length(choices[i]);
        if (length > choice_length ||
                std::memcmp(value, choices[i], length) != 0) {
            continue;
        }
        if (length == choice_length)
            return i;
        if (match >= 0)
            return -1;
        match = i;
    }
    return match;
}


CHARR_R_HELPER const char* prepare_locale_r(
    SEXP options
) noexcept {
    if (Rf_isNull(options))
        return ci__prepare_arg_locale_r(
            R_NilValue, "locale", true, true
        );
    if (!Rf_isVectorList(options))
        Rf_error(MSG__INCORRECT_BRKITER_OPTION_SPEC);

    const R_len_t count = LENGTH(options);
    SEXP names = PROTECT(Rf_getAttrib(options, R_NamesSymbol));
    if (names == R_NilValue || LENGTH(names) != count)
        Rf_error(MSG__INCORRECT_BRKITER_OPTION_SPEC);

    for (R_len_t i = 0; i < count; ++i) {
        const SEXP name = STRING_ELT(names, i);
        if (name == NA_STRING)
            Rf_error(MSG__INCORRECT_BRKITER_OPTION_SPEC);
        if (option_name_is(CHAR(name), "locale")) {
            const char* locale = ci__prepare_arg_locale_r(
                VECTOR_ELT(options, i), "locale", true, true
            );
            UNPROTECT(1);
            return locale;
        }
    }

    UNPROTECT(1);
    return ci__prepare_arg_locale_r(
        R_NilValue, "locale", true, true
    );
}


CHARR_R_HELPER bool prepare_skip_options_r(
    SEXP options
) noexcept {
    if (Rf_isNull(options))
        return false;
    if (!Rf_isVectorList(options))
        Rf_error(MSG__INCORRECT_BRKITER_OPTION_SPEC);

    const R_len_t count = LENGTH(options);
    SEXP names = PROTECT(Rf_getAttrib(options, R_NamesSymbol));
    if (names == R_NilValue || LENGTH(names) != count)
        Rf_error(MSG__INCORRECT_BRKITER_OPTION_SPEC);

    bool has_skip_rules = false;
    for (R_len_t i = 0; i < count; ++i) {
        const SEXP name = STRING_ELT(names, i);
        if (name == NA_STRING)
            Rf_error(MSG__INCORRECT_BRKITER_OPTION_SPEC);

        const char* option_name = CHAR(name);
        const bool recognized =
            option_name_is(option_name, "skip_word_none") ||
            option_name_is(option_name, "skip_word_number") ||
            option_name_is(option_name, "skip_word_letter") ||
            option_name_is(option_name, "skip_word_kana") ||
            option_name_is(option_name, "skip_word_ideo") ||
            option_name_is(option_name, "skip_line_soft") ||
            option_name_is(option_name, "skip_line_hard") ||
            option_name_is(option_name, "skip_sentence_term") ||
            option_name_is(option_name, "skip_sentence_sep");
        if (recognized && ci__prepare_arg_logical_1_notNA_r(
                VECTOR_ELT(options, i), option_name)) {
            has_skip_rules = true;
        }
    }

    UNPROTECT(1);
    return has_skip_rules;
}


CHARR_R_HELPER void prepare_type_r(
    SEXP options, shared::TitleCaseOptions& result
) noexcept {
    if (Rf_isNull(options))
        return;
    if (!Rf_isVectorList(options))
        Rf_error(MSG__INCORRECT_BRKITER_OPTION_SPEC);

    const R_len_t option_count = LENGTH(options);
    SEXP names = PROTECT(Rf_getAttrib(options, R_NamesSymbol));
    if (names == R_NilValue || LENGTH(names) != option_count)
        Rf_error(MSG__INCORRECT_BRKITER_OPTION_SPEC);

    for (R_len_t i = 0; i < option_count; ++i) {
        const SEXP name = STRING_ELT(names, i);
        if (name == NA_STRING)
            Rf_error(MSG__INCORRECT_BRKITER_OPTION_SPEC);
        if (!option_name_is(CHAR(name), "type"))
            continue;

        SEXP values = PROTECT(ci__prepare_arg_string_r(
            VECTOR_ELT(options, i), "type"
        ));
        const R_xlen_t value_count = XLENGTH(values);
        if (value_count <= 0)
            Rf_error(MSG__ARG_EXPECTED_NOT_EMPTY, "type");

        for (R_xlen_t j = 0; j < value_count; ++j) {
            const SEXP value = STRING_ELT(values, j);
            if (value == NA_STRING)
                continue;
            if (Rf_getCharCE(value) == CE_BYTES)
                Rf_error(MSG__BYTESENC);
            Rf_translateCharUTF8(value);
        }

        if (value_count > 1)
            Rf_warning(MSG__ARG_EXPECTED_1_STRING, "type");
        const SEXP selected = STRING_ELT(values, 0);
        if (selected == NA_STRING)
            Rf_error(MSG__INCORRECT_MATCH_OPTION, "type");

        const char* first_value = Rf_translateCharUTF8(selected);
        const std::size_t first_length = string_length(first_value);
        char* rules = static_cast<char*>(R_alloc(
            first_length + 1, static_cast<int>(sizeof(char))
        ));
        first_value = Rf_translateCharUTF8(selected);
        std::memcpy(rules, first_value, first_length + 1);

        const int matched = match_type(rules, first_length);
        if (matched >= 0) {
            const UBreakIteratorType types[] = {
                UBRK_CHARACTER, UBRK_LINE, UBRK_SENTENCE, UBRK_WORD
            };
            result.type = types[matched];
            result.custom_rules = false;
            result.rules = nullptr;
            result.rules_length = 0;
        }
        else {
            result.rules = rules;
            result.rules_length = static_cast<std::int32_t>(first_length);
            result.custom_rules = true;
        }

        UNPROTECT(2);
        return;
    }

    UNPROTECT(1);
}


CHARR_R_HELPER shared::TitleCaseOptions prepare_options_r(
    SEXP options
) noexcept {
    shared::TitleCaseOptions result{
        nullptr, nullptr, 0, UBRK_WORD, false, false
    };
    result.locale = prepare_locale_r(options);
    result.has_skip_rules = prepare_skip_options_r(options);
    prepare_type_r(options, result);
    return result;
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

    const shared::TitleCaseOptions options = prepare_options_r(opts_brkiter);
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );


    bool root_fallback_warning = false;

    try {
        charport::Reader reader;
        charport::StrViews values;
        io::OutputBuilder builder(0);
        shared::TitleCaseMapper mapper;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t str_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );

                const shared::TitleCaseOpenResult open_result =
                    mapper.reset(options);
                root_fallback_warning = open_result.root_fallback;
                require_icu_success(open_result.status);

                reader.reset(str);
                if (reader.size() != str_length) {
                    throw std::runtime_error(
                        "Reader length changed during titlecase conversion"
                    );
                }
                values.resize(str_length);
                if (str_length > 0) {
                    reader.views(
                        0, str_length,
                        values.ptrs(), values.lengths(), values.encodings()
                    );
                }
                builder.reset(str_length);

                for (R_len_t i = 0; i < str_length; ++i) {
                    const charport::StrView source_view = values[i];
                    if (source_view.is_na()) {
                        builder.set_na(i);
                        continue;
                    }

                    const shared::TitleCaseInput input = mapper.prepare(
                        io::as_shared_view(source_view)
                    );
                    if (mapper.has_ascii_fast_path(input)) {
                        char* output = builder.reserve(
                            i, static_cast<std::size_t>(input.length),
                            cetype_ext_t::CE_ASCII
                        );
                        mapper.map_ascii(input, output);
                        continue;
                    }

                    UErrorCode status = U_ZERO_ERROR;
                    const shared::StringView mapped = mapper.map_icu(
                        input, status
                    );
                    require_icu_success(status);
                    builder.set(i, io::as_charport_view(mapped));
                }

                result = entry_protections.reprotect_one(
                    builder.to_sexp(), result_index
                );
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

} } // namespace charr::altrep_backend
