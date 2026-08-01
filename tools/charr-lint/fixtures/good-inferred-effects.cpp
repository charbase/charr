#include "../../../src/shared/lint.h"
#include "inferred-support.h"

CHARR_CXX_HELPER void inferred_effects_are_accepted()
{
    inferred_may_throw();
    inferred_noexcept();
    inferred_c_call();
    InferredOwner owner = inferred_owner();
    (void)owner;
}
