#ifndef CHARR_BASE_ENTRYPOINTS_H
#define CHARR_BASE_ENTRYPOINTS_H

#include "../shared/backend_methods.h"

namespace charr {
namespace base_backend {
namespace entrypoints {

#define CHARR_BASE_DECLARE(name, arity) \
    extern "C" SEXP C_charr_base_##name(CHARR_BACKEND_FORMALS_##arity);
CHARR_BACKEND_METHODS(CHARR_BASE_DECLARE)
#undef CHARR_BASE_DECLARE

} // namespace entrypoints
} // namespace base_backend
} // namespace charr

#endif
