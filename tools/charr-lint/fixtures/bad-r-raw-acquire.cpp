#include "../../../src/shared/lint.h"
#include "resource-support.h"

CHARR_R_HELPER void* bad_r_helper() noexcept
{
    return raw_open();
}
