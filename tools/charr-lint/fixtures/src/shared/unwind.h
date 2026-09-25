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

#endif
