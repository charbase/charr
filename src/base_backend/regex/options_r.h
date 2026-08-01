#ifndef CHARR_BASE_REGEX_OPTIONS_R_H
#define CHARR_BASE_REGEX_OPTIONS_R_H

#include "../ci_external.h"
#include "../../shared/regex_search.h"

namespace charr {
namespace base_backend {
namespace regex {

CHARR_R_HELPER shared::RegexOptions prepare_options(SEXP input) noexcept;

} // namespace regex
} // namespace base_backend
} // namespace charr

#endif
