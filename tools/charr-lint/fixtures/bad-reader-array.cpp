#include "reader-support.h"

#include <array>
#include <utility>

template<typename Fn>
CHARR_TRUSTED_UNWIND int test_unwind(Fn&& fn)
{
    return fn();
}

CHARR_ENTRYPOINT int entrypoint(int input) noexcept
{
    try {
        std::array<charport::Reader, 1> readers;
        return test_unwind([&]() -> int {
            return readers[0].size();
        });
    }
    catch (...) {
        return -1;
    }
}
