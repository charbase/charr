#include "../../../src/shared/lint.h"
#include "extern-c-support.h"

CHARR_R_HELPER void bad_r_calls_extern_c() noexcept
{
    external_cxx_operation();
}
