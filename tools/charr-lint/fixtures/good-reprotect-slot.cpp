#include "protection-support.h"

CHARR_ENTRYPOINT SEXP reusable_protection_slot(SEXP input) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();
    entry_protections.protect_one(input);

    try {
        lint_fixture::Owner owner;

        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                SEXP child = R_NilValue;
                PROTECT_INDEX child_index;
                callback_protections.protect_with_index(child, &child_index);

                result = entry_protections.reprotect_one(
                    Rf_allocVector(VECSXP, 1), result_index
                );
                child = callback_protections.reprotect_slot(
                    Rf_allocVector(INTSXP, 1), child_index
                );
                SET_VECTOR_ELT(result, 0, child);

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}
