#include "protection-support.h"
#include "reader-support.h"

struct Holder {
    charport::Reader reader;

    CHARR_NEUTRAL_HELPER Holder() noexcept = default;
};

CHARR_ENTRYPOINT SEXP entrypoint(int input) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();
    try {
        (void)input;
        Holder holder;
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const int size = holder.reader.size();
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
