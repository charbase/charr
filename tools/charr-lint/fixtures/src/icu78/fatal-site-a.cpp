// Stands in for an ICU source file. Sites are counted after preprocessing:
// the commented and disabled expansions below are not sites.
#include "fatal-site.h"

namespace icu_78 {

int loop_exit(int value)
{
    for (;;) {
        if (value > 0)
            return value;
        ++value;
    }
    UPRV_UNREACHABLE_EXIT;
}

// UPRV_UNREACHABLE_EXIT;

#if 0
void disabled()
{
    UPRV_UNREACHABLE_EXIT;
}
#endif

int two_sites(int kind)
{
    switch (kind) {
    case 0:
        return 0;
    case 1:
        UPRV_UNREACHABLE_EXIT;
    default:
        UPRV_UNREACHABLE_EXIT;
    }
}

} // namespace icu_78

// A bundled C entry point keeps its name without the renaming suffix.
extern "C" void c_entry_78_charr(int kind)
{
    if (kind != 0)
        UPRV_UNREACHABLE_EXIT;
}

#if defined(BAD_DIRECT_CALL)
void direct_call()
{
    charr::shared::icu_invariant_failure("direct");
}
#endif

#if defined(BAD_NOEXCEPT_SITE)
void noexcept_site() noexcept
{
    UPRV_UNREACHABLE_EXIT;
}
#endif

#if defined(BAD_NOEXCEPT_LAMBDA_SITE)
void noexcept_lambda_site(int kind)
{
    auto check = [kind]() noexcept {
        if (kind != 0)
            UPRV_UNREACHABLE_EXIT;
    };
    check();
}
#endif
