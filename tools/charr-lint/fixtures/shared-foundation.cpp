#include "../../../src/shared/entrypoint.h"
#include "../../../src/shared/protect.h"
#include "../../../src/shared/unwind.h"

CHARR_NEUTRAL_HELPER int shared_foundation_probe() noexcept
{
    charr::shared::ProtHelper protections;
    charr::shared::EntryErrorState error;
    return protections.count() + static_cast<int>(error.has_cpp_error());
}
