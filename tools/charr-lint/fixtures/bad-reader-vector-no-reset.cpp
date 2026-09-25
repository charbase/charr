#include "protection-support.h"
#include "reader-support.h"

#include <vector>

CHARR_ENTRYPOINT SEXP bad_entrypoint() noexcept
{
    CHARR_ENTRYPOINT_BEGIN();
    try {
        std::vector<charport::Reader> readers;
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                readers.resize(1);
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
