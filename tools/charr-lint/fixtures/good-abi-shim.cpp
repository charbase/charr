#include "protection-support.h"

CHARR_ENTRYPOINT SEXP abi_target(SEXP input) noexcept
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

#define DECLARE_SHIM(name) \
    extern "C" CHARR_ABI_SHIM SEXP C_##name(SEXP input) noexcept

DECLARE_SHIM(abi_target)
{
    return abi_target(input);
}
