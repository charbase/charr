#ifndef CHARR_LINT_FIXTURE_BAD_RELATIVE_HEADER_H
#define CHARR_LINT_FIXTURE_BAD_RELATIVE_HEADER_H

// Reached as "sub/../shared/bad-relative-header.h" from a compilation
// database entry whose source path is relative to its directory, as bear
// records the package build. The header is charr-owned through its real path.

#include "../../../../../../src/shared/lint.h"

CHARR_R_HELPER inline int relative_r_value() noexcept
{
    return 1;
}

CHARR_CXX_HELPER inline int relative_cxx_helper()
{
    return relative_r_value();
}

#endif
