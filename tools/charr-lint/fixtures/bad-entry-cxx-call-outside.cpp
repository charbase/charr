#include "protection-support.h"

CHARR_CXX_HELPER int potentially_throwing()
{
    return 1;
}

CHARR_ENTRYPOINT SEXP bad_entrypoint() noexcept
{
    CHARR_ENTRYPOINT_BEGIN();
    const int value = potentially_throwing();
    try {
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
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
