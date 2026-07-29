#ifndef CHARR_ALTREP_CI_UNWIND_H
#define CHARR_ALTREP_CI_UNWIND_H

#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif
#include <Rinternals.h>

#include <csetjmp>
#include <exception>
#include <type_traits>
#include <utility>

namespace charr { namespace altrep_backend {

namespace ci {
namespace unwind_detail {

struct RUnwind {
    SEXP token;
};


struct JumpBuffer {
    std::jmp_buf value;
};


template<typename Fn>
struct CallState {
    Fn* fn;
    std::exception_ptr error;
};


template<typename Fn>
SEXP call_body(void* data) noexcept
{
    CallState<Fn>* state = static_cast<CallState<Fn>*>(data);
    try {
        return (*state->fn)();
    }
    catch (...) {
        // Return through R's C frames before rethrowing in C++.
        state->error = std::current_exception();
        return R_NilValue;
    }
}


inline void call_cleanup(void* data, Rboolean jump) noexcept
{
    if (jump == TRUE)
        longjmp(static_cast<JumpBuffer*>(data)->value, 1);
}

} // namespace unwind_detail


/** Run one R API call without letting an R long jump skip caller-owned state. */
template<typename Fn>
SEXP unwind_protect(Fn&& fn)
{
    typedef typename std::remove_reference<Fn>::type fun_type;
    unwind_detail::CallState<fun_type> state{&fn, std::exception_ptr()};
    unwind_detail::JumpBuffer jump;
    SEXP token = PROTECT(R_MakeUnwindCont());

    if (setjmp(jump.value) != 0) {
        R_PreserveObject(token);
        UNPROTECT(1);
        throw unwind_detail::RUnwind{token};
    }

    SEXP out = R_UnwindProtect(
        &unwind_detail::call_body<fun_type>, &state,
        &unwind_detail::call_cleanup, &jump, token
    );

    // R_UnwindProtect temporarily keeps its result in the continuation token.
    SETCAR(token, R_NilValue);
    UNPROTECT(1);
    if (state.error)
        std::rethrow_exception(state.error);
    return out;
}


[[noreturn]] inline void continue_r_unwind(SEXP token)
{
    R_ReleaseObject(token);
    R_ContinueUnwind(token);
    Rf_error("charr ALTREP: failed to continue R error");
}

} // namespace ci


} } // namespace charr::altrep_backend

#endif
