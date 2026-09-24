#include "external-callback-support.h"
#include "../../../src/shared/lint.h"

#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif
#include <Rinternals.h>

CHARR_R_HELPER void touches_r() noexcept
{
    Rf_error("from callback");
}

CHARR_R_HELPER void r_registers_callback() noexcept
{
    run_callback(&touches_r);
}

#if defined(BAD_ESCAPED_POINTER)
CHARR_NEUTRAL_HELPER void neutral_callback() noexcept
{
}

CHARR_R_HELPER void r_stores_callback() noexcept
{
    void (*stored)() = &neutral_callback;
    run_callback(stored);
}
#endif

#if defined(BAD_LAMBDA_CALLBACK)
CHARR_R_HELPER void r_passes_lambda() noexcept
{
    run_functor([]() noexcept {
        Rf_error("from lambda");
    });
}
#endif
