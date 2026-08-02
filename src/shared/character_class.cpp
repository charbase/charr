// Derived from stringi.
// Copyright (c) 2013-2025, Marek Gagolewski. See inst/COPYRIGHTS.

#include "character_class.h"

#include <unicode/stringpiece.h>
#include <unicode/unistr.h>

namespace charr {
namespace shared {

CharacterClassSet::CharacterClassSet() noexcept : values_()
{
}


CharacterClassSet::~CharacterClassSet() noexcept = default;


UErrorCode CharacterClassSet::reset(
    const std::vector<StringView>& patterns, bool negate
) {
    values_.clear();
    values_.resize(patterns.size());

    for (std::size_t i = 0; i < patterns.size(); ++i) {
        const StringView& pattern = patterns[i];
        icu::UnicodeSet& value = values_[i];
        if (pattern.is_na()) {
            value.setToBogus();
            continue;
        }

        UErrorCode status = U_ZERO_ERROR;
        const icu::UnicodeString text = icu::UnicodeString::fromUTF8(
            icu::StringPiece(pattern.ptr, pattern.len)
        );
        value.applyPattern(text, status);
        if (U_FAILURE(status))
            return status;
        if (negate)
            value.complement();
        value.freeze();
    }

    return U_ZERO_ERROR;
}


std::size_t CharacterClassSet::size() const noexcept
{
    return values_.size();
}


bool CharacterClassSet::is_na(std::size_t index) const noexcept
{
    return values_[index % values_.size()].isBogus();
}


const icu::UnicodeSet& CharacterClassSet::get(
    std::size_t index
) const noexcept {
    return values_[index % values_.size()];
}

} // namespace shared
} // namespace charr
