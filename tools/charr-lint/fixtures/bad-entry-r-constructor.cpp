#include "protection-support.h"

class CHARR_OWNER_TYPE Owner {
public:
    CHARR_NEUTRAL_HELPER Owner() noexcept = default;
    CHARR_R_HELPER explicit Owner(int) noexcept {}
    CHARR_NEUTRAL_HELPER Owner(Owner&&) noexcept = default;
    CHARR_NEUTRAL_HELPER Owner& operator=(Owner&&) noexcept = default;

private:
    int value_ = 0;
};

CHARR_ENTRYPOINT SEXP bad_entrypoint() noexcept
{
    CHARR_ENTRYPOINT_BEGIN();
    try {
        Owner owner;
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                owner = Owner(1);
                result = entry_protections.reprotect_one(
                    R_NilValue, result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
        CHARR_UNWIND_KEEP_RESULT();
    }
    CHARR_ENTRYPOINT_END();
}
