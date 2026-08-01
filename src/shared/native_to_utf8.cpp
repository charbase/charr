#include "native_to_utf8.h"

#include <cerrno>
#include <cstring>
#include <string>

#include <R_ext/Riconv.h>
#include <Rinternals.h>

namespace charr {
namespace shared {

namespace native_to_utf8 {

constexpr std::size_t initial_capacity = 64;

CHARR_CXX_HELPER std::string conversion_error_message(
    const char* conversion, int error
)
{
    std::string message;
    message.append("failed to convert ", 18);
    message.append(conversion, std::strlen(conversion));
    if (error != 0) {
        const char* detail = std::strerror(error);
        message.append(": ", 2);
        message.append(detail, std::strlen(detail));
    }
    return message;
}

} // namespace native_to_utf8

using namespace native_to_utf8;

class CHARR_OWNER_TYPE NativeToUtf8::Descriptor {
public:
    CHARR_CXX_HELPER Descriptor(
        const char* target_name, const char* source_name,
        const char* conversion
    )
        : handle_(Riconv_open(target_name, source_name)),
          label_(conversion)
    {
        if (handle_ == reinterpret_cast<void*>(-1)) {
            const int error = errno;
            handle_ = nullptr;
            throw std::runtime_error(
                conversion_error_message(conversion, error)
            );
        }
    }

    CHARR_CXX_HELPER ~Descriptor()
    {
        if (handle_ != nullptr)
            Riconv_close(handle_);
    }

    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;

    CHARR_NEUTRAL_HELPER void* handle() const noexcept
    {
        return handle_;
    }

    CHARR_NEUTRAL_HELPER const char* label() const noexcept
    {
        return label_;
    }

    CHARR_NEUTRAL_HELPER void reset() noexcept
    {
        Riconv(handle_, nullptr, nullptr, nullptr, nullptr);
    }

private:
    void* handle_;
    const char* label_;
};

NativeToUtf8::NativeToUtf8()
    : native_(), latin1_(), utf8_to_native_(), scratch_(),
      native_is_utf8_(Tristate::unresolved)
{
}

NativeToUtf8::~NativeToUtf8() = default;

NativeToUtf8::Descriptor& NativeToUtf8::native_descriptor()
{
    // An empty source name asks R to resolve the active LC_CTYPE. ICU's
    // process default is not necessarily the same encoding.
    if (!native_)
        native_.reset(new Descriptor(
            "UTF-8", "", "R native encoding to UTF-8"
        ));
    return *native_;
}

NativeToUtf8::Descriptor& NativeToUtf8::latin1_descriptor()
{
    if (!latin1_) {
#if defined(_WIN32) || defined(_WIN64)
        latin1_.reset(new Descriptor(
            "UTF-8", "WINDOWS-1252", "Windows-1252 to UTF-8"
        ));
#else
        latin1_.reset(new Descriptor(
            "UTF-8", "ISO-8859-1", "ISO-8859-1 to UTF-8"
        ));
#endif
    }
    return *latin1_;
}

NativeToUtf8::Descriptor& NativeToUtf8::utf8_to_native_descriptor()
{
    if (!utf8_to_native_) {
        utf8_to_native_.reset(new Descriptor(
            "", "UTF-8", "UTF-8 to R native encoding"
        ));
    }
    return *utf8_to_native_;
}

ByteView NativeToUtf8::native(const char* data, int length)
{
    return convert(native_descriptor(), data, length);
}

ByteView NativeToUtf8::latin1(const char* data, int length)
{
    return convert(latin1_descriptor(), data, length);
}

ByteView NativeToUtf8::utf8_to_native(
    const char* data, int length
)
{
    return convert(utf8_to_native_descriptor(), data, length);
}

bool NativeToUtf8::native_is_utf8()
{
    if (native_is_utf8_ != Tristate::unresolved)
        return native_is_utf8_ == Tristate::yes;

    // U+00E9 in UTF-8. Chosen because it round-trips to itself only when the
    // native encoding is UTF-8: a single-byte encoding decodes the two bytes
    // as two separate characters (4 output bytes for Latin-1 and Windows-1252),
    // and encodings that cannot represent them fail outright. Either way the
    // answer to "may these bytes be read as UTF-8 without converting?" is no.
    static const char probe[2] = { '\xc3', '\xa9' };

    bool resolved = false;
    try {
        const ByteView converted = convert(
            native_descriptor(), probe, 2
        );
        resolved = converted.len == 2 &&
            converted.ptr[0] == probe[0] &&
            converted.ptr[1] == probe[1];
    }
    catch (const std::exception&) {
        resolved = false;
    }

    native_is_utf8_ = resolved ? Tristate::yes : Tristate::no;
    return resolved;
}

void NativeToUtf8::reset() noexcept
{
    native_.reset();
    latin1_.reset();
    utf8_to_native_.reset();
    std::vector<char>().swap(scratch_);
    native_is_utf8_ = Tristate::unresolved;
}

void NativeToUtf8::ensure_capacity(std::size_t required)
{
    const std::size_t maximum = static_cast<std::size_t>(R_LEN_T_MAX);
    if (required > maximum)
        throw std::length_error("converted string exceeds R string size");
    if (scratch_.size() >= required)
        return;

    std::size_t next = scratch_.empty() ? initial_capacity : scratch_.size();
    while (next < required) {
        if (next > maximum / 2) {
            next = maximum;
            break;
        }
        next *= 2;
    }
    scratch_.resize(next);
}

ByteView NativeToUtf8::convert(
    Descriptor& descriptor, const char* data, int length
)
{
    if (length < 0 || (data == nullptr && length != 0))
        throw std::invalid_argument("invalid source byte view");

    const std::size_t input_size = static_cast<std::size_t>(length);
    const std::size_t growth = input_size > 24
        ? input_size / 2
        : static_cast<std::size_t>(24);
    if (input_size > static_cast<std::size_t>(R_LEN_T_MAX) - growth)
        ensure_capacity(static_cast<std::size_t>(R_LEN_T_MAX));
    else
        ensure_capacity(input_size + growth);

    descriptor.reset();
    const char* input = data;
    std::size_t input_left = input_size;
    std::size_t used = 0;

    try {
        while (input_left > 0) {
            char* output = scratch_.data() + used;
            std::size_t output_left = scratch_.size() - used;
            errno = 0;
            const std::size_t result = Riconv(
                descriptor.handle(), &input, &input_left,
                &output, &output_left
            );
            used = scratch_.size() - output_left;
            if (result != static_cast<std::size_t>(-1))
                continue;
            if (errno != E2BIG)
                throw std::runtime_error(
                    conversion_error_message(descriptor.label(), errno)
                );
            if (scratch_.size() == static_cast<std::size_t>(R_LEN_T_MAX))
                throw std::length_error(
                    "converted string exceeds R string size"
                );
            ensure_capacity(scratch_.size() + 1);
        }

        for (;;) {
            char* output = scratch_.data() + used;
            std::size_t output_left = scratch_.size() - used;
            errno = 0;
            const std::size_t result = Riconv(
                descriptor.handle(), nullptr, nullptr,
                &output, &output_left
            );
            used = scratch_.size() - output_left;
            if (result != static_cast<std::size_t>(-1))
                break;
            if (errno != E2BIG)
                throw std::runtime_error(
                    conversion_error_message(descriptor.label(), errno)
                );
            if (scratch_.size() == static_cast<std::size_t>(R_LEN_T_MAX))
                throw std::length_error(
                    "converted string exceeds R string size"
                );
            ensure_capacity(scratch_.size() + 1);
        }
    }
    catch (...) {
        descriptor.reset();
        throw;
    }

    return ByteView{scratch_.data(), static_cast<int>(used)};
}

} // namespace shared
} // namespace charr
