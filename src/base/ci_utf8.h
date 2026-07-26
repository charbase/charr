#ifndef CHARR_BASE_CI_UTF8_H
#define CHARR_BASE_CI_UTF8_H

#include "utf8_input.h"

#include <vector>

namespace charr {
namespace base {

SEXP ci__subset_by_logical(
    const Utf8Input& input, const std::vector<int>& which,
    int result_count
);

} // namespace base
} // namespace charr

#endif
