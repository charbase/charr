#ifndef CHARR_SHARED_ICU_FATAL_H
#define CHARR_SHARED_ICU_FATAL_H

/*
 * Included by src/uconfig_local.h, and so by every translation unit of a
 * bundled-ICU build, including ICU's own. Keep it to this one declaration:
 * no R header, no standard header, nothing ICU could collide with.
 */
#include "lint.h"

/*
 * Where a bundled ICU goes when an internal invariant fails. ICU's
 * UPRV_UNREACHABLE_EXIT and double-conversion's DOUBLE_CONVERSION_UNIMPLEMENTED
 * and DOUBLE_CONVERSION_UNREACHABLE expand to a call of this function instead
 * of abort(); see src/uconfig_local.h. ICU marks those sites as unreachable,
 * so no caller handles this, and no charr function may call it.
 *
 * It does not return. Inside a ParallelBody::run, on any lane, it throws a
 * C++ exception carrying `what`, which the parallel driver records and raises
 * on the main thread after the join. Anywhere else it signals an R error
 * with `what`, as stringi does.
 *
 * extern "C++" keeps the C++ linkage should a C API header ever include
 * uconfig.h inside an extern "C" block.
 */
extern "C++" {
namespace charr {
namespace shared {

[[noreturn]] CHARR_ICU_FATAL_HANDLER void icu_invariant_failure(
    const char* what
);

} // namespace shared
} // namespace charr
}

#endif
