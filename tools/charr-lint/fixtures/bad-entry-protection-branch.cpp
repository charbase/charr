#include "protection-support.h"

#include <exception>

#if defined(BAD_HELPER_NAME)
#define callback_protections callback_domain
#endif

CHARR_ENTRYPOINT SEXP bad_protection_branch(SEXP input) noexcept
{
    charr::shared::ProtHelper entry_protections;
    entry_protections.protect_one(input);
    SEXP unwind_token = entry_protections.protect_one(R_MakeUnwindCont());

    SEXP result = R_NilValue;
    PROTECT_INDEX result_index;
    entry_protections.protect_with_index(result, &result_index);

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
#if defined(BAD_CALLBACK_CLEAR)
                callback_protections.clear();
#endif
                callback_protections.release_all();
                return result;
            }
        );
#if defined(BAD_PRELUDE_UNPROTECT)
        UNPROTECT(1);
#endif
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

#if defined(BAD_BEFORE_R_RELEASE)
    entry_protections.release_all();
#endif

    if (error_state.has_r_error()) {
#if defined(BAD_R_ERROR_RELEASE)
        entry_protections.release_all();
#endif
#if defined(BAD_R_ERROR_OTHER_R_CALL)
        (void)Rf_allocVector(INTSXP, 1);
#endif
#if defined(BAD_R_ERROR_TOKEN)
        charr::shared::continue_r_unwind(input);
#else
        charr::shared::continue_r_unwind(unwind_token);
#endif
    }

    if (error_state.has_cpp_error()) {
#if !defined(BAD_CPP_MISSING_RELEASE)
        callback_protections.release_all();
#endif
#if defined(BAD_CPP_DEPTH)
        UNPROTECT(1);
#elif !defined(BAD_CPP_MISSING_ENTRY_RELEASE)
        entry_protections.release_all();
#endif
        Rf_error("%s", error_state.message());
    }

#if defined(BAD_EXTRA_RETURN)
    if (input == R_NilValue) {
        entry_protections.release_all();
        return result;
    }
#endif

#if defined(BAD_SUCCESS_DEPTH)
    UNPROTECT(1);
#else
    entry_protections.release_all();
#endif
#if defined(BAD_POSTLUDE_AFTER_RELEASE)
    (void)Rf_allocVector(INTSXP, 1);
#endif
    return result;
}
