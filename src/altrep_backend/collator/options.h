#ifndef CHARR_ALTREP_COLLATOR_OPTIONS_H
#define CHARR_ALTREP_COLLATOR_OPTIONS_H

#include "../ci_external.h"
#include "../ci_macros.h"
#include "../../shared/collator.h"

namespace charr {
namespace altrep_backend {
namespace collator {

CHARR_R_HELPER shared::CollatorOptions prepare_options(
    SEXP options
) noexcept;

} // namespace collator
} // namespace altrep_backend
} // namespace charr

#endif
