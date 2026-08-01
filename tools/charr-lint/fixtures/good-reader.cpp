#include "reader-support.h"

#include <utility>

template<typename Fn>
CHARR_TRUSTED_UNWIND int test_unwind(Fn&& fn)
{
    return fn();
}

CHARR_ENTRYPOINT int entrypoint(int input) noexcept
{
    try {
        charport::Reader reader;
        return test_unwind([&]() -> int {
            reader.reset(input);
            return reader.size();
        });
    }
    catch (...) {
        return -1;
    }
}
