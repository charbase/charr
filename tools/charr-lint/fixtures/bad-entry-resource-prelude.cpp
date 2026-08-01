#include "../../../src/shared/lint.h"
#include "resource-support.h"

#include <utility>

template<typename Fn>
CHARR_TRUSTED_UNWIND int test_unwind(Fn&& fn)
{
    return fn();
}

CHARR_ENTRYPOINT int bad_entrypoint() noexcept
{
    void* resource = raw_open();
    try {
        return test_unwind([&]() -> int {
            return resource != nullptr ? 1 : 0;
        });
    }
    catch (...) {
        return -1;
    }
}
