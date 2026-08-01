#include "substring.h"

#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif
#include <Rinternals.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unicode/utf8.h>

namespace charr {
namespace shared {
namespace substring {

CHARR_NEUTRAL_HELPER Utf8Indexer::Utf8Indexer() noexcept
    : data_(nullptr), length_(0), ascii_(true),
      last_forward_codepoint_(0), last_forward_byte_(0),
      last_backward_codepoint_(0), last_backward_byte_(0)
{
}

void Utf8Indexer::reset(
    const char* data, int length, bool ascii
) noexcept
{
    data_ = data;
    length_ = length;
    ascii_ = ascii;
    last_forward_codepoint_ = 0;
    last_forward_byte_ = 0;
    last_backward_codepoint_ = 0;
    last_backward_byte_ = length;
}

int Utf8Indexer::forward(int position) noexcept
{
    if (position <= 0)
        return 0;
    if (ascii_)
        return std::min(position, length_);

    int codepoint = 0;
    int byte = 0;
    if (last_forward_codepoint_ > 0) {
        if (position < last_forward_codepoint_ &&
                last_forward_codepoint_-position < position) {
            codepoint = last_forward_codepoint_;
            byte = last_forward_byte_;
            while (codepoint > position && byte > 0) {
                U8_BACK_1(
                    reinterpret_cast<const std::uint8_t*>(data_), 0, byte
                );
                --codepoint;
            }
            last_forward_codepoint_ = position;
            last_forward_byte_ = byte;
            return byte;
        }
        if (position >= last_forward_codepoint_) {
            codepoint = last_forward_codepoint_;
            byte = last_forward_byte_;
        }
    }

    while (codepoint < position && byte < length_) {
        U8_FWD_1(
            reinterpret_cast<const std::uint8_t*>(data_), byte, length_
        );
        ++codepoint;
    }
    last_forward_codepoint_ = codepoint;
    last_forward_byte_ = byte;
    return byte;
}

int Utf8Indexer::backward(int position) noexcept
{
    if (position <= 0)
        return length_;
    if (ascii_)
        return length_ > position ? length_-position : 0;

    int codepoint = 0;
    int byte = length_;
    if (last_backward_codepoint_ > 0) {
        if (position < last_backward_codepoint_ &&
                last_backward_codepoint_-position < position) {
            codepoint = last_backward_codepoint_;
            byte = last_backward_byte_;
            while (codepoint > position && byte < length_) {
                U8_FWD_1(
                    reinterpret_cast<const std::uint8_t*>(data_),
                    byte, length_
                );
                --codepoint;
            }
            last_backward_codepoint_ = position;
            last_backward_byte_ = byte;
            return byte;
        }
        if (position >= last_backward_codepoint_) {
            codepoint = last_backward_codepoint_;
            byte = last_backward_byte_;
        }
    }

    while (codepoint < position && byte > 0) {
        U8_BACK_1(
            reinterpret_cast<const std::uint8_t*>(data_), 0, byte
        );
        ++codepoint;
    }
    last_backward_codepoint_ = codepoint;
    last_backward_byte_ = byte;
    return byte;
}

int Utf8Indexer::codepoint_count() noexcept
{
    if (ascii_)
        return length_;

    int codepoints = 0;
    int byte = 0;
    while (byte < length_) {
        U8_FWD_1(
            reinterpret_cast<const std::uint8_t*>(data_), byte, length_
        );
        ++codepoints;
    }
    return codepoints;
}

void ReplacementAssembler::append(
    const char* data, std::size_t length
)
{
    if (length == 0)
        return;
    if (data == nullptr)
        throw std::invalid_argument("null character input");
    const std::size_t offset = bytes_.size();
    bytes_.resize(checked_output_size(offset, length));
    std::memcpy(bytes_.data()+offset, data, length);
}

ReplacementResult ReplacementAssembler::build(
    const StringView& source,
    const StringView* replacements, int replacement_length,
    const int* from, int from_length,
    const int* to, int to_length,
    const int* lengths, int lengths_length,
    int vectorize_length, bool omit_na
)
{
    bytes_.clear();
    if (source.is_na()) {
        return ReplacementResult{
            StringView{nullptr, missing_string_length,
                       StringEncoding::missing},
            ReplacementWarning::none
        };
    }
    if (vectorize_length <= 0) {
        return ReplacementResult{source, ReplacementWarning::none};
    }
    if (replacement_length <= 0) {
        return ReplacementResult{
            StringView{nullptr, missing_string_length,
                       StringEncoding::missing},
            ReplacementWarning::replacement_zero
        };
    }

    if (!omit_na) {
        for (int i = 0; i < vectorize_length; ++i) {
            const int current_from = from[i % from_length];
            const int current_to = to
                ? to[i % to_length]
                : lengths[i % lengths_length];
            if (current_from == NA_INTEGER || current_to == NA_INTEGER) {
                return ReplacementResult{
                    StringView{nullptr, missing_string_length,
                               StringEncoding::missing},
                    ReplacementWarning::none
                };
            }
        }
        for (int i = 0; i < vectorize_length; ++i) {
            if (replacements[i % replacement_length].is_na()) {
                return ReplacementResult{
                    StringView{nullptr, missing_string_length,
                               StringEncoding::missing},
                    ReplacementWarning::none
                };
            }
        }
    }

    const char* source_data = source.len == 0 ? "" : source.ptr;
    int source_codepoints = source.len;
    if (source.enc != StringEncoding::ascii) {
        source_codepoints = 0;
        int byte = 0;
        while (byte < source.len) {
            U8_FWD_1(
                reinterpret_cast<const std::uint8_t*>(source_data),
                byte, source.len
            );
            ++source_codepoints;
        }
    }

    int replaced = 0;
    int last_position = 0;
    int byte_position = 0;
    for (int i = 0; i < vectorize_length; ++i) {
        int current_from = from[i % from_length];
        int current_to = to
            ? to[i % to_length]
            : lengths[i % lengths_length];
        const StringView& replacement =
            replacements[i % replacement_length];

        if (current_from == NA_INTEGER || current_to == NA_INTEGER ||
                replacement.is_na() || (!to && current_to < 0)) {
            continue;
        }

        ++replaced;
        current_from = replacement_all_from(
            current_from, source_codepoints
        );
        current_to = replacement_all_to(
            current_to, lengths != nullptr,
            current_from, source_codepoints
        );
        if (last_position > current_from) {
            throw std::invalid_argument(
                "index ranges must be sorted and mutually disjoint"
            );
        }

        const int previous_byte = byte_position;
        while (last_position < current_from) {
            U8_FWD_1(
                reinterpret_cast<const std::uint8_t*>(source_data),
                byte_position, source.len
            );
            ++last_position;
        }
        append(
            source_data+previous_byte,
            static_cast<std::size_t>(byte_position-previous_byte)
        );
        append(
            replacement.len == 0 ? "" : replacement.ptr,
            static_cast<std::size_t>(replacement.len)
        );

        while (last_position < current_to) {
            U8_FWD_1(
                reinterpret_cast<const std::uint8_t*>(source_data),
                byte_position, source.len
            );
            ++last_position;
        }
    }

    append(
        source_data+byte_position,
        static_cast<std::size_t>(source.len-byte_position)
    );
    bool ascii = true;
    for (std::size_t i = 0; i < bytes_.size(); ++i) {
        if (static_cast<unsigned char>(bytes_[i]) > 0x7fU) {
            ascii = false;
            break;
        }
    }
    return ReplacementResult{
        StringView{
            bytes_.empty() ? "" : bytes_.data(),
            static_cast<int>(bytes_.size()),
            ascii ? StringEncoding::ascii : StringEncoding::utf8
        },
        replaced > 0 && vectorize_length % replacement_length != 0
            ? ReplacementWarning::recycling
            : ReplacementWarning::none
    };
}

ByteRange Utf8Indexer::range(int from, int to) noexcept
{
    const int begin = from >= 0
        ? forward(nonnegative_index(static_cast<std::int64_t>(from)-1))
        : backward(nonnegative_index(-static_cast<std::int64_t>(from)));
    const int end = to >= 0
        ? forward(to)
        : backward(nonnegative_index(-static_cast<std::int64_t>(to)-1));
    return ByteRange{begin, end};
}

int nonnegative_index(std::int64_t value) noexcept
{
    if (value <= 0)
        return 0;
    if (value >= static_cast<std::int64_t>(
            std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(value);
}

int length_endpoint(int from, int length) noexcept
{
    const std::int64_t endpoint = static_cast<std::int64_t>(from)+
        static_cast<std::int64_t>(length)-1;
    const std::int64_t limit = std::numeric_limits<int>::max();
    if (from < 0 && endpoint >= 0)
        return -1;
    if (endpoint >= limit)
        return std::numeric_limits<int>::max();
    if (endpoint <= -limit)
        return -std::numeric_limits<int>::max();
    return static_cast<int>(endpoint);
}

int replacement_all_from(int from, int codepoints) noexcept
{
    std::int64_t position = from;
    if (position < 0)
        position = static_cast<std::int64_t>(codepoints)+position+1;
    if (position <= 0)
        position = 1;
    --position;
    if (position >= codepoints)
        return codepoints;
    return static_cast<int>(position);
}

int replacement_all_to(
    int to, bool is_length, int from, int codepoints
) noexcept
{
    std::int64_t position;
    if (is_length) {
        position = static_cast<std::int64_t>(from)+(to > 0 ? to : 0);
    }
    else {
        position = to;
        if (position < 0)
            position = static_cast<std::int64_t>(codepoints)+position+1;
        if (position < from)
            position = from;
    }
    if (position >= codepoints)
        return codepoints;
    return static_cast<int>(position);
}

int recycled_order_begin(
    int source_length, int output_length
) noexcept
{
    return source_length <= 0 ? output_length : 0;
}

int recycled_order_next(
    int index, int source_length, int output_length
) noexcept
{
    if (source_length <= 0)
        return output_length;
    if (index == output_length-1-(output_length % source_length))
        return output_length;
    index += source_length;
    return index >= output_length
        ? (index % source_length)+1
        : index;
}

int recycling_length(
    const int* lengths, int count, bool& warning
) noexcept
{
    warning = false;
    int output = 0;
    for (int i = 0; i < count; ++i) {
        if (lengths[i] <= 0)
            return 0;
        if (lengths[i] > output)
            output = lengths[i];
    }
    for (int i = 0; i < count; ++i) {
        if (output % lengths[i] != 0) {
            warning = true;
            break;
        }
    }
    return output;
}

std::size_t checked_output_size(
    std::size_t current, std::size_t additional
)
{
    if (additional > std::numeric_limits<std::size_t>::max()-current)
        throw std::length_error("character output size overflow");
    const std::size_t output = current+additional;
    if (output > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::length_error("character output is too large");
    return output;
}

} // namespace substring
} // namespace shared
} // namespace charr
