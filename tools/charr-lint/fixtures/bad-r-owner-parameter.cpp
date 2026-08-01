#include "../../../src/shared/lint.h"

#include <string>

CHARR_R_HELPER int bad_r_helper(std::string value) noexcept
{
    return value.empty() ? 0 : 1;
}
