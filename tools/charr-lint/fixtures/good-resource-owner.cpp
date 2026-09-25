#include "protection-support.h"
#include "resource-support.h"

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

CHARR_ENTRYPOINT SEXP entrypoint() noexcept
{
    CHARR_ENTRYPOINT_BEGIN();
    try {
        ResourceOwner resource;
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const int valid = resource.valid() ? 1 : 0;
                result = entry_protections.reprotect_one(
                    Rf_ScalarInteger(valid), result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
        CHARR_UNWIND_KEEP_RESULT();
    }
    CHARR_ENTRYPOINT_END();
}
