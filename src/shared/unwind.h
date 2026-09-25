#ifndef CHARR_SHARED_UNWIND_H
#define CHARR_SHARED_UNWIND_H

#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif
#include <Rinternals.h>

#include "lint.h"

#include <csetjmp>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <type_traits>

namespace charr {
namespace shared {

struct RUnwind {
    SEXP token;
};

/*
 * Base of each backend's StriException, so shared code can read its message
 * without naming the backend type. It is deliberately not std::exception:
 * code that catches std::exception keeps its current behaviour.
 */
class ReportedError {
public:
    CHARR_NEUTRAL_HELPER virtual const char* message() const noexcept = 0;

protected:
    CHARR_NEUTRAL_HELPER ReportedError() noexcept = default;
    CHARR_NEUTRAL_HELPER ReportedError(const ReportedError&) noexcept = default;
    CHARR_NEUTRAL_HELPER ReportedError& operator=(
        const ReportedError&
    ) noexcept = default;
    CHARR_NEUTRAL_HELPER ~ReportedError() = default;
};

/*
 * A captured failure rethrown by value; its message becomes the R error.
 */
class CapturedFailure : public ReportedError {
public:
    static constexpr std::size_t message_size = 4096;

    CHARR_NEUTRAL_HELPER explicit CapturedFailure(const char* message) noexcept
    {
        std::snprintf(message_, message_size, "%s", message);
    }

    CHARR_NEUTRAL_HELPER const char* message() const noexcept override
    {
        return message_;
    }

private:
    char message_[message_size];
};

/*
 * The current exception, described by value so it can be raised again
 * later. This replaces std::exception_ptr. When a package built on libc++
 * shares a process with libstdc++ (R linked to a system ICU built on
 * libstdc++, as on R-hub's clang images), the ordinary throw and catch
 * entry points resolve to libstdc++, while exception_ptr's capture and
 * rethrow exist only in libc++abi; re-raising through exception_ptr then
 * corrupts the heap. Classifying with `throw;` stays within one runtime.
 */
class CapturedError {
public:
    CHARR_NEUTRAL_HELPER CapturedError() noexcept = default;

    // Call only while handling an exception, e.g. in `catch (...)`.
    CHARR_CXX_HELPER void capture_current() noexcept
    {
        try {
            throw;
        }
        catch (const RUnwind& error) {
            kind_ = Kind::r_unwind;
            token_ = error.token;
        }
        catch (const ReportedError& error) {
            set_failure(error.message());
        }
        catch (const std::exception& error) {
            set_failure(error.what());
        }
        catch (...) {
            set_failure("unknown C++ exception");
        }
    }

    CHARR_NEUTRAL_HELPER bool captured() const noexcept
    {
        return kind_ != Kind::none;
    }

    [[noreturn]] CHARR_CXX_HELPER void rethrow() const
    {
        if (kind_ == Kind::r_unwind)
            throw RUnwind{token_};
        throw CapturedFailure(message_);
    }

private:
    enum class Kind { none, r_unwind, failure };

    CHARR_NEUTRAL_HELPER void set_failure(const char* message) noexcept
    {
        kind_ = Kind::failure;
        std::snprintf(
            message_, CapturedFailure::message_size, "%s",
            message != nullptr && message[0] != '\0'
                ? message : "C++ exception"
        );
    }

    Kind kind_ = Kind::none;
    SEXP token_ = nullptr;
    char message_[CapturedFailure::message_size] = {};
};

namespace unwind_detail {

struct JumpBuffer {
    std::jmp_buf value;
};

template<typename Fn>
struct CallState {
    Fn* fn;
    CapturedError error;
};

template<typename Fn>
CHARR_TRUSTED_UNWIND SEXP call_body(void* data) noexcept
{
    CallState<Fn>* state = static_cast<CallState<Fn>*>(data);
    try {
        return (*state->fn)();
    }
    catch (...) {
        state->error.capture_current();
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

    unwind_detail::CallState<fun_type> state{&fn, CapturedError()};
    unwind_detail::JumpBuffer jump;

    if (setjmp(jump.value) != 0)
        throw RUnwind{token};

    SEXP result = R_UnwindProtect(
        &unwind_detail::call_body<fun_type>, &state,
        &unwind_detail::call_cleanup, &jump, token
    );

    SETCAR(token, R_NilValue);
    if (state.error.captured())
        state.error.rethrow();
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
