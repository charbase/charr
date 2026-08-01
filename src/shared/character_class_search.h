#ifndef CHARR_SHARED_CHARACTER_CLASS_SEARCH_H
#define CHARR_SHARED_CHARACTER_CLASS_SEARCH_H

#include "lint.h"
#include "replacement.h"
#include "string_view.h"

#include <unicode/uniset.h>

#include <cstddef>
#include <vector>

namespace charr {
namespace shared {

using CharacterClassRange = ByteRange;


CHARR_CXX_HELPER std::size_t find_character_class_ranges(
    const StringView& input,
    const icu::UnicodeSet& pattern,
    bool merge,
    std::vector<CharacterClassRange>& output
);

} // namespace shared
} // namespace charr

#endif
