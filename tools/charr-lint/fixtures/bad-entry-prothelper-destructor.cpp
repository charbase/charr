#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif
#include <Rinternals.h>

#include "../../../src/shared/lint.h"

#include <utility>

namespace charr {
namespace shared {

// A counter with a destructor would run cleanup during C++ unwinding.
class ProtHelper {
public:
    CHARR_NEUTRAL_HELPER ProtHelper() noexcept = default;
    CHARR_NEUTRAL_HELPER ~ProtHelper() noexcept {}
};

template<typename Fn>
CHARR_TRUSTED_UNWIND SEXP unwind_protect(SEXP token, Fn&& fn)
{
    (void)token;
    return fn();
}

} // namespace shared
} // namespace charr

CHARR_ENTRYPOINT SEXP entrypoint(SEXP input) noexcept
{
    charr::shared::ProtHelper entry_protections;
    charr::shared::ProtHelper callback_protections;
    SEXP result = R_NilValue;
    try {
        result = charr::shared::unwind_protect(input, [&]() -> SEXP {
            return input;
        });
    }
    catch (...) {
        return R_NilValue;
    }
    return result;
}
