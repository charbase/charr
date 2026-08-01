#ifndef CHARR_BASE_COLLATOR_OPTIONS_H
#define CHARR_BASE_COLLATOR_OPTIONS_H

#include "../ci_external.h"
#include "../ci_macros.h"
#include "../../shared/collator.h"

namespace charr {
namespace base_backend {
namespace collator {

CHARR_R_HELPER shared::CollatorOptions prepare_options(
    SEXP options
) noexcept;

} // namespace collator
} // namespace base_backend
} // namespace charr

#endif
