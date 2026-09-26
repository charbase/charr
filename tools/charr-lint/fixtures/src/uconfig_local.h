#ifndef CHARR_LINT_FIXTURE_UCONFIG_LOCAL_H
#define CHARR_LINT_FIXTURE_UCONFIG_LOCAL_H

/* Stands in for src/uconfig_local.h: the one place ICU's fatal macros name
   the handler. */
#include "../../../../src/shared/icu_fatal.h"

#define UPRV_UNREACHABLE_EXIT \
    (::charr::shared::icu_invariant_failure( \
        "ICU internal error: UPRV_UNREACHABLE"))

#endif
