// No charr function may call the ICU fatal handler or take its address: ICU
// reaches it only through the macros in src/uconfig_local.h. The role is
// accepted only in src/shared/icu_fatal.h and src/shared/icu_fatal.cpp.
#include "../../../src/shared/icu_fatal.h"

CHARR_CXX_HELPER void calls_icu_fatal_handler()
{
    charr::shared::icu_invariant_failure("fixture");
}

#if defined(BAD_HANDLER_ADDRESS)
using Handler = void (*)(const char*);

CHARR_NEUTRAL_HELPER Handler takes_icu_fatal_handler() noexcept
{
    return &charr::shared::icu_invariant_failure;
}
#endif

#if defined(BAD_HANDLER_OUTSIDE_FILE)
[[noreturn]] CHARR_ICU_FATAL_HANDLER void stray_handler(const char* what)
{
    throw what;
}
#endif
