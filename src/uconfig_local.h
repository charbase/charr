/*
 * ICU configuration adapted from stringi's uconfig_local.h.
 * Copyright (c) 2013-2025, Marek Gagolewski.
 * See inst/COPYRIGHTS for the BSD-3-Clause license.
 */

#ifndef CHARR_UCONFIG_LOCAL_H
#define CHARR_UCONFIG_LOCAL_H

#define R_NO_REMAP
#include <R_ext/Error.h>

/* Never terminate the R process for an ICU internal invariant failure. */
#define UPRV_UNREACHABLE_EXIT \
    (Rf_error("ICU internal error: UPRV_UNREACHABLE"))
#define DOUBLE_CONVERSION_UNIMPLEMENTED() \
    (Rf_error("ICU internal error: DOUBLE_CONVERSION_UNIMPLEMENTED"))
#define DOUBLE_CONVERSION_UNREACHABLE() \
    (Rf_error("ICU internal error: DOUBLE_CONVERSION_UNREACHABLE"))

#endif
