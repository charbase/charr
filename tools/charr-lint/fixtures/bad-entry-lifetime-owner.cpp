#include "protection-support.h"

#include <string>

CHARR_CXX_HELPER std::string make_owner()
{
    std::string owner;
    owner = "value";
    return owner;
}

CHARR_R_HELPER int r_value() noexcept
{
    return 1;
}

CHARR_ENTRYPOINT SEXP bad_entrypoint() noexcept
{
    CHARR_ENTRYPOINT_BEGIN();
    try {
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const std::string& owner = make_owner();
                const int value = static_cast<int>(owner.size()) + r_value();
                result = entry_protections.reprotect_one(
                    Rf_ScalarInteger(value), result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
        CHARR_UNWIND_KEEP_RESULT();
    }
    CHARR_ENTRYPOINT_END();
}
