#include "../../../src/shared/lint.h"

template<bool AddOne>
CHARR_NEUTRAL_HELPER int select_value(int value) noexcept
{
    if constexpr (AddOne)
        return value + 1;
    return value;
}

template<bool AddOne>
CHARR_NEUTRAL_HELPER int forward_value(int value) noexcept
{
    return select_value<AddOne>(value);
}

CHARR_NEUTRAL_HELPER int use_value() noexcept
{
    return forward_value<true>(3);
}
