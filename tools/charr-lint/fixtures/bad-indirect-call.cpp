#include "../../../src/shared/lint.h"

using Operation = int (*)();

CHARR_NEUTRAL_HELPER int call_operation(Operation operation) noexcept
{
    return operation();
}
