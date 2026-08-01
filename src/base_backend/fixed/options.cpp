// Derived from stringi 19e9586ba39b3320df49355e32bd18d74ed6098f.
// Copyright (c) 2013-2025, Marek Gagolewski. See inst/COPYRIGHTS.

#include "options.h"

#include "../ci_stringi.h"

#include <cstring>

namespace charr {
namespace base_backend {
namespace fixed {

CHARR_R_HELPER shared::FixedSearchOptions prepare_options(
    SEXP input, bool allow_overlap
) noexcept
{
    shared::FixedSearchOptions options{false, false};
    if (!Rf_isNull(input) && !Rf_isVectorList(input))
        Rf_error(MSG__ARG_EXPECTED_LIST, "opts_fixed");

    const R_len_t count = Rf_isNull(input) ? 0 : LENGTH(input);
    if (count <= 0)
        return options;

    SEXP names = PROTECT(Rf_getAttrib(input, R_NamesSymbol));
    if (names == R_NilValue || LENGTH(names) != count)
        Rf_error(MSG__FIXED_CONFIG_FAILED);

    for (R_len_t i = 0; i < count; ++i) {
        if (STRING_ELT(names, i) == NA_STRING)
            Rf_error(MSG__FIXED_CONFIG_FAILED);

        const char* name = CHAR(STRING_ELT(names, i));
        SEXP value = PROTECT(VECTOR_ELT(input, i));
        if (std::strcmp(name, "case_insensitive") == 0) {
            if (ci__prepare_arg_logical_1_notNA_r(
                    value, "case_insensitive")) {
                options.case_insensitive = true;
            }
        }
        else if (std::strcmp(name, "overlap") == 0 && allow_overlap) {
            if (ci__prepare_arg_logical_1_notNA_r(value, "overlap"))
                options.overlap = true;
        }
        else {
            Rf_warning(MSG__INCORRECT_FIXED_OPTION, name);
        }
        UNPROTECT(1);
    }

    UNPROTECT(1);
    return options;
}

} // namespace fixed
} // namespace base_backend
} // namespace charr
