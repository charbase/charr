#include "../../../src/shared/lint.h"

// ICU owners may lose the C++ effect through a reviewed override, but only
// when every declaration comes from an ICU header. The same override on the
// same signature declared elsewhere is an integrity error, and ownership
// inference stays in force either way.
#if defined(ICU_OWNER_SYSTEM_HEADER)
#include <unicode/icu-owner.h>
#elif defined(ICU_OWNER_NON_ICU_HEADER)
#include "non-icu/icu-owner.h"
#else
#include "src/icu78/unicode/icu-owner.h"
#endif

CHARR_CXX_HELPER void icu_owner_does_not_throw() noexcept
{
    icu_fixture::IcuOwner empty;
    icu_fixture::IcuOwner converted = icu_fixture::IcuOwner::fromBytes("x");
    (void)empty;
    (void)converted;
}

#ifdef ICU_OWNER_IN_NEUTRAL_HELPER
CHARR_NEUTRAL_HELPER void icu_owner_is_still_owner() noexcept
{
    icu_fixture::IcuOwner converted = icu_fixture::IcuOwner::fromBytes("x");
    (void)converted;
}
#endif
