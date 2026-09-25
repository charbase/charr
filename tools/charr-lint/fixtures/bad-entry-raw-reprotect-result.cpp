#include "protection-support.h"

// Only CHARR_UNWIND_KEEP_RESULT() may re-protect the result slot directly.
CHARR_ENTRYPOINT SEXP bad_raw_reprotect_result(SEXP input) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    try {
        lint_fixture::Owner owner;
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                result = entry_protections.reprotect_one(
                    Rf_allocVector(INTSXP, 1), result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
        R_Reprotect(result, result_index);
    }
    CHARR_ENTRYPOINT_END();
}
