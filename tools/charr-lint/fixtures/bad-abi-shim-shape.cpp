#include "protection-support.h"

CHARR_ENTRYPOINT SEXP bad_abi_target(SEXP input) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();
    try {
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                result = entry_protections.reprotect_one(input, result_index);
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}

extern "C" CHARR_ABI_SHIM SEXP bad_abi_shim(SEXP input) noexcept
{
    SEXP forwarded = input;
    return bad_abi_target(forwarded);
}
