#include "../../../src/shared/lint.h"

CHARR_R_HELPER int* bad_r_helper() noexcept
{
    return new int(1);
}
