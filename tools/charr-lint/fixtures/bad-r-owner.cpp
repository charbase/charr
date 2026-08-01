#include "../../../src/shared/lint.h"

#include <string>

CHARR_R_HELPER int bad_r_helper() noexcept
{
    std::string owner("value");
    return static_cast<int>(owner.size());
}
