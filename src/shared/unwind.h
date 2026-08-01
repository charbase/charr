#ifndef CHARR_SHARED_UNWIND_H
#define CHARR_SHARED_UNWIND_H

#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif
#include <Rinternals.h>

#include "lint.h"

#include <csetjmp>
#include <exception>
#include <type_traits>

namespace charr {
namespace shared {

struct RUnwind {
    SEXP token;
};

namespace unwind_detail {

struct JumpBuffer {
    std::jmp_buf value;
};

template<typename Fn>
struct CallState {
    Fn* fn;
    std::exception_ptr error;
};

template<typename Fn>
CHARR_TRUSTED_UNWIND SEXP call_body(void* data) noexcept
{
    CallState<Fn>* state = static_cast<CallState<Fn>*>(data);
    try {
        return (*state->fn)();
    }
    catch (...) {
        state->error = std::current_exception();
        return R_NilValue;
    }
}

CHARR_TRUSTED_UNWIND inline void call_cleanup(
    void* data, Rboolean jump
) noexcept {
    if (jump == TRUE)
        longjmp(static_cast<JumpBuffer*>(data)->value, 1);
}

} // namespace unwind_detail

/*
 * Run the operation's unwind callback using a continuation token created and
 * protected before the lexical owner region begins.
 */
template<typename Fn>
CHARR_TRUSTED_UNWIND SEXP unwind_protect(SEXP token, Fn&& fn)
{
    typedef typename std::remove_reference<Fn>::type fun_type;
    static_assert(
        std::is_trivially_destructible<fun_type>::value,
        "unwind callback must be trivially destructible"
    );

    unwind_detail::CallState<fun_type> state{
        &fn, std::exception_ptr()
    };
    unwind_detail::JumpBuffer jump;

    if (setjmp(jump.value) != 0)
        throw RUnwind{token};

    SEXP result = R_UnwindProtect(
        &unwind_detail::call_body<fun_type>, &state,
        &unwind_detail::call_cleanup, &jump, token
    );

    SETCAR(token, R_NilValue);
    if (state.error)
        std::rethrow_exception(state.error);
    return result;
}

CHARR_R_HELPER [[noreturn]] inline void continue_r_unwind(
    SEXP token
) noexcept {
    R_ContinueUnwind(token);
    Rf_error("charr: failed to continue R error");
}

} // namespace shared
} // namespace charr

#endif
