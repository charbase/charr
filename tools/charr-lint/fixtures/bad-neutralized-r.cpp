#include "../../../src/shared/lint.h"

#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif
#include <Rinternals.h>

CHARR_CXX_HELPER void from_cxx(SEXP value)
{
    if (Rf_isInteger(value)) {
        (void)PRINTNAME(value);
    }
    Rf_unprotect(1);
}
