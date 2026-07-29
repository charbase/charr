#ifndef CHARR_RUNTIME_ICU_H
#define CHARR_RUNTIME_ICU_H

#include "charport.h"

namespace charr { namespace runtime { namespace icu {

extern "C" {

SEXP C_charr_abi_ok(void);

// Initialize charr's own ICU: in bundle mode load the packaged icudt from
// `path`; in system mode `path` is ignored (see src/runtime/icu.cpp for the
// copyright and provenance note).
SEXP C_charr_icu_init(SEXP path);
SEXP C_charr_icu_info(void);
SEXP C_charr_icu_version(void);
SEXP C_charr_icu_ok(void);
SEXP C_charr_icu_smoke(void);
SEXP C_charr_icu_bundled(void);

} // extern "C"

} } } // namespace charr::runtime::icu

#endif
