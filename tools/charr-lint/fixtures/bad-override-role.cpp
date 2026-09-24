#include "../../../src/shared/lint.h"

#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif
#include <Rinternals.h>

#include <exception>

struct Base {
    CHARR_CXX_HELPER virtual void run() {}
    CHARR_NEUTRAL_HELPER virtual ~Base() noexcept = default;
};

struct Derived : Base {
    CHARR_R_HELPER void run() noexcept override
    {
        Rf_error("worker reached R");
    }
};

CHARR_CXX_HELPER void dispatch(Base& body)
{
    body.run();
}

#if defined(BAD_INDIRECT_OVERRIDE)
// The intermediate declaration has no body; the concrete override is still
// checked against the original C++ virtual.
struct Middle : Base {
    CHARR_R_HELPER void run() noexcept override = 0;
};

struct Leaf : Middle {
    CHARR_R_HELPER void run() noexcept override
    {
        Rf_error("worker reached R");
    }
};
#endif

#if defined(BAD_CXX_OVER_NEUTRAL)
struct NeutralBase {
    CHARR_NEUTRAL_HELPER virtual int value() const noexcept
    {
        return 0;
    }
    CHARR_NEUTRAL_HELPER virtual ~NeutralBase() noexcept = default;
};

struct CxxOverride : NeutralBase {
    CHARR_CXX_HELPER int value() const noexcept override
    {
        return 1;
    }
};
#endif

#if defined(BAD_EXTERNAL_BASE)
struct Error : std::exception {
    CHARR_R_HELPER const char* what() const noexcept override
    {
        Rf_error("message");
    }
};
#endif
