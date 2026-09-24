#include "reader-support.h"

#include <utility>

template<typename Fn>
CHARR_TRUSTED_UNWIND int test_unwind(Fn&& fn)
{
    return fn();
}

CHARR_CXX_HELPER int read_size(charport::Reader& reader)
{
    return reader.size();
}

CHARR_ENTRYPOINT int entrypoint(int input) noexcept
{
    try {
        charport::Reader reader;
        return test_unwind([&]() -> int {
            const int early = read_size(reader);
            reader.reset(input);
            return early + reader.size();
        });
    }
    catch (...) {
        return -1;
    }
}
