#include "../../../src/shared/lint.h"

#include <string>
#include <utility>

struct TrivialResult {
    int value;
};

CHARR_NEUTRAL_HELPER int neutral_value() noexcept
{
    return 4;
}

CHARR_CXX_HELPER std::string make_output()
{
    std::string output("value");
    return output;
}

CHARR_CXX_HELPER void replace_output(std::string& output) noexcept
{
    output = "next";
}

CHARR_R_HELPER int r_value() noexcept
{
    return 3;
}

CHARR_R_HELPER TrivialResult r_trivial_result() noexcept
{
    TrivialResult result{5};
    return result;
}

template<typename Fn>
CHARR_TRUSTED_UNWIND int test_unwind(Fn&& fn)
{
    return fn();
}

CHARR_ENTRYPOINT int entrypoint() noexcept
{
    int result = r_trivial_result().value;
    try {
        std::string output;
        result = test_unwind([&]() -> int {
            output = make_output();
            replace_output(output);
            return r_value() + neutral_value();
        });
    }
    catch (...) {
        return -1;
    }
    return result;
}
