#ifndef CHARR_SHARED_CHARACTER_CLASS_H
#define CHARR_SHARED_CHARACTER_CLASS_H

#include "lint.h"
#include "string_view.h"

#include <unicode/uniset.h>
#include <unicode/utypes.h>

#include <cstddef>
#include <vector>

namespace charr {
namespace shared {

// Owns compiled ICU character classes. Input views must already be normalized
// to UTF-8, so initialization remains entirely in the native phase.
class CHARR_OWNER_TYPE CharacterClassSet {
public:
    CHARR_CXX_HELPER CharacterClassSet() noexcept;
    CHARR_CXX_HELPER ~CharacterClassSet() noexcept;

    CharacterClassSet(const CharacterClassSet&) = delete;
    CharacterClassSet& operator=(const CharacterClassSet&) = delete;
    CharacterClassSet(CharacterClassSet&&) = delete;
    CharacterClassSet& operator=(CharacterClassSet&&) = delete;

    CHARR_CXX_HELPER UErrorCode reset(
        const std::vector<StringView>& patterns, bool negate
    );

    CHARR_NEUTRAL_HELPER std::size_t size() const noexcept;
    CHARR_CXX_HELPER bool is_na(std::size_t index) const noexcept;
    CHARR_CXX_HELPER const icu::UnicodeSet& get(
        std::size_t index
    ) const noexcept;

private:
    std::vector<icu::UnicodeSet> values_;
};

} // namespace shared
} // namespace charr

#endif
