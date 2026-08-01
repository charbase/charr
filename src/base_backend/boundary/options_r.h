#ifndef CHARR_BASE_BOUNDARY_OPTIONS_R_H
#define CHARR_BASE_BOUNDARY_OPTIONS_R_H

#include "../ci_external.h"
#include "../../shared/boundary_iterator.h"

namespace charr {
namespace base_backend {
namespace boundary {

CHARR_R_HELPER shared::BoundaryOptions prepare_options_r(
    SEXP options, UBreakIteratorType default_type
) noexcept;

} // namespace boundary
} // namespace base_backend
} // namespace charr

#endif
