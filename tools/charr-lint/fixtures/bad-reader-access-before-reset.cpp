#include "reader-support.h"

#include <utility>

template<typename Fn>
CHARR_TRUSTED_UNWIND int test_unwind(Fn&& fn)
{
    return fn();
}

CHARR_ENTRYPOINT int bad_entrypoint(int input) noexcept
{
    try {
        charport::Reader reader;
        return test_unwind([&]() -> int {
            const int size = reader.size();
            reader.reset(input);
            return size;
        });
    }
    catch (...) {
        return -1;
    }
}
