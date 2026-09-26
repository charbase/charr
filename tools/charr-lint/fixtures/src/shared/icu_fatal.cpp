// Stands in for src/shared/icu_fatal.cpp. The ICU fatal handler may enter R
// and may throw, which no other role may do together, so the role is held to
// its reviewed files, to [[noreturn]], and to a declaration that can throw.
#include "../../../../../src/shared/icu_fatal.h"

#include <R_ext/Error.h>

namespace {

struct Failure {
    const char* what;
};

CHARR_NEUTRAL_HELPER bool inside_parallel_body() noexcept
{
    return false;
}

} // namespace

[[noreturn]] CHARR_ICU_FATAL_HANDLER void
charr::shared::icu_invariant_failure(const char* what)
{
    if (inside_parallel_body())
        throw Failure{what};
    Rf_error("%s", what);
}

#if defined(BAD_HANDLER_RETURNS)
CHARR_ICU_FATAL_HANDLER void returning_handler(const char* what)
{
    (void)what;
}
#endif

#if defined(BAD_HANDLER_NOEXCEPT)
[[noreturn]] CHARR_ICU_FATAL_HANDLER void noexcept_handler(
    const char* what
) noexcept
{
    Rf_error("%s", what);
}
#endif
