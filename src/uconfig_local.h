/*
 * ICU configuration adapted from stringi's uconfig_local.h.
 * Copyright (c) 2013-2025, Marek Gagolewski.
 * See inst/COPYRIGHTS for the BSD-3-Clause license.
 */

#ifndef CHARR_UCONFIG_LOCAL_H
#define CHARR_UCONFIG_LOCAL_H

#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif
#include <R_ext/Error.h>

/* Rtools40's 32-bit import library does not provide ResolveLocaleName. */
#if defined(_WIN32) && !defined(_WIN64)
#define CHARR_DISABLE_RESOLVE_LOCALE_NAME 1
#else
#define CHARR_DISABLE_RESOLVE_LOCALE_NAME 0
#endif

/* Never terminate the R process for an ICU internal invariant failure. */
#define UPRV_UNREACHABLE_EXIT \
    (Rf_error("ICU internal error: UPRV_UNREACHABLE"))
#define DOUBLE_CONVERSION_UNIMPLEMENTED() \
    (Rf_error("ICU internal error: DOUBLE_CONVERSION_UNIMPLEMENTED"))
#define DOUBLE_CONVERSION_UNREACHABLE() \
    (Rf_error("ICU internal error: DOUBLE_CONVERSION_UNREACHABLE"))

#endif
