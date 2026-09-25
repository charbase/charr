#include "protection-support.h"
#include "reader-support.h"

#include <string>

CHARR_CXX_HELPER std::string make_owner()
{
    std::string owner;
    owner = "value";
    return owner;
}

CHARR_ENTRYPOINT SEXP entrypoint(int input) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();
#if defined(BAD_TEMPORARY_BEFORE_TRY)
    const std::string& early = make_owner();
#endif
    try {
        charport::Reader reader;
#if defined(BAD_READER_OUTSIDE_UNWIND)
        const int outside = reader.size();
#endif
        result = charr::shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                reader.reset(input);
#if defined(BAD_NEW_IN_UNWIND)
                int* allocated = new int(1);
#endif
#if defined(BAD_DELETE_IN_UNWIND)
                int* released = nullptr;
                delete released;
#endif
#if defined(BAD_READER_INDIRECT)
                const int size =
                    static_cast<const charport::Reader&>(reader).size();
#else
                const int size = reader.size();
#endif
                result = entry_protections.reprotect_one(
                    Rf_ScalarInteger(size), result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
        CHARR_UNWIND_KEEP_RESULT();
#if defined(BAD_TEMPORARY_AFTER_UNWIND)
        const std::string& late = make_owner();
#endif
    }
    CHARR_ENTRYPOINT_END();
}
