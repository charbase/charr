#include "../../../src/shared/lint.h"

#include <exception>
#include <vector>

CHARR_CXX_HELPER int grow() noexcept
{
    std::vector<int> values;
    values.push_back(1);
    if (values.empty())
        throw 1;
    return static_cast<int>(values.size());
}

CHARR_R_HELPER int r_calls_noexcept_cxx() noexcept
{
    return grow();
}

#if defined(BAD_THROWING_HELPER)
CHARR_CXX_HELPER int may_throw()
{
    return 1;
}

// A catch clause that names a type does not stop every exception.
CHARR_CXX_HELPER int calls_may_throw() noexcept
{
    try {
        return may_throw();
    }
    catch (const std::exception&) {
        return 0;
    }
}
#endif

#if defined(BAD_THROWING_NEW)
CHARR_CXX_HELPER int* allocate() noexcept
{
    return new int(1);
}
#endif
