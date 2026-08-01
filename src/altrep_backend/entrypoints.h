#ifndef CHARR_ALTREP_ENTRYPOINTS_H
#define CHARR_ALTREP_ENTRYPOINTS_H

#include "../shared/backend_methods.h"
#include "../shared/lint.h"

namespace charr {
namespace altrep_backend {
namespace entrypoints {

#define CHARR_ALTREP_DECLARE(name, arity) \
    extern "C" CHARR_ABI_SHIM SEXP C_charr_altrep_##name( \
        CHARR_BACKEND_FORMALS_##arity \
    ) noexcept;
CHARR_BACKEND_METHODS(CHARR_ALTREP_DECLARE)
#undef CHARR_ALTREP_DECLARE

} // namespace entrypoints
} // namespace altrep_backend
} // namespace charr

#endif
