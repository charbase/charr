// Derived from stringi.
// Copyright (c) 2013-2025, Marek Gagolewski. See inst/COPYRIGHTS.

#include "options_r.h"

#include "../ci_stringi.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace charr {
namespace base_backend {
namespace boundary {

CHARR_R_HELPER SEXP option_names_r(
    SEXP options, R_len_t& count
) noexcept {
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
    if (Rf_isNull(options)) {
        return ci__prepare_arg_locale_r(
            R_NilValue, "locale", true, true
        );
    }

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


CHARR_NEUTRAL_HELPER bool skip_interval(
    const char* name, std::int32_t& first, std::int32_t& limit
) noexcept {
    if (std::strcmp(name, "skip_word_none") == 0) {
        first = UBRK_WORD_NONE;
        limit = UBRK_WORD_NONE_LIMIT;
    }
    else if (std::strcmp(name, "skip_word_number") == 0) {
        first = UBRK_WORD_NUMBER;
        limit = UBRK_WORD_NUMBER_LIMIT;
    }
    else if (std::strcmp(name, "skip_word_letter") == 0) {
        first = UBRK_WORD_LETTER;
        limit = UBRK_WORD_LETTER_LIMIT;
    }
    else if (std::strcmp(name, "skip_word_kana") == 0) {
        first = UBRK_WORD_KANA;
        limit = UBRK_WORD_KANA_LIMIT;
    }
    else if (std::strcmp(name, "skip_word_ideo") == 0) {
        first = UBRK_WORD_IDEO;
        limit = UBRK_WORD_IDEO_LIMIT;
    }
    else if (std::strcmp(name, "skip_line_soft") == 0) {
        first = UBRK_LINE_SOFT;
        limit = UBRK_LINE_SOFT_LIMIT;
    }
    else if (std::strcmp(name, "skip_line_hard") == 0) {
        first = UBRK_LINE_HARD;
        limit = UBRK_LINE_HARD_LIMIT;
    }
    else if (std::strcmp(name, "skip_sentence_term") == 0) {
        first = UBRK_SENTENCE_TERM;
        limit = UBRK_SENTENCE_TERM_LIMIT;
    }
    else if (std::strcmp(name, "skip_sentence_sep") == 0) {
        first = UBRK_SENTENCE_SEP;
        limit = UBRK_SENTENCE_SEP_LIMIT;
    }
    else {
        return false;
    }
    return true;
}


CHARR_R_HELPER void prepare_skip_rules_r(
    SEXP options, shared::BoundaryOptions& result
) noexcept {
    if (Rf_isNull(options))
        return;

    R_len_t count = 0;
    const SEXP names = option_names_r(options, count);
    std::int32_t* rules = count > 0
        ? reinterpret_cast<std::int32_t*>(R_alloc(
            static_cast<std::size_t>(count) * 2,
            static_cast<int>(sizeof(std::int32_t))
        ))
        : nullptr;

    std::size_t size = 0;
    for (R_len_t i = 0; i < count; ++i) {
        const SEXP name_value = STRING_ELT(names, i);
        if (name_value == NA_STRING)
            Rf_error(MSG__INCORRECT_BRKITER_OPTION_SPEC);

        const char* name = CHAR(name_value);
        std::int32_t first = 0;
        std::int32_t limit = 0;
        if (skip_interval(name, first, limit) &&
                ci__prepare_arg_logical_1_notNA_r(
                    VECTOR_ELT(options, i), name
                )) {
            rules[size++] = first;
            rules[size++] = limit;
        }
    }

    result.skip_rules = size > 0 ? rules : nullptr;
    result.skip_size = size;
}


CHARR_NEUTRAL_HELPER int match_type(
    const char* value, std::size_t length
) noexcept {
    static const char* const choices[] = {
        "character", "line_break", "sentence", "word"
    };
    int match = -1;
    for (int i = 0; i < 4; ++i) {
        const std::size_t choice_length = std::strlen(choices[i]);
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


CHARR_R_HELPER void prepare_type_r(
    SEXP options, shared::BoundaryOptions& result
) noexcept {
    if (Rf_isNull(options))
        return;

    R_len_t option_count = 0;
    const SEXP names = option_names_r(options, option_count);
    for (R_len_t i = 0; i < option_count; ++i) {
        const SEXP name = STRING_ELT(names, i);
        if (name == NA_STRING)
            Rf_error(MSG__INCORRECT_BRKITER_OPTION_SPEC);
        if (std::strcmp(CHAR(name), "type") != 0)
            continue;

        SEXP values = PROTECT(ci__prepare_arg_string_r(
            VECTOR_ELT(options, i), "type"
        ));
        const R_xlen_t count = XLENGTH(values);
        if (count <= 0) {
            UNPROTECT(1);
            Rf_error(MSG__ARG_EXPECTED_NOT_EMPTY, "type");
        }

        for (R_xlen_t j = 0; j < count; ++j) {
            const SEXP value = STRING_ELT(values, j);
            if (value == NA_STRING)
                continue;
            if (Rf_getCharCE(value) == CE_BYTES) {
                UNPROTECT(1);
                Rf_error(MSG__BYTESENC);
            }
            Rf_translateCharUTF8(value);
        }

        if (count > 1)
            Rf_warning(MSG__ARG_EXPECTED_1_STRING, "type");

        const SEXP selected = STRING_ELT(values, 0);
        if (selected == NA_STRING) {
            UNPROTECT(1);
            Rf_error(MSG__INCORRECT_MATCH_OPTION, "type");
        }

        const char* utf8 = Rf_translateCharUTF8(selected);
        const std::size_t length = std::strlen(utf8);
        const int matched = match_type(utf8, length);
        result.type = UBRK_CHARACTER;
        result.rules = nullptr;
        result.rules_length = 0;
        result.custom_rules = false;
        if (matched >= 0) {
            static const UBreakIteratorType types[] = {
                UBRK_CHARACTER, UBRK_LINE, UBRK_SENTENCE, UBRK_WORD
            };
            result.type = types[matched];
        }
        else if (length > 0) {
            char* rules = static_cast<char*>(R_alloc(
                length + 1, static_cast<int>(sizeof(char))
            ));
            utf8 = Rf_translateCharUTF8(selected);
            std::memcpy(rules, utf8, length + 1);
            result.rules = rules;
            result.rules_length = static_cast<std::int32_t>(length);
            result.custom_rules = true;
        }

        UNPROTECT(1);
        return;
    }
}


CHARR_R_HELPER shared::BoundaryOptions prepare_options_r(
    SEXP options, UBreakIteratorType default_type
) noexcept {
    shared::BoundaryOptions result{
        nullptr, nullptr, 0, default_type, nullptr, 0, false
    };
    result.locale = prepare_locale_r(options);
    prepare_skip_rules_r(options, result);
    prepare_type_r(options, result);
    return result;
}

} // namespace boundary
} // namespace base_backend
} // namespace charr
