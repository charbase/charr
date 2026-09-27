#ifndef CHARR_LINT_FIXTURE_TRUSTED_UNWIND_H
#define CHARR_LINT_FIXTURE_TRUSTED_UNWIND_H

// A fixture stand-in for src/shared/unwind.h: charr-lint accepts
// CHARR_TRUSTED_UNWIND only on definitions in a file with that path suffix.

#include "../../../../../src/shared/lint.h"
#include "../../trusted-unreviewed-support.h"

CHARR_TRUSTED_UNWIND inline void trusted_unwind_operation() noexcept
{
    unreviewed_unwind_operation();
}

// Only ICU reaches the ICU fatal handler; the trusted unwind role does not
// lift that rule.
#if defined(BAD_TRUSTED_CALLS_ICU_FATAL_HANDLER)
#include "../../../../../src/shared/icu_fatal.h"

CHARR_TRUSTED_UNWIND inline void trusted_calls_icu_fatal_handler() noexcept
{
    charr::shared::icu_invariant_failure("fixture");
}
#endif

#endif
