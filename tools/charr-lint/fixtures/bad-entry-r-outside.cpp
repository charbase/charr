#include "../../../src/shared/lint.h"

#include <string>
#include <utility>

CHARR_R_HELPER int r_value() noexcept
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
    try {
        std::string owner;
        const int value = r_value();
        return test_unwind([&]() -> int {
            return value + static_cast<int>(owner.size());
        });
    }
    catch (...) {
        return -1;
    }
}
