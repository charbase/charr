#ifndef CHARR_SHARED_ENTRYPOINT_H
#define CHARR_SHARED_ENTRYPOINT_H

#include "lint.h"
#include "protect.h"
#include "unwind.h"

#include <cstdio>
#include <type_traits>

namespace charr {
namespace shared {

class EntryErrorState {
public:
    static constexpr int message_size = 4096;

private:
    bool r_error_ = false;
    bool cpp_error_ = false;
    char message_[message_size] = {};

public:
    CHARR_NEUTRAL_HELPER EntryErrorState() noexcept = default;

    CHARR_NEUTRAL_HELPER void capture_r_error() noexcept
    {
        r_error_ = true;
    }

    CHARR_NEUTRAL_HELPER void capture_cpp_error(
        const char* message
    ) noexcept {
        cpp_error_ = true;
        const char* text = message != nullptr && message[0] != '\0'
            ? message
            : "C++ exception";
        std::snprintf(message_, message_size, "%s", text);
    }

    CHARR_NEUTRAL_HELPER void capture_unknown_cpp_error() noexcept
    {
        capture_cpp_error("unknown C++ exception");
    }

    CHARR_NEUTRAL_HELPER bool has_r_error() const noexcept
    {
        return r_error_;
    }

    CHARR_NEUTRAL_HELPER bool has_cpp_error() const noexcept
    {
        return cpp_error_;
    }

    CHARR_NEUTRAL_HELPER const char* message() const noexcept
    {
        return message_;
    }
};

static_assert(
    std::is_trivially_destructible<EntryErrorState>::value,
    "EntryErrorState must remain trivially destructible"
);

} // namespace shared
} // namespace charr

/*
 * Entry protections predate the R_UnwindProtect callback. Callback
 * protections are the stack delta created while that callback is running.
 * Both counters are trivial. Native return paths release them explicitly;
 * the R-error path lets R restore the protection stack.
 */
#define CHARR_ENTRYPOINT_BEGIN()                                      \
    ::charr::shared::ProtHelper entry_protections;                    \
    SEXP unwind_token = entry_protections.protect_one(                \
        R_MakeUnwindCont()                                            \
    );                                                                \
    SEXP result = R_NilValue;                                         \
    PROTECT_INDEX result_index;                                       \
    entry_protections.protect_with_index(result, &result_index);      \
    ::charr::shared::ProtHelper callback_protections;                 \
    ::charr::shared::EntryErrorState error_state

#define CHARR_UNWIND_RETURN()                                         \
    do {                                                              \
        callback_protections.release_all();                           \
        return result;                                                \
    } while (false)

#define CHARR_ENTRYPOINT_RUN_POSTLUDE(...)                            \
    do {                                                              \
        __VA_ARGS__                                                   \
    } while (false)

/*
 * The postlude may call R after all Frame owners have been destroyed. It
 * runs on normal return and before a captured C++ exception becomes an R
 * error. A pending R error is always continued first.
 */
#define CHARR_ENTRYPOINT_END(...)                                     \
    catch (const ::charr::shared::RUnwind&) {                         \
        error_state.capture_r_error();                                \
    }                                                                 \
    catch (const StriException& error) {                              \
        error_state.capture_cpp_error(error.getMessage());            \
    }                                                                 \
    catch (const ::std::exception& error) {                           \
        error_state.capture_cpp_error(error.what());                  \
    }                                                                 \
    catch (...) {                                                     \
        error_state.capture_unknown_cpp_error();                      \
    }                                                                 \
                                                                      \
    if (error_state.has_r_error())                                    \
        ::charr::shared::continue_r_unwind(unwind_token);             \
                                                                      \
    if (error_state.has_cpp_error()) {                                \
        callback_protections.release_all();                           \
        CHARR_ENTRYPOINT_RUN_POSTLUDE(__VA_ARGS__);                   \
        entry_protections.release_all();                              \
        ::Rf_error("%s", error_state.message());                      \
    }                                                                 \
                                                                      \
    CHARR_ENTRYPOINT_RUN_POSTLUDE(__VA_ARGS__);                       \
    entry_protections.release_all();                                  \
    return result

#endif
