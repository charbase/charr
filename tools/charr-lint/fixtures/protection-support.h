#ifndef CHARR_LINT_FIXTURE_PROTECTION_SUPPORT_H
#define CHARR_LINT_FIXTURE_PROTECTION_SUPPORT_H

#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif
#include <Rinternals.h>

#include "../../../src/shared/lint.h"

class StriException {
public:
    CHARR_NEUTRAL_HELPER const char* getMessage() const noexcept
    {
        return "fixture error";
    }
};

#include "../../../src/shared/entrypoint.h"
#include "../../../src/shared/protect.h"
#include "../../../src/shared/unwind.h"

namespace lint_fixture {

class CHARR_OWNER_TYPE Owner {
public:
    CHARR_NEUTRAL_HELPER Owner() noexcept = default;

    CHARR_NEUTRAL_HELPER ~Owner() noexcept
    {
    }
};

} // namespace lint_fixture

#endif
