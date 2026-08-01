#ifndef CHARR_BASE_IO_STRING_VIEW_H
#define CHARR_BASE_IO_STRING_VIEW_H

#include "../ci_macros.h"
#include "../../shared/string_view.h"

namespace charr {
namespace base_backend {
namespace io {

CHARR_R_HELPER inline shared::StringView as_shared_view(SEXP value) noexcept
{
    if (value == NA_STRING) {
        return shared::StringView{
            nullptr, shared::missing_string_length,
            shared::StringEncoding::missing
        };
    }

    const shared::StringEncoding encoding = IS_ASCII(value)
        ? shared::StringEncoding::ascii
        : (IS_BYTES(value)
            ? shared::StringEncoding::bytes
            : (IS_UTF8(value)
                ? shared::StringEncoding::utf8
                : (IS_LATIN1(value)
                    ? shared::StringEncoding::latin1
                    : shared::StringEncoding::native)));
    return shared::StringView{CHAR(value), LENGTH(value), encoding};
}


CHARR_R_HELPER inline shared::StringView as_direct_utf8_view(
    SEXP value
) noexcept {
    if (value == NA_STRING) {
        return shared::StringView{
            nullptr, shared::missing_string_length,
            shared::StringEncoding::missing
        };
    }

    return shared::StringView{
        CHAR(value), LENGTH(value),
        IS_ASCII(value)
            ? shared::StringEncoding::ascii
            : shared::StringEncoding::utf8
    };
}

} // namespace io
} // namespace base_backend
} // namespace charr

#endif
