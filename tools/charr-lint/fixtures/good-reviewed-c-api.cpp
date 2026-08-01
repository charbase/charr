#include "../../../src/shared/lint.h"
#include <reviewed/api.h>

CHARR_NEUTRAL_HELPER int reviewed_c_api_is_neutral(int value) noexcept
{
    return reviewed_plain_call(value) + reviewed_pasted_call(value);
}
