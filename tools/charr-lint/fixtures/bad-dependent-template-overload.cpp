#include "../../../src/shared/lint.h"

template<bool First>
CHARR_NEUTRAL_HELPER int select_value(int value) noexcept
{
    return First ? value : -value;
}

template<bool First>
CHARR_NEUTRAL_HELPER long select_value(long value) noexcept
{
    return First ? value : -value;
}

template<bool First, typename Value>
CHARR_NEUTRAL_HELPER Value forward_value(Value value) noexcept
{
    return select_value<First>(value);
}

CHARR_NEUTRAL_HELPER int use_value() noexcept
{
    return forward_value<true>(3);
}
