#include "external-callback-support.h"
#include "../../../src/shared/lint.h"
#include "reader-support.h"

#include <new>

#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif
#include <Rinternals.h>

// A noexcept C++ helper may throw and call throwing code when a catch (...)
// in its own body stops every exception.
CHARR_CXX_HELPER int may_throw()
{
    return 1;
}

CHARR_CXX_HELPER int contained() noexcept
{
    try {
        if (may_throw() < 0)
            throw 1;
        int* value = new int(may_throw());
        const int result = *value;
        delete value;
        return result;
    }
    catch (...) {
        return 0;
    }
}

CHARR_CXX_HELPER int* nothrow_allocation() noexcept
{
    return new (std::nothrow) int(1);
}

// Overrides may keep the base role or narrow it to neutral.
struct Body {
    CHARR_CXX_HELPER virtual int run() = 0;
    CHARR_CXX_HELPER virtual int describe() noexcept
    {
        return 0;
    }
};

struct ThrowingBody : Body {
    CHARR_CXX_HELPER int run() override
    {
        return may_throw();
    }
    CHARR_NEUTRAL_HELPER int describe() noexcept override
    {
        return 1;
    }
};

CHARR_CXX_HELPER int run_body(Body& body)
{
    return body.run() + body.describe();
}

// A callback whose effects the receiving external function permits.
CHARR_NEUTRAL_HELPER void neutral_callback() noexcept
{
}

CHARR_CXX_HELPER void quiet_callback() noexcept
{
}

CHARR_CXX_HELPER void register_callbacks()
{
    run_callback(&neutral_callback);
    run_callback(quiet_callback);
    run_functor([]() noexcept {
        neutral_callback();
    });
}

// Constant initializers and default arguments do not run code.
static const int table[] = {1, 2, 3};
constexpr int table_size = sizeof(table) / sizeof(table[0]);

CHARR_NEUTRAL_HELPER int with_default(int value = table_size) noexcept
{
    return value;
}

// Raw protection balances within the R helper, including a counter for a
// conditional push and an error path that R cleans up.
CHARR_R_HELPER SEXP balanced_r(SEXP value, int size) noexcept
{
    PROTECT(value = Rf_duplicate(value));
    int nprotect = 1;
    if (size <= 0) {
        UNPROTECT(nprotect);
        Rf_error("empty");
    }
    if (size > 1) {
        PROTECT(value = Rf_duplicate(value));
        nprotect += 1;
    }
    UNPROTECT(nprotect);
    return value;
}

// A helper may drop a Reader borrow; it may not read through it.
CHARR_CXX_HELPER void release_reader(charport::Reader& reader)
{
    reader = charport::Reader();
}
