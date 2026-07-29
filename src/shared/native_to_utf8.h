#ifndef CHARR_SHARED_NATIVE_TO_UTF8_H
#define CHARR_SHARED_NATIVE_TO_UTF8_H

#include "byte_view.h"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace charr {
namespace shared {

class NativeToUtf8 {
public:
    NativeToUtf8();
    ~NativeToUtf8();

    NativeToUtf8(const NativeToUtf8&) = delete;
    NativeToUtf8& operator=(const NativeToUtf8&) = delete;
    NativeToUtf8(NativeToUtf8&&) = delete;
    NativeToUtf8& operator=(NativeToUtf8&&) = delete;

    ByteView native(const char* data, int length);
    ByteView latin1(const char* data, int length);
    ByteView utf8_to_native(const char* data, int length);

    // Whether R's native encoding is UTF-8, decided by probing the same
    // converter the native() path uses. This is the only such predicate:
    // do not reintroduce a locale-name test or ICU's default converter name.
    // Resolved at most once per instance; because instances are per-operation
    // and Riconv_open("") binds the locale when it opens, a Sys.setlocale()
    // between operations is picked up automatically.
    bool native_is_utf8();

private:
    class Descriptor;

    enum class Tristate : signed char { unresolved = -1, no = 0, yes = 1 };

    std::unique_ptr<Descriptor> native_;
    std::unique_ptr<Descriptor> latin1_;
    std::unique_ptr<Descriptor> utf8_to_native_;
    std::vector<char> scratch_;
    Tristate native_is_utf8_;

    Descriptor& native_descriptor();
    Descriptor& latin1_descriptor();
    Descriptor& utf8_to_native_descriptor();
    ByteView convert(Descriptor& descriptor, const char* data, int length);
    void ensure_capacity(std::size_t required);
};

} // namespace shared
} // namespace charr

#endif
