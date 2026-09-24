#include "reader-support.h"

#include <string>
#include <utility>

CHARR_CXX_HELPER std::string make_owner()
{
    return std::string("value");
}

template<typename Fn>
CHARR_TRUSTED_UNWIND int test_unwind(Fn&& fn)
{
    return fn();
}

CHARR_ENTRYPOINT int entrypoint(int input) noexcept
{
#if defined(BAD_TEMPORARY_BEFORE_TRY)
    const std::string& early = make_owner();
#endif
    try {
        charport::Reader reader;
#if defined(BAD_READER_OUTSIDE_UNWIND)
        const int outside = reader.size();
#endif
        const int value = test_unwind([&]() -> int {
            reader.reset(input);
#if defined(BAD_NEW_IN_UNWIND)
            int* allocated = new int(1);
#endif
#if defined(BAD_DELETE_IN_UNWIND)
            int* released = nullptr;
            delete released;
#endif
#if defined(BAD_READER_INDIRECT)
            return static_cast<const charport::Reader&>(reader).size();
#else
            return reader.size();
#endif
        });
#if defined(BAD_TEMPORARY_AFTER_UNWIND)
        const std::string& late = make_owner();
#endif
        return value;
    }
    catch (...) {
        return -1;
    }
}
