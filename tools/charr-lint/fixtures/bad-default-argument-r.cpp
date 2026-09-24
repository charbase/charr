#include "../../../src/shared/lint.h"

#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif
#include <Rinternals.h>

CHARR_R_HELPER int r_default(SEXP value = Rf_allocVector(STRSXP, 1)) noexcept
{
    return value == nullptr ? 0 : 1;
}
