#include "reader-support.h"

#include <utility>
#include <vector>

template<typename Fn>
CHARR_TRUSTED_UNWIND int test_unwind(Fn&& fn)
{
    return fn();
}

CHARR_ENTRYPOINT int bad_entrypoint() noexcept
{
    try {
        std::vector<charport::Reader> readers;
        return test_unwind([&]() -> int {
            readers.resize(1);
            return readers[0].size();
        });
    }
    catch (...) {
        return -1;
    }
}
