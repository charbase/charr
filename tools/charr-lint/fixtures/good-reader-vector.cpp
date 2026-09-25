#include "protection-support.h"
#include "reader-support.h"

#include <vector>

CHARR_ENTRYPOINT SEXP entrypoint(int input) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();
    try {
        std::vector<charport::Reader> readers;
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                readers.resize(2);
                int total = 0;
                for (std::size_t i = 0; i < 2; ++i) {
                    readers[i].reset(input);
                    total += readers[i].size();
                }
                result = entry_protections.reprotect_one(
                    Rf_ScalarInteger(total), result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
        CHARR_UNWIND_KEEP_RESULT();
    }
    CHARR_ENTRYPOINT_END();
}
