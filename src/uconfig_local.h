/*
 * ICU configuration adapted from stringi's uconfig_local.h.
 * Copyright (c) 2013-2025, Marek Gagolewski.
 * See inst/COPYRIGHTS for the BSD-3-Clause license.
 */

#ifndef CHARR_UCONFIG_LOCAL_H
#define CHARR_UCONFIG_LOCAL_H

#include "shared/icu_fatal.h"

/* Rtools40's 32-bit import library does not provide ResolveLocaleName. */
#if defined(_WIN32) && !defined(_WIN64)
#define CHARR_DISABLE_RESOLVE_LOCALE_NAME 1
#else
#define CHARR_DISABLE_RESOLVE_LOCALE_NAME 0
#endif

/*
 * Never terminate the R process for an ICU internal invariant failure. ICU
 * uses these macros only at sites its authors intend to be unreachable. The
 * handler throws inside a parallel body and signals an R error elsewhere;
 * see src/shared/icu_fatal.h. `make lint-fatal-sites` checks every expansion
 * against tools/charr-lint/fatal-sites.tsv.
 */
#define UPRV_UNREACHABLE_EXIT \
    (::charr::shared::icu_invariant_failure( \
        "ICU internal error: UPRV_UNREACHABLE"))
#define DOUBLE_CONVERSION_UNIMPLEMENTED() \
    (::charr::shared::icu_invariant_failure( \
        "ICU internal error: DOUBLE_CONVERSION_UNIMPLEMENTED"))
#define DOUBLE_CONVERSION_UNREACHABLE() \
    (::charr::shared::icu_invariant_failure( \
        "ICU internal error: DOUBLE_CONVERSION_UNREACHABLE"))

#endif
