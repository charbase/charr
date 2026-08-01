#include "../../../src/shared/lint.h"

#include <string>
#include <utility>

template<typename Fn>
CHARR_TRUSTED_UNWIND int test_unwind(Fn&& fn)
{
    return fn();
}

CHARR_ENTRYPOINT int bad_entrypoint() noexcept
{
    std::string owner;
    try {
        return test_unwind([&]() -> int {
            return 0;
        });
    }
    catch (...) {
        return -1;
    }
}
