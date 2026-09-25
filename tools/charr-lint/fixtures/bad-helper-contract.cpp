#include "protection-support.h"

#include <csetjmp>

CHARR_ENTRYPOINT SEXP entry_target() noexcept
{
    CHARR_ENTRYPOINT_BEGIN();
    try {
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                result = entry_protections.reprotect_one(
                    R_NilValue, result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
        CHARR_UNWIND_KEEP_RESULT();
    }
    CHARR_ENTRYPOINT_END();
}

#if defined(BAD_R_THROW)
CHARR_R_HELPER int r_throw() noexcept
{
    throw 1;
}
#endif

#if defined(BAD_NEUTRAL_THROW)
CHARR_NEUTRAL_HELPER int neutral_throw() noexcept
{
    throw 1;
}
#endif

#if defined(BAD_R_DELETE)
CHARR_R_HELPER void r_delete(int* value) noexcept
{
    delete value;
}
#endif

#if defined(BAD_NEUTRAL_DELETE)
CHARR_NEUTRAL_HELPER void neutral_delete(int* value) noexcept
{
    delete value;
}
#endif

#if defined(BAD_R_MAY_THROW)
CHARR_R_HELPER int r_may_throw()
{
    return 0;
}
#endif

#if defined(BAD_NEUTRAL_MAY_THROW)
CHARR_NEUTRAL_HELPER int neutral_may_throw()
{
    return 0;
}
#endif

#if defined(BAD_ENTRY_MAY_THROW)
CHARR_ENTRYPOINT SEXP entry_may_throw()
{
    CHARR_ENTRYPOINT_BEGIN();
    try {
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                result = entry_protections.reprotect_one(
                    R_NilValue, result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
        CHARR_UNWIND_KEEP_RESULT();
    }
    CHARR_ENTRYPOINT_END();
}
#endif

#if defined(BAD_CONFLICTING_ROLES)
CHARR_R_HELPER int conflicting() noexcept;

CHARR_CXX_HELPER int conflicting() noexcept
{
    return 0;
}
#endif

#if defined(BAD_CXX_CALLS_ENTRY)
CHARR_CXX_HELPER SEXP cxx_calls_entry()
{
    return entry_target();
}
#endif

#if defined(BAD_NEUTRAL_CALLS_ENTRY)
CHARR_NEUTRAL_HELPER SEXP neutral_calls_entry() noexcept
{
    return entry_target();
}
#endif

#if defined(BAD_UNWIND_FROM_HELPER)
CHARR_CXX_HELPER SEXP cxx_calls_unwind(SEXP token)
{
    return charr::shared::unwind_protect(token, [&]() -> SEXP {
        return R_NilValue;
    });
}
#endif

#if defined(BAD_ENTRY_NO_UNWIND)
CHARR_ENTRYPOINT SEXP entry_no_unwind() noexcept
{
    return R_NilValue;
}
#endif

#if defined(BAD_ENTRY_TWO_UNWINDS)
CHARR_ENTRYPOINT SEXP entry_two_unwinds() noexcept
{
    CHARR_ENTRYPOINT_BEGIN();
    try {
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                result = entry_protections.reprotect_one(
                    R_NilValue, result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
        CHARR_UNWIND_KEEP_RESULT();
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                result = entry_protections.reprotect_one(
                    R_NilValue, result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}
#endif

#if defined(BAD_ENTRY_NOT_SEXP)
CHARR_ENTRYPOINT int entry_not_sexp() noexcept
{
    return 0;
}
#endif

#if defined(BAD_TRUSTED_OUTSIDE_UNWIND_H)
CHARR_TRUSTED_UNWIND SEXP local_unwind(SEXP token) noexcept
{
    return token;
}
#endif

#if defined(BAD_NEUTRAL_LONGJMP)
CHARR_NEUTRAL_HELPER void neutral_longjmp(std::jmp_buf& buffer) noexcept
{
    std::longjmp(buffer, 1);
}
#endif

#if defined(BAD_CXX_SETJMP)
CHARR_CXX_HELPER int cxx_setjmp(std::jmp_buf& buffer)
{
    return setjmp(buffer);
}
#endif

#if defined(BAD_NONLOCAL_LAMBDA_CALL)
// A lambda defined outside the caller is not checked with the caller's body.
const auto nonlocal_lambda = [](int value) {
    return value + 1;
};

CHARR_CXX_HELPER int calls_nonlocal_lambda()
{
    return nonlocal_lambda(1);
}
#endif
