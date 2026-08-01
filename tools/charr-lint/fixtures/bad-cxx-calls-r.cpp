#include "../../../src/shared/lint.h"

CHARR_R_HELPER int r_value() noexcept
{
    return 1;
}

CHARR_CXX_HELPER int bad_cxx_helper()
{
    return r_value();
}
