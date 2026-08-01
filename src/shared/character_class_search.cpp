#include "character_class_search.h"

#include <unicode/utf8.h>

#include <limits>
#include <stdexcept>

namespace charr {
namespace shared {

std::size_t find_character_class_ranges(
    const StringView& input,
    const icu::UnicodeSet& pattern,
    bool merge,
    std::vector<CharacterClassRange>& output
) {
    output.clear();

    if (input.is_na()) {
        throw std::invalid_argument(
            "character class search requires non-missing input"
        );
    }
    if (input.len < 0) {
        throw std::invalid_argument(
            "character class search input has negative length"
        );
    }
    if (input.ptr == nullptr) {
        throw std::invalid_argument(
            "character class search input data is null"
        );
    }
    if (input.enc != StringEncoding::ascii &&
            input.enc != StringEncoding::utf8 &&
            input.enc != StringEncoding::ascii_or_utf8) {
        throw std::invalid_argument(
            "character class search requires normalized UTF-8 input"
        );
    }
    if (input.len == 0)
        return 0;

    std::size_t matched_bytes = 0;
    int cursor = 0;
    while (cursor < input.len) {
        const int start = cursor;
        UChar32 code_point;
        U8_NEXT(input.ptr, cursor, input.len, code_point);
        if (code_point < 0) {
            throw std::runtime_error(
                "invalid UTF-8 byte sequence detected; try calling ci_enc_toutf8()"
            );
        }
        if (!pattern.contains(code_point))
            continue;

        const std::size_t width = static_cast<std::size_t>(cursor-start);
        if (width > std::numeric_limits<std::size_t>::max()-matched_bytes) {
            throw std::length_error(
                "character class matched byte count overflow"
            );
        }
        matched_bytes += width;

        const std::size_t output_size = output.size();
        if (merge && output_size > 0 &&
                output[output_size-1].end == start) {
            output[output_size-1].end = cursor;
        }
        else {
            output.push_back(CharacterClassRange{start, cursor});
        }
    }

    return matched_bytes;
}
} // namespace shared
} // namespace charr
