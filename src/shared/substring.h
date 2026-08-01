#ifndef CHARR_SHARED_SUBSTRING_H
#define CHARR_SHARED_SUBSTRING_H

#include "lint.h"
#include "string_view.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace charr {
namespace shared {
namespace substring {

struct ByteRange {
    int begin;
    int end;
};

enum class ReplacementWarning : unsigned char {
    none,
    replacement_zero,
    recycling,
    recycling_rule
};

struct ReplacementResult {
    StringView value;
    ReplacementWarning warning;
};

class Utf8Indexer {
public:
    CHARR_NEUTRAL_HELPER Utf8Indexer() noexcept;

    CHARR_NEUTRAL_HELPER void reset(
        const char* data, int length, bool ascii
    ) noexcept;

    CHARR_NEUTRAL_HELPER int forward(int position) noexcept;
    CHARR_NEUTRAL_HELPER int backward(int position) noexcept;
    CHARR_NEUTRAL_HELPER int codepoint_count() noexcept;
    CHARR_NEUTRAL_HELPER ByteRange range(int from, int to) noexcept;

private:
    const char* data_;
    int length_;
    bool ascii_;
    int last_forward_codepoint_;
    int last_forward_byte_;
    int last_backward_codepoint_;
    int last_backward_byte_;
};

class CHARR_OWNER_TYPE ReplacementAssembler {
public:
    CHARR_CXX_HELPER ReplacementAssembler() = default;

    CHARR_CXX_HELPER ReplacementResult build(
        const StringView& source,
        const StringView* replacements, int replacement_length,
        const int* from, int from_length,
        const int* to, int to_length,
        const int* lengths, int lengths_length,
        int vectorize_length, bool omit_na
    );

private:
    std::vector<char> bytes_;

    CHARR_CXX_HELPER void append(const char* data, std::size_t length);
};

CHARR_NEUTRAL_HELPER int nonnegative_index(std::int64_t value) noexcept;
CHARR_NEUTRAL_HELPER int length_endpoint(int from, int length) noexcept;
CHARR_NEUTRAL_HELPER int replacement_all_from(
    int from, int codepoints
) noexcept;
CHARR_NEUTRAL_HELPER int replacement_all_to(
    int to, bool is_length, int from, int codepoints
) noexcept;
CHARR_NEUTRAL_HELPER int recycled_order_begin(
    int source_length, int output_length
) noexcept;
CHARR_NEUTRAL_HELPER int recycled_order_next(
    int index, int source_length, int output_length
) noexcept;
CHARR_NEUTRAL_HELPER int recycling_length(
    const int* lengths, int count, bool& warning
) noexcept;
CHARR_CXX_HELPER std::size_t checked_output_size(
    std::size_t current, std::size_t additional
);

} // namespace substring
} // namespace shared
} // namespace charr

#endif
