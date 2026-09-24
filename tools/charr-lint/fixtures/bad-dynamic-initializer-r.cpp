#include "../../../src/shared/lint.h"

#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif
#include <Rinternals.h>

#include <string>

static SEXP leaked = []() -> SEXP {
    return Rf_allocVector(STRSXP, 1);
}();

#if defined(BAD_THROWING_INITIALIZER)
static const std::string loaded("constructed while the library loads");
#endif
