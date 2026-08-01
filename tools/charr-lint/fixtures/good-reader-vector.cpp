#include "reader-support.h"

#include <utility>
#include <vector>

template<typename Fn>
CHARR_TRUSTED_UNWIND int test_unwind(Fn&& fn)
{
    return fn();
}

CHARR_ENTRYPOINT int entrypoint(int input) noexcept
{
    try {
        std::vector<charport::Reader> readers;
        return test_unwind([&]() -> int {
            readers.resize(2);
            int result = 0;
            for (std::size_t i = 0; i < readers.size(); ++i) {
                readers[i].reset(input);
                result += readers[i].size();
            }
            return result;
        });
    }
    catch (...) {
        return -1;
    }
}
