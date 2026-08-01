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
    try {
        void* resource = raw_open();
        const int result = test_unwind([&]() -> int {
            return resource != nullptr ? 1 : 0;
        });
        raw_close(resource);
        return result;
    }
    catch (...) {
        return -1;
    }
}
