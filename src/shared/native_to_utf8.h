#ifndef CHARR_SHARED_NATIVE_TO_UTF8_H
#define CHARR_SHARED_NATIVE_TO_UTF8_H

#include "byte_view.h"
#include "lint.h"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace charr {
namespace shared {

class CHARR_OWNER_TYPE NativeToUtf8 {
public:
    CHARR_CXX_HELPER NativeToUtf8();
    CHARR_CXX_HELPER ~NativeToUtf8();

    NativeToUtf8(const NativeToUtf8&) = delete;
    NativeToUtf8& operator=(const NativeToUtf8&) = delete;
    NativeToUtf8(NativeToUtf8&&) = delete;
    NativeToUtf8& operator=(NativeToUtf8&&) = delete;

    CHARR_CXX_HELPER ByteView native(const char* data, int length);
    CHARR_CXX_HELPER ByteView latin1(const char* data, int length);
    CHARR_CXX_HELPER ByteView utf8_to_native(
        const char* data, int length
    );

    // Whether R's native encoding is UTF-8, decided by probing the same
    // converter the native() path uses. This is the only such predicate:
    // do not reintroduce a locale-name test or ICU's default converter name.
    // The result is cached only for the lifetime of this per-operation object.
    CHARR_CXX_HELPER bool native_is_utf8();

    CHARR_CXX_HELPER void reset() noexcept;

private:
    class Descriptor;

    enum class Tristate : signed char { unresolved = -1, no = 0, yes = 1 };

    std::unique_ptr<Descriptor> native_;
    std::unique_ptr<Descriptor> latin1_;
    std::unique_ptr<Descriptor> utf8_to_native_;
    std::vector<char> scratch_;
    Tristate native_is_utf8_;

    CHARR_CXX_HELPER Descriptor& native_descriptor();
    CHARR_CXX_HELPER Descriptor& latin1_descriptor();
    CHARR_CXX_HELPER Descriptor& utf8_to_native_descriptor();
    CHARR_CXX_HELPER ByteView convert(
        Descriptor& descriptor, const char* data, int length
    );
    CHARR_CXX_HELPER void ensure_capacity(std::size_t required);
};

} // namespace shared
} // namespace charr

#endif
