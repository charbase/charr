#include "../../../src/shared/lint.h"
#include "trusted-unreviewed-support.h"

CHARR_TRUSTED_UNWIND void trusted_unwind_operation() noexcept
{
    unreviewed_unwind_operation();
}
