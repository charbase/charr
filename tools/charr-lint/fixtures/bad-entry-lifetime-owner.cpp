#include "../../../src/shared/lint.h"

#include <string>
#include <utility>

CHARR_CXX_HELPER std::string make_owner()
{
    return std::string("value");
}

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
        return test_unwind([&]() -> int {
            const std::string& owner = make_owner();
            return static_cast<int>(owner.size()) + r_value();
        });
    }
    catch (...) {
        return -1;
    }
}
