#include "../../../src/shared/lint.h"

#include <string>
#include <utility>

CHARR_CXX_HELPER std::string make_owner()
{
    return std::string("value");
}

CHARR_R_HELPER int r_consume(const std::string&) noexcept
{
    return 0;
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
            return r_consume(make_owner());
        });
    }
    catch (...) {
        return -1;
    }
}
