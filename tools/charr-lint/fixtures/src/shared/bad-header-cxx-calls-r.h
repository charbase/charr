#ifndef CHARR_LINT_FIXTURE_BAD_HEADER_CXX_CALLS_R_H
#define CHARR_LINT_FIXTURE_BAD_HEADER_CXX_CALLS_R_H

#include "../../../../../src/shared/lint.h"

CHARR_R_HELPER inline int header_r_value() noexcept
{
    return 1;
}

CHARR_CXX_HELPER inline int bad_header_cxx_helper()
{
    return header_r_value();
}

#endif
