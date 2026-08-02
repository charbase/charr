// Derived from stringi.
// Copyright (c) 2013-2025, Marek Gagolewski. See inst/COPYRIGHTS.

#include "case_mapper.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace charr {
namespace shared {

namespace case_mapper {

const char empty = '\0';

CHARR_NEUTRAL_HELPER bool has_bom(
    const char* data, int length
) noexcept {
    return length >= 3 &&
        static_cast<unsigned char>(data[0]) == 0xef &&
        static_cast<unsigned char>(data[1]) == 0xbb &&
        static_cast<unsigned char>(data[2]) == 0xbf;
}


CHARR_NEUTRAL_HELPER bool is_ascii(
    const char* data, int length
) noexcept {
    for (int i = 0; i < length; ++i) {
        if (static_cast<unsigned char>(data[i]) >= 0x80)
            return false;
    }
    return true;
}


CHARR_NEUTRAL_HELPER bool is_turkic_locale(
    const char* locale
) noexcept {
    if (locale == nullptr || locale[0] == '\0' || locale[1] == '\0')
        return false;
    const bool delimiter = locale[2] == '\0' || locale[2] == '_' ||
        locale[2] == '-' || locale[2] == '@';
    return delimiter &&
        ((locale[0] == 't' && locale[1] == 'r') ||
         (locale[0] == 'a' && locale[1] == 'z'));
}


CHARR_NEUTRAL_HELPER int call(
    UCaseMap* casemap, CaseMapMode mode,
    char* destination, int capacity,
    const char* source, int length, UErrorCode* status
) noexcept {
    if (mode == CaseMapMode::lower) {
        return ucasemap_utf8ToLower(
            casemap, destination, capacity, source, length, status
        );
    }
    return ucasemap_utf8ToUpper(
        casemap, destination, capacity, source, length, status
    );
}

} // namespace case_mapper

using namespace case_mapper;

CaseMapper::CaseMapper()
    : casemap_(nullptr), locale_(nullptr), mode_(CaseMapMode::lower),
      simple_ascii_(true), converter_(), output_()
{
}


CaseMapper::~CaseMapper() noexcept
{
    close();
}


void CaseMapper::close() noexcept
{
    if (casemap_ != nullptr) {
        ucasemap_close(casemap_);
        casemap_ = nullptr;
    }
}


void CaseMapper::reset(
    const char* locale, CaseMapMode mode
) noexcept {
    close();
    locale_ = locale;
    mode_ = mode;
    simple_ascii_ = !is_turkic_locale(locale);
}


CaseMapInput CaseMapper::prepare(
    const StringView& source
)
{
    if (source.is_na())
        throw std::invalid_argument("cannot case-map a missing string");
    if (source.len < 0 || (source.ptr == nullptr && source.len != 0))
        throw std::invalid_argument("invalid case-map input string view");

    const char* data = source.ptr == nullptr ? &empty : source.ptr;
    int length = source.len;
    bool strip_bom = false;
    bool ascii = false;
    bool ascii_known = false;

    switch (source.enc) {
    case StringEncoding::ascii:
        ascii = true;
        ascii_known = true;
        break;
    case StringEncoding::utf8:
        strip_bom = has_bom(data, length);
        ascii_known = true;
        break;
    case StringEncoding::ascii_or_utf8:
        strip_bom = has_bom(data, length);
        break;
    case StringEncoding::latin1: {
        const ByteView converted = converter_.latin1(data, length);
        data = converted.ptr;
        length = converted.len;
        break;
    }
    case StringEncoding::native: {
        const bool native_bom = has_bom(data, length);
        const ByteView converted = converter_.native(data, length);
        data = converted.ptr;
        length = converted.len;
        strip_bom = native_bom && has_bom(data, length);
        break;
    }
    case StringEncoding::bytes:
        throw std::runtime_error(
            "bytes encoding is not supported by this function"
        );
    case StringEncoding::missing:
        throw std::invalid_argument(
            "non-missing case-map input has NA encoding"
        );
    case StringEncoding::unknown:
        throw std::invalid_argument("unknown case-map input encoding");
    }

    if (strip_bom) {
        data += 3;
        length -= 3;
    }
    if (length == 0)
        data = &empty;
    if (!ascii_known)
        ascii = is_ascii(data, length);

    return CaseMapInput{data, length, ascii};
}


bool CaseMapper::has_ascii_fast_path(
    const CaseMapInput& input
) const noexcept {
    return simple_ascii_ && input.ascii;
}


bool CaseMapper::map_ascii(
    const CaseMapInput& input, char* output
) const noexcept {
    bool changed = false;
    if (mode_ == CaseMapMode::upper) {
        for (int i = 0; i < input.length; ++i) {
            unsigned char byte = static_cast<unsigned char>(input.data[i]);
            if (byte >= 'a' && byte <= 'z') {
                byte -= static_cast<unsigned char>('a'-'A');
                changed = true;
            }
            output[i] = static_cast<char>(byte);
        }
    }
    else {
        for (int i = 0; i < input.length; ++i) {
            unsigned char byte = static_cast<unsigned char>(input.data[i]);
            if (byte >= 'A' && byte <= 'Z') {
                byte += static_cast<unsigned char>('a'-'A');
                changed = true;
            }
            output[i] = static_cast<char>(byte);
        }
    }
    return changed;
}


bool CaseMapper::open(UErrorCode& status) noexcept
{
    if (casemap_ != nullptr)
        return true;

    status = U_ZERO_ERROR;
    casemap_ = ucasemap_open(locale_, U_FOLD_CASE_DEFAULT, &status);
    if (U_FAILURE(status) || casemap_ == nullptr) {
        close();
        if (U_SUCCESS(status))
            status = U_MEMORY_ALLOCATION_ERROR;
        return false;
    }
    return true;
}


StringView CaseMapper::map_icu(
    const CaseMapInput& input, UErrorCode& status
)
{
    if (!open(status)) {
        return StringView{
            nullptr, missing_string_length, StringEncoding::missing
        };
    }

    const std::size_t margin = 10;
    const std::size_t initial =
        static_cast<std::size_t>(input.length) + margin;
    if (output_.size() < initial)
        output_.resize(initial);

    const std::size_t int_max = static_cast<std::size_t>(
        std::numeric_limits<int>::max()
    );
    int capacity = static_cast<int>(std::min(output_.size(), int_max));
    status = U_ZERO_ERROR;
    int output_length = call(
        casemap_, mode_, output_.data(), capacity,
        input.data, input.length, &status
    );

    if (U_FAILURE(status)) {
        if (output_length < 0) {
            return StringView{
                nullptr, missing_string_length, StringEncoding::missing
            };
        }
        if (output_.size() < static_cast<std::size_t>(output_length))
            output_.resize(static_cast<std::size_t>(output_length));
        capacity = static_cast<int>(std::min(output_.size(), int_max));
        status = U_ZERO_ERROR;
        output_length = call(
            casemap_, mode_, output_.data(), capacity,
            input.data, input.length, &status
        );
        if (U_FAILURE(status)) {
            return StringView{
                nullptr, missing_string_length, StringEncoding::missing
            };
        }
    }

    const char* data = output_length == 0 ? &empty : output_.data();
    const bool output_ascii =
        (input.ascii && !is_turkic_locale(ucasemap_getLocale(casemap_))) ||
        is_ascii(data, output_length);
    return StringView{
        data, output_length,
        output_ascii ? StringEncoding::ascii : StringEncoding::utf8
    };
}

} // namespace shared
} // namespace charr
