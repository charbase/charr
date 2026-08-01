#include "../../../src/shared/lint.h"
#include "fake-r/Rinternals.h"

CHARR_CXX_HELPER int call_r_declaration()
{
    return inferred_cross_tu_operation();
}
