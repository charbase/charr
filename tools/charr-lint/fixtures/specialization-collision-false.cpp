#include "../../../src/shared/lint.h"
#include "specialization-collision.h"

CHARR_CXX_HELPER void call_false_specialization()
{
    external_specialization<false>();
}
