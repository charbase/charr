#include "protection-support.h"
#include "reader-support.h"

CHARR_ENTRYPOINT SEXP bad_entrypoint(int input) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();
    try {
        charport::Reader reader;
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const int size = reader.size();
                reader.reset(input);
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
