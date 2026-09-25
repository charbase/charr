#include "protection-support.h"
#include "resource-support.h"

CHARR_ENTRYPOINT SEXP bad_entrypoint() noexcept
{
    CHARR_ENTRYPOINT_BEGIN();
    void* resource = raw_open();
    try {
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const int valid = resource != nullptr ? 1 : 0;
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
