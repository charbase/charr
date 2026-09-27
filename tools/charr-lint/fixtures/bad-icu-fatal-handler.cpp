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

// Default arguments and dynamic initializers have no role; naming or calling
// the handler there is the same error.
#if defined(BAD_HANDLER_DEFAULT_ARGUMENT)
using DefaultHandler = void (*)(const char*);

CHARR_NEUTRAL_HELPER bool has_default_handler(
    DefaultHandler handler = &charr::shared::icu_invariant_failure
) noexcept
{
    return handler != nullptr;
}
#endif

#if defined(BAD_HANDLER_DYNAMIC_INITIALIZER)
int handler_initialized =
    (charr::shared::icu_invariant_failure("fixture"), 0);
#endif

// A constant initializer runs no code, but a stored pointer could be called.
#if defined(BAD_HANDLER_CONSTANT_POINTER)
void (*const stored_handler)(const char*) =
    &charr::shared::icu_invariant_failure;
#endif
