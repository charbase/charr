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

#if defined(BAD_SHIM_LINKAGE)
CHARR_ABI_SHIM SEXP C_abi_target(SEXP input) noexcept
{
    return abi_target(input);
}
#elif defined(BAD_SHIM_NOEXCEPT)
extern "C" CHARR_ABI_SHIM SEXP C_abi_target(SEXP input)
{
    return abi_target(input);
}
#elif defined(BAD_SHIM_PARAMETER)
extern "C" CHARR_ABI_SHIM SEXP C_abi_target(SEXP input, int flag) noexcept
{
    return abi_target(input);
}
#elif defined(BAD_SHIM_RETURN)
extern "C" CHARR_ABI_SHIM void* C_abi_target(SEXP input) noexcept
{
    return abi_target(input);
}
#else
extern "C" CHARR_ABI_SHIM SEXP C_abi_target(SEXP input) noexcept
{
    return abi_target(input);
}
#endif
