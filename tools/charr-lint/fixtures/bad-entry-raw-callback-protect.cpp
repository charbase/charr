#include "protection-support.h"

CHARR_ENTRYPOINT SEXP bad_raw_callback_protect(SEXP input) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    try {
        lint_fixture::Owner owner;
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                PROTECT(input);
                result = entry_protections.reprotect_one(
                    Rf_allocVector(INTSXP, 1), result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}
