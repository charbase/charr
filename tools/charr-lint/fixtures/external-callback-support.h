#pragma once

inline void run_callback(void (*fn)()) noexcept
{
    fn();
}

template<typename Fn>
inline void run_functor(Fn fn) noexcept
{
    fn();
}
