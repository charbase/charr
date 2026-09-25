#include "protection-support.h"

#include <string>

CHARR_ENTRYPOINT SEXP bad_entrypoint() noexcept
{
    CHARR_ENTRYPOINT_BEGIN();
    try {
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                result = entry_protections.reprotect_one(
                    R_NilValue, result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
        CHARR_UNWIND_KEEP_RESULT();
        std::string late_owner;
        (void)late_owner.size();
    }
    CHARR_ENTRYPOINT_END();
}
