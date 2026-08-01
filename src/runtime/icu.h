#ifndef CHARR_RUNTIME_ICU_H
#define CHARR_RUNTIME_ICU_H

#include "charport.h"
#include "../shared/lint.h"

namespace charr { namespace runtime { namespace icu {

extern "C" {

CHARR_R_HELPER SEXP C_charr_abi_ok(void) noexcept;

// Initialize charr's own ICU: in bundle mode load the packaged icudt from
// `path`; in system mode `path` is ignored (see src/runtime/icu.cpp for the
// copyright and provenance note).
CHARR_R_HELPER SEXP C_charr_icu_init(SEXP path) noexcept;
CHARR_R_HELPER SEXP C_charr_icu_info(void) noexcept;
CHARR_R_HELPER SEXP C_charr_icu_bundled(void) noexcept;

} // extern "C"

} } } // namespace charr::runtime::icu

#endif
