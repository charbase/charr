#include "../../../src/shared/lint.h"

CHARR_CXX_HELPER int cxx_helper() noexcept
{
    return 1;
}

CHARR_NEUTRAL_HELPER int bad_neutral_helper() noexcept
{
    return cxx_helper();
}
