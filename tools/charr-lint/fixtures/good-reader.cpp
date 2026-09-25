#include "protection-support.h"
#include "reader-support.h"

CHARR_ENTRYPOINT SEXP entrypoint(int input) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();
    try {
        charport::Reader reader;
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                reader.reset(input);
                const int size = reader.size();
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
