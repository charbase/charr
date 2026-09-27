// Outside src/icu78/: a fatal macro expanded here is not an ICU site, even
// though it is spelled through src/uconfig_local.h.
#include "uconfig_local.h"

int outside_icu(int value)
{
    if (value < 0)
        UPRV_UNREACHABLE_EXIT;
    return value;
}
