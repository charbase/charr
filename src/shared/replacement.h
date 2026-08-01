#ifndef CHARR_SHARED_REPLACEMENT_H
#define CHARR_SHARED_REPLACEMENT_H

#include "lint.h"
#include "string_view.h"

#include <cstddef>
#include <vector>

namespace charr {
namespace shared {

struct ByteRange {
    int start;
    int end;
};


CHARR_CXX_HELPER std::size_t checked_replacement_size(
    const StringView& subject,
    std::size_t matched_bytes,
    const std::vector<ByteRange>& ranges,
    const StringView& replacement
);

CHARR_CXX_HELPER void write_replacement(
    const StringView& subject,
    const std::vector<ByteRange>& ranges,
    const StringView& replacement,
    char* output,
    std::size_t output_size
);

CHARR_CXX_HELPER bool replacement_is_ascii(
    const StringView& subject,
    const std::vector<ByteRange>& ranges,
    const StringView& replacement
);

} // namespace shared
} // namespace charr

#endif
