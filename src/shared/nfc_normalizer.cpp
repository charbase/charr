// Derived from stringi.
// Copyright (c) 2013-2025, Marek Gagolewski. See inst/COPYRIGHTS.

#include "nfc_normalizer.h"

#include <unicode/ucnv.h>
#include <unicode/stringpiece.h>
#include <unicode/ustring.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace charr {
namespace shared {

namespace nfc_normalizer {

const char empty = '\0';

CHARR_NEUTRAL_HELPER StringView missing() noexcept
{
    return StringView{
        nullptr, missing_string_length, StringEncoding::missing
    };
}

} // namespace nfc_normalizer

using namespace nfc_normalizer;

NfcNormalizer::NfcNormalizer()
    : normalizer_(nullptr), converter_(), input_(), output_(), utf8_()
{
}

UErrorCode NfcNormalizer::reset()
{
    UErrorCode status = U_ZERO_ERROR;
    normalizer_ = icu::Normalizer2::getNFCInstance(status);
    if (normalizer_ == nullptr && U_SUCCESS(status))
        status = U_MEMORY_ALLOCATION_ERROR;
    return status;
}

StringView NfcNormalizer::normalize(
    const StringView& source, UErrorCode& status
)
{
    if (source.is_na())
        return missing();
    if (normalizer_ == nullptr)
        throw std::logic_error("NFC normalizer has not been initialized");
    if (source.len < 0 || (source.ptr == nullptr && source.len != 0))
        throw std::invalid_argument("invalid NFC input string view");

    const char* data = source.ptr == nullptr ? &empty : source.ptr;
    int length = source.len;
    switch (source.enc) {
    case StringEncoding::ascii:
    case StringEncoding::utf8:
    case StringEncoding::ascii_or_utf8:
        break;
    case StringEncoding::latin1: {
        const ByteView converted = converter_.latin1(data, length);
        data = converted.ptr;
        length = converted.len;
        break;
    }
    case StringEncoding::native: {
        const ByteView converted = converter_.native(data, length);
        data = converted.ptr;
        length = converted.len;
        break;
    }
    case StringEncoding::bytes:
        throw std::runtime_error(
            "bytes encoding is not supported by this function"
        );
    case StringEncoding::missing:
        throw std::invalid_argument(
            "non-missing NFC input has NA encoding"
        );
    case StringEncoding::unknown:
        throw std::invalid_argument("unknown NFC input encoding");
    }

    input_.setTo(icu::UnicodeString::fromUTF8(
        icu::StringPiece(data, length)
    ));
    normalizer_->normalize(input_, output_, status);
    if (U_FAILURE(status))
        return missing();

    const int32_t utf16_length = output_.length();
    const int32_t maximum = std::numeric_limits<int32_t>::max();
    if (utf16_length > maximum / 3 - 10)
        throw std::length_error("UTF-8 output exceeds ICU's length limit");

    if (utf16_length == 0) {
        return StringView{
            &empty, 0, StringEncoding::ascii
        };
    }

    const std::size_t capacity = static_cast<std::size_t>(
        UCNV_GET_MAX_BYTES_FOR_STRING(utf16_length, 3)
    );
    if (utf8_.size() < capacity)
        utf8_.resize(capacity);

    int32_t utf8_length = 0;
    u_strToUTF8(
        utf8_.data(), static_cast<int32_t>(capacity), &utf8_length,
        output_.getBuffer(), utf16_length, &status
    );
    if (U_FAILURE(status))
        return missing();

    return StringView{
        utf8_.data(), utf8_length,
        utf8_length == utf16_length
            ? StringEncoding::ascii
            : StringEncoding::utf8
    };
}

} // namespace shared
} // namespace charr
