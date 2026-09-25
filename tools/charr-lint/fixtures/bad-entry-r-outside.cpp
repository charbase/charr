#include "protection-support.h"

#include <string>

CHARR_R_HELPER int r_value() noexcept
{
    return 1;
}

CHARR_ENTRYPOINT SEXP bad_entrypoint() noexcept
{
    CHARR_ENTRYPOINT_BEGIN();
    try {
        std::string owner;
        const int value = r_value();
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const int size = value + static_cast<int>(owner.size());
                result = entry_protections.reprotect_one(
                    Rf_ScalarInteger(size), result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
        CHARR_UNWIND_KEEP_RESULT();
    }
    CHARR_ENTRYPOINT_END();
}
