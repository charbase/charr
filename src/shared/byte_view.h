#ifndef CHARR_SHARED_BYTE_VIEW_H
#define CHARR_SHARED_BYTE_VIEW_H

#include "lint.h"

namespace charr {
namespace shared {

struct ByteView {
    const char* ptr;
    int len;

    CHARR_NEUTRAL_HELPER const char* data() const noexcept { return ptr; }
    CHARR_NEUTRAL_HELPER int length() const noexcept { return len; }
};

} // namespace shared
} // namespace charr

#endif
