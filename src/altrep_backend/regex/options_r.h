#ifndef CHARR_ALTREP_REGEX_OPTIONS_R_H
#define CHARR_ALTREP_REGEX_OPTIONS_R_H

#include "../ci_external.h"
#include "../../shared/regex_search.h"

namespace charr {
namespace altrep_backend {
namespace regex {

CHARR_R_HELPER shared::RegexOptions prepare_options(SEXP input) noexcept;

} // namespace regex
} // namespace altrep_backend
} // namespace charr

#endif
