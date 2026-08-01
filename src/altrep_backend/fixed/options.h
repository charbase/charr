#ifndef CHARR_ALTREP_FIXED_OPTIONS_H
#define CHARR_ALTREP_FIXED_OPTIONS_H

#include "../ci_external.h"
#include "../../shared/fixed_search.h"

namespace charr {
namespace altrep_backend {
namespace fixed {

CHARR_R_HELPER shared::FixedSearchOptions prepare_options(
    SEXP options, bool allow_overlap = false
) noexcept;

} // namespace fixed
} // namespace altrep_backend
} // namespace charr

#endif
