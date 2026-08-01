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
    try {
        return test_unwind([&]() -> int {
            std::string owner("value");
            return static_cast<int>(owner.size());
        });
    }
    catch (...) {
        return -1;
    }
}
