#include "protection-support.h"
#include "reader-support.h"

CHARR_CXX_HELPER int read_size(charport::Reader& reader)
{
    return reader.size();
}

CHARR_ENTRYPOINT SEXP entrypoint(int input) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();
    try {
        charport::Reader reader;
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const int early = read_size(reader);
                reader.reset(input);
                const int size = early + reader.size();
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
