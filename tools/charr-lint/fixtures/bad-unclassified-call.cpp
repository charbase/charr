#include "../../../src/shared/lint.h"

int legacy_helper() noexcept
{
    return 1;
}

CHARR_NEUTRAL_HELPER int checked_helper() noexcept
{
    return legacy_helper();
}
