// Derived from stringi.
// Copyright (c) 2013-2025, Marek Gagolewski. See inst/COPYRIGHTS.

#include "options_r.h"

#include "../ci_stringi.h"

#include <cstring>
#include <unicode/uregex.h>

namespace charr {
namespace base_backend {
namespace regex {

CHARR_R_HELPER shared::RegexOptions prepare_options(SEXP input) noexcept
{
    shared::RegexOptions options{0, 0, 0};
    if (!Rf_isNull(input) && !Rf_isVectorList(input))
        Rf_error(MSG__ARG_EXPECTED_LIST, "opts_regex");

    const R_len_t count = Rf_isNull(input) ? 0 : LENGTH(input);
    if (count <= 0)
        return options;

    SEXP names = PROTECT(Rf_getAttrib(input, R_NamesSymbol));
    if (names == R_NilValue || LENGTH(names) != count)
        Rf_error(MSG__REGEX_CONFIG_FAILED);

    for (R_len_t i = 0; i < count; ++i) {
        if (STRING_ELT(names, i) == NA_STRING)
            Rf_error(MSG__REGEX_CONFIG_FAILED);

        const char* name = CHAR(STRING_ELT(names, i));
        SEXP value = PROTECT(VECTOR_ELT(input, i));

        if (std::strcmp(name, "case_insensitive") == 0) {
            if (ci__prepare_arg_logical_1_notNA_r(
                    value, "case_insensitive")) {
                options.flags |= UREGEX_CASE_INSENSITIVE;
            }
        }
        else if (std::strcmp(name, "comments") == 0) {
            if (ci__prepare_arg_logical_1_notNA_r(value, "comments"))
                options.flags |= UREGEX_COMMENTS;
        }
        else if (std::strcmp(name, "dotall") == 0) {
            if (ci__prepare_arg_logical_1_notNA_r(value, "dotall"))
                options.flags |= UREGEX_DOTALL;
        }
        else if (std::strcmp(name, "literal") == 0) {
            if (ci__prepare_arg_logical_1_notNA_r(value, "literal"))
                options.flags |= UREGEX_LITERAL;
        }
        else if (std::strcmp(name, "multiline") == 0) {
            if (ci__prepare_arg_logical_1_notNA_r(value, "multiline"))
                options.flags |= UREGEX_MULTILINE;
        }
        else if (std::strcmp(name, "unix_lines") == 0) {
            if (ci__prepare_arg_logical_1_notNA_r(value, "unix_lines"))
                options.flags |= UREGEX_UNIX_LINES;
        }
        else if (std::strcmp(name, "uword") == 0) {
            if (ci__prepare_arg_logical_1_notNA_r(value, "uword"))
                options.flags |= UREGEX_UWORD;
        }
        else if (std::strcmp(name, "error_on_unknown_escapes") == 0) {
            if (ci__prepare_arg_logical_1_notNA_r(
                    value, "error_on_unknown_escapes")) {
                options.flags |= UREGEX_ERROR_ON_UNKNOWN_ESCAPES;
            }
        }
        else if (std::strcmp(name, "stack_limit") == 0) {
            options.stack_limit = ci__prepare_arg_integer_1_notNA_r(
                value, "stack_limit"
            );
        }
        else if (std::strcmp(name, "time_limit") == 0) {
            options.time_limit = ci__prepare_arg_integer_1_notNA_r(
                value, "time_limit"
            );
        }
        else {
            Rf_warning(MSG__INCORRECT_REGEX_OPTION, name);
        }

        UNPROTECT(1);
    }

    UNPROTECT(1);
    return options;
}

} // namespace regex
} // namespace base_backend
} // namespace charr
