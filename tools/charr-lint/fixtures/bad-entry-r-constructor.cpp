#include "../../../src/shared/lint.h"

#include <utility>

class CHARR_OWNER_TYPE Owner {
public:
    CHARR_NEUTRAL_HELPER Owner() noexcept = default;
    CHARR_R_HELPER explicit Owner(int) noexcept {}
    CHARR_NEUTRAL_HELPER Owner(Owner&&) noexcept = default;
    CHARR_NEUTRAL_HELPER Owner& operator=(Owner&&) noexcept = default;

private:
    int value_ = 0;
};

template<typename Fn>
CHARR_TRUSTED_UNWIND int test_unwind(Fn&& fn)
{
    return fn();
}

CHARR_ENTRYPOINT int bad_entrypoint() noexcept
{
    try {
        Owner owner;
        return test_unwind([&]() -> int {
            owner = Owner(1);
            return 0;
        });
    }
    catch (...) {
        return -1;
    }
}
