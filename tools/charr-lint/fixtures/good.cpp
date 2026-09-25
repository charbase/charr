#include "protection-support.h"

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
    std::string output;
    output = "value";
    return output;
}

CHARR_CXX_HELPER void replace_output(std::string& output)
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

CHARR_ENTRYPOINT SEXP entrypoint() noexcept
{
    CHARR_ENTRYPOINT_BEGIN();
    const int base = r_trivial_result().value;
    try {
        std::string output;
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const int made = static_cast<int>(make_output().size());
                replace_output(output);
                const int value =
                    base + made + r_value() + neutral_value();
                result = entry_protections.reprotect_one(
                    Rf_ScalarInteger(value), result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
        CHARR_UNWIND_KEEP_RESULT();
    }
    CHARR_ENTRYPOINT_END();
}
