#include "protection-support.h"
#include "resource-support.h"

CHARR_ENTRYPOINT SEXP bad_entrypoint() noexcept
{
    CHARR_ENTRYPOINT_BEGIN();
    try {
        void* resource = raw_open();
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
        raw_close(resource);
    }
    CHARR_ENTRYPOINT_END();
}
