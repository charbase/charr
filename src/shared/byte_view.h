#ifndef CHARR_SHARED_BYTE_VIEW_H
#define CHARR_SHARED_BYTE_VIEW_H

namespace charr {
namespace shared {

struct ByteView {
    const char* ptr;
    int len;

    const char* data() const noexcept { return ptr; }
    int length() const noexcept { return len; }
};

} // namespace shared
} // namespace charr

#endif
