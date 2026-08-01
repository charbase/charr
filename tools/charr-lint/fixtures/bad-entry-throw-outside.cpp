#include "../../../src/shared/lint.h"

#include <utility>

template<typename Fn>
CHARR_TRUSTED_UNWIND int test_unwind(Fn&& fn)
{
    return fn();
}

CHARR_ENTRYPOINT int bad_entrypoint() noexcept
{
    try {
        return test_unwind([&]() -> int {
            return 1;
        });
    }
    catch (...) {
        return -1;
    }
    throw 1;
}
