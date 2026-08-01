// Derived from stringi 19e9586ba39b3320df49355e32bd18d74ed6098f.
// Copyright (c) 2013-2025, Marek Gagolewski. See inst/COPYRIGHTS.

#include "options.h"

#include "../ci_stringi.h"

#include <cstring>

namespace charr {
namespace base_backend {
namespace collator {

CHARR_NEUTRAL_HELPER shared::CollatorOptions default_options(
    bool custom, const char* locale
) noexcept
{
    return shared::CollatorOptions{
        custom,
        locale,
        UCOL_DEFAULT,
        UCOL_DEFAULT,
        UCOL_DEFAULT,
        UCOL_DEFAULT,
        UCOL_DEFAULT,
        UCOL_DEFAULT_STRENGTH,
        UCOL_DEFAULT
    };
}


CHARR_R_HELPER shared::CollatorOptions prepare_options(
    SEXP input
) noexcept
{
    if (!Rf_isNull(input) && !Rf_isVectorList(input))
        Rf_error(MSG__INCORRECT_COLLATOR_OPTION_SPEC);

    const R_len_t count = Rf_isNull(input) ? 0 : LENGTH(input);
    shared::CollatorOptions options = default_options(
        count > 0,
        ci__prepare_arg_locale_r(R_NilValue, "locale")
    );
    if (count <= 0)
        return options;

    SEXP names = PROTECT(Rf_getAttrib(input, R_NamesSymbol));
    if (names == R_NilValue || LENGTH(names) != count)
        Rf_error(MSG__INCORRECT_COLLATOR_OPTION_SPEC);

    for (R_len_t i = 0; i < count; ++i) {
        if (STRING_ELT(names, i) == NA_STRING)
            Rf_error(MSG__INCORRECT_COLLATOR_OPTION_SPEC);

        const char* name = CHAR(STRING_ELT(names, i));
        SEXP value = PROTECT(VECTOR_ELT(input, i));

        if (std::strcmp(name, "locale") == 0) {
            options.locale = ci__prepare_arg_locale_r(value, "locale");
        }
        else if (std::strcmp(name, "strength") == 0) {
            int strength = ci__prepare_arg_integer_1_notNA_r(
                value, "strength"
            );
            if (strength < static_cast<int>(UCOL_PRIMARY)+1)
                strength = static_cast<int>(UCOL_PRIMARY)+1;
            else if (strength > static_cast<int>(UCOL_STRENGTH_LIMIT)+1)
                strength = static_cast<int>(UCOL_STRENGTH_LIMIT)+1;
            options.strength = static_cast<UColAttributeValue>(strength-1);
        }
        else if (std::strcmp(name, "alternate_shifted") == 0) {
            options.alternate_handling =
                ci__prepare_arg_logical_1_notNA_r(
                    value, "alternate_shifted"
                ) ? UCOL_SHIFTED : UCOL_NON_IGNORABLE;
        }
        else if (std::strcmp(name, "uppercase_first") == 0) {
            SEXP logical = PROTECT(ci__prepare_arg_logical_1_r(
                value, "uppercase_first"
            ));
            const int setting = LOGICAL(logical)[0];
            options.case_first = setting == NA_LOGICAL
                ? UCOL_OFF
                : (setting ? UCOL_UPPER_FIRST : UCOL_LOWER_FIRST);
            UNPROTECT(1);
        }
        else if (std::strcmp(name, "french") == 0) {
            options.french_collation =
                ci__prepare_arg_logical_1_notNA_r(value, "french")
                    ? UCOL_ON : UCOL_OFF;
        }
        else if (std::strcmp(name, "case_level") == 0) {
            options.case_level =
                ci__prepare_arg_logical_1_notNA_r(value, "case_level")
                    ? UCOL_ON : UCOL_OFF;
        }
        else if (std::strcmp(name, "normalization") == 0) {
            options.normalization_mode =
                ci__prepare_arg_logical_1_notNA_r(
                    value, "normalization"
                ) ? UCOL_ON : UCOL_OFF;
        }
        else if (std::strcmp(name, "numeric") == 0) {
            options.numeric_collation =
                ci__prepare_arg_logical_1_notNA_r(value, "numeric")
                    ? UCOL_ON : UCOL_OFF;
        }
        else {
            Rf_warning(MSG__INCORRECT_COLLATOR_OPTION, name);
        }

        UNPROTECT(1);
    }

    UNPROTECT(1);
    return options;
}

} // namespace collator
} // namespace base_backend
} // namespace charr
