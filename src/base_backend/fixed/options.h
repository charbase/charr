#ifndef CHARR_BASE_FIXED_OPTIONS_H
#define CHARR_BASE_FIXED_OPTIONS_H

#include "../ci_external.h"
#include "../../shared/fixed_search.h"

namespace charr {
namespace base_backend {
namespace fixed {

CHARR_R_HELPER shared::FixedSearchOptions prepare_options(
    SEXP input, bool allow_overlap = false
) noexcept;

} // namespace fixed
} // namespace base_backend
} // namespace charr

#endif
