// Derived from stringi 19e9586ba39b3320df49355e32bd18d74ed6098f.
// Copyright (c) 2013-2025, Marek Gagolewski. See inst/COPYRIGHTS.

#include "boundary_iterator.h"

#include <unicode/stringpiece.h>
#include <unicode/uloc.h>
#include <unicode/unistr.h>

#include <cstring>

namespace charr {
namespace shared {

BoundaryIterator::BoundaryIterator() noexcept
    : iterator_(nullptr), text_(nullptr), skip_rules_(nullptr), skip_size_(0),
      position_(UBRK_DONE)
{
}


BoundaryIterator::~BoundaryIterator() noexcept
{
    close();
}


void BoundaryIterator::close() noexcept
{
    if (iterator_ != nullptr) {
        ubrk_close(iterator_);
        iterator_ = nullptr;
    }
    if (text_ != nullptr) {
        utext_close(text_);
        text_ = nullptr;
    }
    skip_rules_ = nullptr;
    skip_size_ = 0;
    position_ = UBRK_DONE;
}


BoundaryOpenResult BoundaryIterator::reset(
    const BoundaryOptions& options
) noexcept {
    close();

    if ((options.skip_size % 2) != 0 ||
            (options.skip_size > 0 && options.skip_rules == nullptr) ||
            (options.custom_rules &&
                (options.rules == nullptr || options.rules_length <= 0))) {
        return BoundaryOpenResult{U_ILLEGAL_ARGUMENT_ERROR, false};
    }

    UErrorCode status = U_ZERO_ERROR;
    if (options.custom_rules) {
        icu::UnicodeString rules;
        rules.setTo(icu::UnicodeString::fromUTF8(
            icu::StringPiece(options.rules, options.rules_length)
        ));
        if (rules.isBogus()) {
            return BoundaryOpenResult{
                U_MEMORY_ALLOCATION_ERROR, false
            };
        }

        UParseError parse_error = {};
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
        return BoundaryOpenResult{status, false};
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

    skip_rules_ = options.skip_rules;
    skip_size_ = options.skip_size;
    return BoundaryOpenResult{status, root_fallback};
}


int BoundaryIterator::count(
    const StringView& source, UErrorCode& status
) noexcept {
    status = set_text(source);
    if (U_FAILURE(status))
        return 0;

    first();
    BoundaryRange range{0, 0};
    int result = 0;
    while (next(range))
        ++result;
    return result;
}


bool boundary_ascii_word_first(
    const BoundaryOptions& options
) noexcept {
    if (options.custom_rules || options.type != UBRK_WORD ||
            options.skip_size != 0) {
        return false;
    }

    const char* locale = options.locale;
    if (locale == nullptr)
        locale = uloc_getDefault();
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
    for (const char* current = locale + 2; *current; ++current) {
        if (*current == '@')
            return false;
    }
    return true;
}


bool boundary_ascii_initial_word(
    const char* value, int length, int& end
) noexcept {
    // This is deliberately narrower than ICU word iteration. A plain ASCII
    // letter run followed by ASCII whitespace (or the end) has the same first
    // boundary in the root and English rules; everything else stays on ICU.
    if (value == nullptr || length <= 0)
        return false;
    int position = 0;
    while (position < length) {
        const unsigned char byte = static_cast<unsigned char>(value[position]);
        if (!((byte >= 'A' && byte <= 'Z') ||
                (byte >= 'a' && byte <= 'z'))) {
            break;
        }
        ++position;
    }
    if (position == 0)
        return false;
    if (position < length) {
        const unsigned char byte = static_cast<unsigned char>(value[position]);
        if (byte != ' ' && (byte < '\t' || byte > '\r'))
            return false;
    }
    end = position;
    return true;
}

} // namespace shared
} // namespace charr
