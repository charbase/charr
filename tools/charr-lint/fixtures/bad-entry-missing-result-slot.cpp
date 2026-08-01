#include "protection-support.h"

#include <exception>

CHARR_ENTRYPOINT SEXP bad_missing_result_slot(SEXP input) noexcept
{
    charr::shared::ProtHelper entry_protections;
    entry_protections.protect_one(input);
    SEXP unwind_token = entry_protections.protect_one(R_MakeUnwindCont());

    SEXP result = R_NilValue;
    PROTECT_INDEX result_index;

    charr::shared::ProtHelper callback_protections;
    charr::shared::EntryErrorState error_state;

    try {
        lint_fixture::Owner owner;
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                result = entry_protections.reprotect_one(
                    Rf_allocVector(INTSXP, 1), result_index
                );
                callback_protections.release_all();
                return result;
            }
        );
    }
    catch (const charr::shared::RUnwind&) {
        error_state.capture_r_error();
    }
    catch (const std::exception& error) {
        error_state.capture_cpp_error(error.what());
    }
    catch (...) {
        error_state.capture_unknown_cpp_error();
    }

    if (error_state.has_r_error())
        charr::shared::continue_r_unwind(unwind_token);

    if (error_state.has_cpp_error()) {
        callback_protections.release_all();
        entry_protections.release_all();
        Rf_error("%s", error_state.message());
    }

    entry_protections.release_all();
    return result;
}
