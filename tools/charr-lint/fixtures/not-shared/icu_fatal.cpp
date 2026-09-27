// Named like the reviewed file but outside src/shared/: the role's location
// rule matches the directory, not only the file name.
#include "../../../../src/shared/icu_fatal.h"

#include <R_ext/Error.h>

[[noreturn]] CHARR_ICU_FATAL_HANDLER void misplaced_handler(const char* what)
{
    Rf_error("%s", what);
}
