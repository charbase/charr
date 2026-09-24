#include "protection-support.h"

// The shape of ci__prepare_arg_string_1_r: a local counter tracks a
// conditional second push, and error paths leave cleanup to R.
CHARR_R_HELPER SEXP balanced_r(SEXP value, int size) noexcept
{
    PROTECT(value = Rf_allocVector(STRSXP, 1));
    int nprotect = 1;
    if (size <= 0) {
        UNPROTECT(nprotect);
        Rf_error("empty");
    }
    if (size > 1) {
        PROTECT(value = Rf_allocVector(STRSXP, 1));
        ++nprotect;
    }
    if (size > 2)
        Rf_error("still protected; R restores the stack");
    for (int i = 0; i < size; ++i) {
        SEXP item = PROTECT(Rf_allocVector(INTSXP, 1));
        (void)item;
        UNPROTECT(1);
    }
    UNPROTECT(nprotect);
    return value;
}

#if defined(BAD_EXTRA_UNPROTECT)
CHARR_R_HELPER void extra_unprotect_r(SEXP value) noexcept
{
    PROTECT(value);
    UNPROTECT(2);
}
#endif

#if defined(BAD_UNKNOWN_COUNT)
CHARR_R_HELPER void unknown_count_r(SEXP value, int count) noexcept
{
    PROTECT(value);
    UNPROTECT(count);
}
#endif

#if defined(BAD_BRANCH_LEAK)
CHARR_R_HELPER SEXP branch_leak_r(SEXP value, bool copy) noexcept
{
    if (copy)
        PROTECT(value = Rf_duplicate(value));
    return value;
}
#endif

#if defined(BAD_LOOP_GROWTH)
CHARR_R_HELPER void loop_growth_r(int size) noexcept
{
    int count = 0;
    for (int i = 0; i < size; ++i) {
        PROTECT(Rf_allocVector(INTSXP, 1));
        ++count;
    }
    UNPROTECT(count);
}
#endif

#if defined(BAD_LAMBDA_PROTECT)
CHARR_R_HELPER void lambda_protect_r(SEXP value) noexcept
{
    auto keep = [&]() noexcept {
        PROTECT(value);
    };
    keep();
    UNPROTECT(1);
}
#endif

#if defined(BAD_PROTHELPER_LOCAL)
CHARR_R_HELPER void local_domain_r(SEXP value) noexcept
{
    charr::shared::ProtHelper protections;
    protections.protect_one(value);
}
#endif

#if defined(BAD_PRESERVE)
CHARR_R_HELPER void preserve_r(SEXP value) noexcept
{
    R_PreserveObject(value);
}
#endif

#if defined(BAD_NEUTRAL_UNPROTECT)
CHARR_NEUTRAL_HELPER void neutral_unprotect() noexcept
{
    UNPROTECT(1);
}
#endif
