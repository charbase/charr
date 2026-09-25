#include "protection-support.h"
#include "reader-support.h"

#include <array>

CHARR_ENTRYPOINT SEXP entrypoint(int input) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();
    try {
        std::array<charport::Reader, 1> readers;
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                (void)input;
                const int size = readers[0].size();
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
