#include "../../../src/shared/lint.h"

#include <utility>

template<typename Fn>
CHARR_TRUSTED_UNWIND int test_unwind(Fn&& fn)
{
    return fn();
}

CHARR_ENTRYPOINT int entry_target() noexcept
{
    try {
        return test_unwind([&]() -> int {
            return 0;
        });
    }
    catch (...) {
        return -1;
    }
}

#if defined(BAD_R_THROW)
CHARR_R_HELPER int r_throw() noexcept
{
    throw 1;
}
#endif

#if defined(BAD_NEUTRAL_THROW)
CHARR_NEUTRAL_HELPER int neutral_throw() noexcept
{
    throw 1;
}
#endif

#if defined(BAD_R_DELETE)
CHARR_R_HELPER void r_delete(int* value) noexcept
{
    delete value;
}
#endif

#if defined(BAD_NEUTRAL_DELETE)
CHARR_NEUTRAL_HELPER void neutral_delete(int* value) noexcept
{
    delete value;
}
#endif

#if defined(BAD_R_MAY_THROW)
CHARR_R_HELPER int r_may_throw()
{
    return 0;
}
#endif

#if defined(BAD_NEUTRAL_MAY_THROW)
CHARR_NEUTRAL_HELPER int neutral_may_throw()
{
    return 0;
}
#endif

#if defined(BAD_ENTRY_MAY_THROW)
CHARR_ENTRYPOINT int entry_may_throw()
{
    try {
        return test_unwind([&]() -> int {
            return 0;
        });
    }
    catch (...) {
        return -1;
    }
}
#endif

#if defined(BAD_CONFLICTING_ROLES)
CHARR_R_HELPER int conflicting() noexcept;

CHARR_CXX_HELPER int conflicting() noexcept
{
    return 0;
}
#endif

#if defined(BAD_CXX_CALLS_ENTRY)
CHARR_CXX_HELPER int cxx_calls_entry()
{
    return entry_target();
}
#endif

#if defined(BAD_NEUTRAL_CALLS_ENTRY)
CHARR_NEUTRAL_HELPER int neutral_calls_entry() noexcept
{
    return entry_target();
}
#endif

#if defined(BAD_UNWIND_FROM_HELPER)
CHARR_CXX_HELPER int cxx_calls_unwind()
{
    return test_unwind([&]() -> int {
        return 0;
    });
}
#endif

#if defined(BAD_ENTRY_NO_UNWIND)
CHARR_ENTRYPOINT int entry_no_unwind() noexcept
{
    return 0;
}
#endif

#if defined(BAD_ENTRY_TWO_UNWINDS)
CHARR_ENTRYPOINT int entry_two_unwinds() noexcept
{
    try {
        const int first = test_unwind([&]() -> int {
            return 0;
        });
        return first + test_unwind([&]() -> int {
            return 1;
        });
    }
    catch (...) {
        return -1;
    }
}
#endif
