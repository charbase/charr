#ifndef CHARR_ICU_H
#define CHARR_ICU_H

#include "charport.h"

extern "C" {

SEXP C_charr_abi_ok(void);

// Initialize charr's own ICU: in bundle mode load the packaged icudt from
// `path`; in system mode `path` is ignored (see src/charr_icu.cpp for the
// copyright and provenance note).
SEXP C_charr_icu_init(SEXP path);
SEXP C_charr_icu_version(void);
SEXP C_charr_icu_ok(void);
SEXP C_charr_icu_smoke(void);
SEXP C_charr_icu_bundled(void);

} // extern "C"

#endif
