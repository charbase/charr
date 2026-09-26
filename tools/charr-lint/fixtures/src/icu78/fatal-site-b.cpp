// A second translation unit reaching the same header site.
#include "fatal-site.h"

int use_header_site(int value)
{
    return fixture_header_site(value);
}
