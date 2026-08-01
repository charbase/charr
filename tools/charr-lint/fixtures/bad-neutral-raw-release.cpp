#include "../../../src/shared/lint.h"
#include "resource-support.h"

CHARR_NEUTRAL_HELPER void bad_neutral_helper(void* resource) noexcept
{
    raw_close(resource);
}
