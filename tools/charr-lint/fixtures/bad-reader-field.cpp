#include "reader-support.h"

#include <utility>

template<typename Fn>
CHARR_TRUSTED_UNWIND int test_unwind(Fn&& fn)
{
    return fn();
}

struct Holder {
    charport::Reader reader;

    CHARR_NEUTRAL_HELPER Holder() noexcept = default;
};

CHARR_ENTRYPOINT int entrypoint(int input) noexcept
{
    (void)input;
    try {
        Holder holder;
        return test_unwind([&]() -> int {
            return holder.reader.size();
        });
    }
    catch (...) {
        return -1;
    }
}
