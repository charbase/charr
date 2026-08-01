#include "../../../src/shared/lint.h"
#include "resource-support.h"

CHARR_CXX_HELPER void* call_unreviewed_external() noexcept
{
    return raw_open();
}
