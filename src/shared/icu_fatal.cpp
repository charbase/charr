#include "icu_fatal.h"

#include "parallel.h"
#include "unwind.h"

#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif
#include <R_ext/Error.h>

/*
 * Compiled in both ICU modes so the lint frontier and the object lists stay
 * the same. With a system ICU nothing refers to the handler: that ICU keeps
 * its own abort().
 */

namespace charr {
namespace shared {
namespace {

// Carries the message across the parallel driver, which records a failing
// worker through describe_error() and raises the text on the main thread.
// `what` is one of the string literals in src/uconfig_local.h, so holding
// the pointer is enough.
class IcuInvariantFailure : public ReportedError {
public:
    CHARR_NEUTRAL_HELPER explicit IcuInvariantFailure(
        const char* what
    ) noexcept
        : what_(what)
    {
    }

    CHARR_NEUTRAL_HELPER const char* message() const noexcept override
    {
        return what_;
    }

private:
    const char* what_;
};

} // namespace


/*
 * An R error unwinds with longjmp. On a worker thread that jump would leave
 * the thread; on the main thread's own lane it would skip the join and
 * destroy the Frame while workers still read it. Inside a parallel body the
 * failure is therefore a C++ exception, which the driver already carries to
 * the main thread. Outside one, the main thread is the R thread and the R
 * error is what stringi raises for the same site.
 */
[[noreturn]] CHARR_ICU_FATAL_HANDLER void icu_invariant_failure(
    const char* what
)
{
    if (what == nullptr)
        what = "ICU internal error";
    if (running_parallel_body())
        throw IcuInvariantFailure(what);
    Rf_error("%s", what);
}

} // namespace shared
} // namespace charr
