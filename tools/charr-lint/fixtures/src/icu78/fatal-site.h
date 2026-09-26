#ifndef CHARR_LINT_FIXTURE_FATAL_SITE_H
#define CHARR_LINT_FIXTURE_FATAL_SITE_H

#include "../uconfig_local.h"

/* A header site counts once, however many translation units include it. */
inline int fixture_header_site(int value)
{
    if (value < 0)
        UPRV_UNREACHABLE_EXIT;
    return value;
}

#endif
