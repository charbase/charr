#include "../../../src/shared/lint.h"

#include <utility>

CHARR_CXX_HELPER int potentially_throwing()
{
    return 1;
}

template<typename Fn>
CHARR_TRUSTED_UNWIND int test_unwind(Fn&& fn)
{
    return fn();
}

CHARR_ENTRYPOINT int bad_entrypoint() noexcept
{
    const int value = potentially_throwing();
    try {
        return test_unwind([&]() -> int {
            return value;
        });
    }
    catch (...) {
        return -1;
    }
}
