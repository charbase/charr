#include "protection-support.h"

CHARR_ENTRYPOINT SEXP bad_helper_after_release(SEXP input) noexcept
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
                callback_protections.release_all();
                callback_protections.adopt(1);
                callback_protections.release(1);
                return result;
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}
