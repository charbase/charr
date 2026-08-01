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
        const int result = test_unwind([&]() -> int {
            return 1;
        });
        std::string late_owner;
        return result + static_cast<int>(late_owner.size());
    }
    catch (...) {
        return -1;
    }
}
