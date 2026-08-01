#include "../../../src/shared/lint.h"
#include "resource-support.h"

#include <utility>

class CHARR_OWNER_TYPE ResourceOwner {
private:
    void* handle_;

public:
    CHARR_CXX_HELPER ResourceOwner() noexcept
        : handle_(raw_open())
    {
    }

    CHARR_CXX_HELPER ~ResourceOwner() noexcept
    {
        raw_close(handle_);
    }

    CHARR_NEUTRAL_HELPER bool valid() const noexcept
    {
        return handle_ != nullptr;
    }

    CHARR_CXX_HELPER void replace() noexcept
    {
        handle_ = raw_replace(handle_, 16);
    }
};

template<typename Fn>
CHARR_TRUSTED_UNWIND int test_unwind(Fn&& fn)
{
    return fn();
}

CHARR_ENTRYPOINT int entrypoint() noexcept
{
    try {
        ResourceOwner resource;
        return test_unwind([&]() -> int {
            return resource.valid() ? 1 : 0;
        });
    }
    catch (...) {
        return -1;
    }
}
