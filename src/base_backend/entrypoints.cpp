#include "entrypoints.h"

#include "ci_exception.h"
#include "ci_exports.h"

namespace charr {
namespace base_backend {
namespace entrypoints {

#define CHARR_BASE_DEFINE(name, arity) \
    extern "C" SEXP C_charr_base_##name(CHARR_BACKEND_FORMALS_##arity) \
    { \
        return charr::base_backend::r_boundary([&]() -> SEXP { \
            return charr::base_backend::name(CHARR_BACKEND_ARGS_##arity); \
        }); \
    }
CHARR_BACKEND_METHODS(CHARR_BASE_DEFINE)
#undef CHARR_BASE_DEFINE

} // namespace entrypoints
} // namespace base_backend
} // namespace charr
