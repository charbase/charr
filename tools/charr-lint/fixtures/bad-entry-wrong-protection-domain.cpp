#include "protection-support.h"

CHARR_ENTRYPOINT SEXP wrong_protection_domain(SEXP input) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();
    entry_protections.protect_one(input);

    try {
        lint_fixture::Owner owner;
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                entry_protections.protect_one(input);
                result = entry_protections.reprotect_one(
                    Rf_allocVector(INTSXP, 1), result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}
