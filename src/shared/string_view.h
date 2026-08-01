#ifndef CHARR_SHARED_STRING_VIEW_H
#define CHARR_SHARED_STRING_VIEW_H

#include "lint.h"

namespace charr {
namespace shared {

constexpr int missing_string_length = -1;

enum class StringEncoding : unsigned char {
    native,
    utf8,
    latin1,
    bytes,
    ascii,
    ascii_or_utf8,
    missing,
    unknown
};


// A length-delimited, non-owning string view shared by the native kernels.
// Backend I/O is responsible for translating its own encoding metadata.
struct StringView {
    const char* ptr;
    int len;
    StringEncoding enc;

    CHARR_NEUTRAL_HELPER bool is_na() const noexcept
    {
        return enc == StringEncoding::missing;
    }
};

} // namespace shared
} // namespace charr

#endif
