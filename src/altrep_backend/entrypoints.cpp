#include "entrypoints.h"

#include "ci_exports.h"

namespace charr {
namespace altrep_backend {
namespace entrypoints {

#define CHARR_ALTREP_DEFINE(name, arity) \
    extern "C" CHARR_ABI_SHIM SEXP C_charr_altrep_##name( \
        CHARR_BACKEND_FORMALS_##arity \
    ) noexcept \
    { \
        return charr::altrep_backend::name(CHARR_BACKEND_ARGS_##arity); \
    }
CHARR_BACKEND_METHODS(CHARR_ALTREP_DEFINE)
#undef CHARR_ALTREP_DEFINE

} // namespace entrypoints
} // namespace altrep_backend
} // namespace charr
