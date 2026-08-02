// Derived from stringi.
// Copyright (c) 2013-2025, Marek Gagolewski. See inst/COPYRIGHTS.

#include "title_case.h"

#include <unicode/stringpiece.h>
#include <unicode/uloc.h>
#include <unicode/unistr.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace charr {
namespace shared {

namespace title_case {

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


CHARR_NEUTRAL_HELPER bool is_title_locale(
    const char* locale
) noexcept {
    if (locale == nullptr)
        return false;
    if ((locale[0] == 'C' || locale[0] == 'c') &&
            (locale[1] == '\0' || locale[1] == '.')) {
        return true;
    }
    if (locale[0] == '\0')
        return true;
    if (std::strcmp(locale, "root") == 0)
        return true;
    if (locale[0] != 'e' || locale[1] != 'n')
        return false;
    if (locale[2] != '\0' && locale[2] != '_' && locale[2] != '-')
        return false;
    for (const char* current = locale + 2; *current != '\0'; ++current) {
        if (*current == '@')
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


CHARR_NEUTRAL_HELPER bool ascii_title_eligible(
    const TitleCaseInput& input
) noexcept {
    for (int i = 0; i < input.length; ++i) {
        const unsigned char byte = static_cast<unsigned char>(input.data[i]);
        if (byte >= 0x80)
            return false;
        if ((byte >= 'A' && byte <= 'Z') ||
                (byte >= 'a' && byte <= 'z')) {
            continue;
        }

        const bool whitespace = byte == ' ' ||
            (byte >= '\t' && byte <= '\r');
        const bool punctuation =
            (byte >= 0x21 && byte <= 0x2f) ||
            (byte >= 0x3a && byte <= 0x40) ||
            (byte >= 0x5b && byte <= 0x60) ||
            (byte >= 0x7b && byte <= 0x7e);
        if ((!whitespace && !punctuation) || byte == '\'' || byte == '_')
            return false;

        if (byte == '.' && i > 0 && i + 1 < input.length) {
            const unsigned char previous = static_cast<unsigned char>(
                input.data[i-1]
            );
            const unsigned char next = static_cast<unsigned char>(
                input.data[i+1]
            );
            const bool previous_letter =
                (previous >= 'A' && previous <= 'Z') ||
                (previous >= 'a' && previous <= 'z');
            const bool next_letter =
                (next >= 'A' && next <= 'Z') ||
                (next >= 'a' && next <= 'z');
            if (previous_letter && next_letter)
                return false;
        }
    }
    return true;
}


CHARR_NEUTRAL_HELPER StringView missing() noexcept
{
    return StringView{
        nullptr, missing_string_length, StringEncoding::missing
    };
}

} // namespace title_case

using namespace title_case;

TitleCaseMapper::TitleCaseMapper()
    : casemap_(nullptr), iterator_(nullptr), ascii_fast_path_(false),
      turkic_(false), converter_(), output_()
{
}


TitleCaseMapper::~TitleCaseMapper() noexcept
{
    close();
}


void TitleCaseMapper::close() noexcept
{
    if (casemap_ != nullptr) {
        ucasemap_close(casemap_);
        casemap_ = nullptr;
    }
    if (iterator_ != nullptr) {
        ubrk_close(iterator_);
        iterator_ = nullptr;
    }
    ascii_fast_path_ = false;
    turkic_ = false;
}


TitleCaseOpenResult TitleCaseMapper::reset(
    const TitleCaseOptions& options
) noexcept {
    close();

    UErrorCode status = U_ZERO_ERROR;
    casemap_ = ucasemap_open(
        options.locale, U_FOLD_CASE_DEFAULT, &status
    );
    if (U_FAILURE(status) || casemap_ == nullptr) {
        close();
        if (U_SUCCESS(status))
            status = U_MEMORY_ALLOCATION_ERROR;
        return TitleCaseOpenResult{status, false};
    }

    status = U_ZERO_ERROR;
    if (options.custom_rules) {
        icu::UnicodeString rules;
        rules.setTo(icu::UnicodeString::fromUTF8(
            icu::StringPiece(options.rules, options.rules_length)
        ));
        if (rules.isBogus()) {
            close();
            return TitleCaseOpenResult{U_MEMORY_ALLOCATION_ERROR, false};
        }

        UParseError parse_error;
        iterator_ = ubrk_openRules(
            rules.getBuffer(), rules.length(), nullptr, 0,
            &parse_error, &status
        );
    }
    else {
        iterator_ = ubrk_open(
            options.type, options.locale, nullptr, 0, &status
        );
    }

    if (U_FAILURE(status) || iterator_ == nullptr) {
        close();
        if (U_SUCCESS(status))
            status = U_MEMORY_ALLOCATION_ERROR;
        return TitleCaseOpenResult{status, false};
    }

    bool root_fallback = false;
    if (status == U_USING_DEFAULT_WARNING && options.locale != nullptr) {
        UErrorCode locale_status = U_ZERO_ERROR;
        const char* valid_locale = ubrk_getLocaleByType(
            iterator_, ULOC_VALID_LOCALE, &locale_status
        );
        root_fallback = valid_locale != nullptr &&
            std::strcmp(valid_locale, "root") == 0;
    }

    status = U_ZERO_ERROR;
    ucasemap_setBreakIterator(casemap_, iterator_, &status);
    // The setter adopts the iterator as part of the call.
    iterator_ = nullptr;
    if (U_FAILURE(status)) {
        close();
        return TitleCaseOpenResult{status, root_fallback};
    }

    const char* locale = ucasemap_getLocale(casemap_);
    ascii_fast_path_ = !options.custom_rules && !options.has_skip_rules &&
        options.type == UBRK_WORD && is_title_locale(locale);
    turkic_ = is_turkic_locale(locale);

    return TitleCaseOpenResult{U_ZERO_ERROR, root_fallback};
}


TitleCaseInput TitleCaseMapper::prepare(
    const StringView& source
)
{
    if (source.is_na())
        throw std::invalid_argument("cannot titlecase a missing string");
    if (source.len < 0 || (source.ptr == nullptr && source.len != 0))
        throw std::invalid_argument("invalid titlecase input string view");

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
            "non-missing titlecase input has NA encoding"
        );
    case StringEncoding::unknown:
        throw std::invalid_argument("unknown titlecase input encoding");
    }

    if (strip_bom) {
        data += 3;
        length -= 3;
    }
    if (length == 0)
        data = &empty;
    if (!ascii_known && !ascii_fast_path_)
        ascii = is_ascii(data, length);

    return TitleCaseInput{data, length, ascii};
}


bool TitleCaseMapper::has_ascii_fast_path(
    const TitleCaseInput& input
) const noexcept {
    return ascii_fast_path_ && ascii_title_eligible(input);
}


void TitleCaseMapper::map_ascii(
    const TitleCaseInput& input, char* output
) const noexcept {
    bool word_start = true;
    for (int i = 0; i < input.length; ++i) {
        unsigned char byte = static_cast<unsigned char>(input.data[i]);
        if (byte >= 'A' && byte <= 'Z') {
            if (!word_start)
                byte += static_cast<unsigned char>('a'-'A');
            word_start = false;
        }
        else if (byte >= 'a' && byte <= 'z') {
            if (word_start)
                byte -= static_cast<unsigned char>('a'-'A');
            word_start = false;
        }
        else {
            word_start = true;
        }
        output[i] = static_cast<char>(byte);
    }
}


StringView TitleCaseMapper::map_icu(
    const TitleCaseInput& input, UErrorCode& status
)
{
    if (casemap_ == nullptr)
        throw std::logic_error("titlecase mapper has not been initialized");

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
    int output_length = ucasemap_utf8ToTitle(
        casemap_, output_.data(), capacity,
        input.data, input.length, &status
    );
    if (U_FAILURE(status)) {
        if (output_length < 0)
            return missing();
        if (output_.size() < static_cast<std::size_t>(output_length))
            output_.resize(static_cast<std::size_t>(output_length));
        capacity = static_cast<int>(std::min(output_.size(), int_max));
        status = U_ZERO_ERROR;
        output_length = ucasemap_utf8ToTitle(
            casemap_, output_.data(), capacity,
            input.data, input.length, &status
        );
        if (U_FAILURE(status))
            return missing();
    }

    const bool output_ascii = (input.ascii && !turkic_) ||
        is_ascii(output_.data(), output_length);
    return StringView{
        output_.data(), output_length,
        output_ascii ? StringEncoding::ascii : StringEncoding::utf8
    };
}

} // namespace shared
} // namespace charr
