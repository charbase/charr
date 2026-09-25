#include "protection-support.h"

CHARR_ENTRYPOINT SEXP bad_entrypoint(bool fail) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();
    if (fail)
        throw 1;
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
    }
    CHARR_ENTRYPOINT_END();
}
