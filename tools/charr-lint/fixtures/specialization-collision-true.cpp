#include "../../../src/shared/lint.h"
#include "specialization-collision.h"

CHARR_CXX_HELPER void call_true_specialization()
{
    external_specialization<true>();
}
