#include "../../../src/shared/lint.h"

CHARR_NEUTRAL_HELPER int select_value(int value) noexcept
{
    return value;
}

namespace associated {

struct Value {};

CHARR_NEUTRAL_HELPER int select_value(Value) noexcept
{
    return 4;
}

} // namespace associated

template<typename Value>
CHARR_NEUTRAL_HELPER int forward_value(Value value) noexcept
{
    return select_value(value);
}

CHARR_NEUTRAL_HELPER int use_value() noexcept
{
    return forward_value(associated::Value{});
}
