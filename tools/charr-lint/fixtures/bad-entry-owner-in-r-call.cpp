#include "protection-support.h"

#include <string>

CHARR_CXX_HELPER std::string make_owner()
{
    std::string owner;
    owner = "value";
    return owner;
}

CHARR_R_HELPER int r_consume(const std::string&) noexcept
{
    return 0;
}

CHARR_ENTRYPOINT SEXP bad_entrypoint() noexcept
{
    CHARR_ENTRYPOINT_BEGIN();
    try {
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                (void)r_consume(make_owner());
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
