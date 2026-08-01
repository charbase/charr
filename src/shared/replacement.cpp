#include "replacement.h"

#include <cstring>
#include <limits>
#include <stdexcept>

#include <Rinternals.h>

namespace charr {
namespace shared {

namespace replacement {

CHARR_CXX_HELPER void validate_inputs(
    const StringView& subject,
    const StringView& replacement
)
{
    if (subject.is_na() || replacement.is_na())
        throw std::invalid_argument("replacement requires non-missing input");
    if (subject.len < 0 || replacement.len < 0)
        throw std::invalid_argument("replacement input has negative length");
    if (subject.ptr == nullptr || replacement.ptr == nullptr)
        throw std::invalid_argument("replacement input data is null");
    if ((subject.enc != StringEncoding::ascii &&
            subject.enc != StringEncoding::utf8 &&
            subject.enc != StringEncoding::ascii_or_utf8) ||
            (replacement.enc != StringEncoding::ascii &&
            replacement.enc != StringEncoding::utf8 &&
            replacement.enc != StringEncoding::ascii_or_utf8)) {
        throw std::invalid_argument(
            "replacement requires normalized UTF-8 input"
        );
    }
}


CHARR_CXX_HELPER std::size_t validate_ranges(
    const StringView& subject,
    const std::vector<ByteRange>& ranges
)
{
    std::size_t matched_bytes = 0;
    int previous_end = 0;
    const std::size_t count = ranges.size();
    for (std::size_t i = 0; i < count; ++i) {
        const ByteRange& range = ranges[i];
        if (range.start < previous_end || range.start < 0 ||
                range.end <= range.start || range.end > subject.len) {
            throw std::out_of_range("replacement range is out of bounds");
        }
        const std::size_t width =
            static_cast<std::size_t>(range.end-range.start);
        if (width > std::numeric_limits<std::size_t>::max()-matched_bytes)
            throw std::length_error("matched byte count overflow");
        matched_bytes += width;
        previous_end = range.end;
    }
    return matched_bytes;
}


CHARR_NEUTRAL_HELPER bool span_is_ascii(
    const char* data,
    int length
) noexcept
{
    for (int i = 0; i < length; ++i) {
        if (static_cast<unsigned char>(data[i]) > 0x7fU)
            return false;
    }
    return true;
}

} // namespace replacement

using namespace replacement;


std::size_t checked_replacement_size(
    const StringView& subject,
    std::size_t matched_bytes,
    const std::vector<ByteRange>& ranges,
    const StringView& replacement
)
{
    validate_inputs(subject, replacement);

    const std::size_t subject_size = static_cast<std::size_t>(subject.len);
    if (matched_bytes > subject_size)
        throw std::invalid_argument("matched byte count exceeds the subject");

    const std::size_t unmatched_bytes = subject_size-matched_bytes;
    const std::size_t replacement_size =
        static_cast<std::size_t>(replacement.len);
    const std::size_t count = ranges.size();
    if (replacement_size > 0 && count >
            (std::numeric_limits<std::size_t>::max()-unmatched_bytes) /
                replacement_size) {
        throw std::length_error(
            "Elements of character vectors (CHARSXPs) are limited to "
            "2^31-1 bytes"
        );
    }

    const std::size_t result = unmatched_bytes+count*replacement_size;
    if (result > static_cast<std::size_t>(R_LEN_T_MAX)) {
        throw std::length_error(
            "Elements of character vectors (CHARSXPs) are limited to "
            "2^31-1 bytes"
        );
    }
    return result;
}


void write_replacement(
    const StringView& subject,
    const std::vector<ByteRange>& ranges,
    const StringView& replacement,
    char* output,
    std::size_t output_size
)
{
    validate_inputs(subject, replacement);

    const std::size_t matched_bytes = validate_ranges(subject, ranges);
    const std::size_t count = ranges.size();
    const std::size_t expected_size = checked_replacement_size(
        subject, matched_bytes, ranges, replacement
    );
    if (output_size != expected_size)
        throw std::length_error("replacement buffer size mismatch");
    if (output_size > 0 && output == nullptr)
        throw std::invalid_argument("replacement output buffer is null");

    std::size_t used = 0;
    int previous_end = 0;
    const std::size_t replacement_size =
        static_cast<std::size_t>(replacement.len);
    for (std::size_t i = 0; i < count; ++i) {
        const ByteRange& range = ranges[i];
        const std::size_t prefix =
            static_cast<std::size_t>(range.start-previous_end);
        if (prefix > 0) {
            std::memcpy(output+used, subject.ptr+previous_end, prefix);
            used += prefix;
        }
        if (replacement_size > 0) {
            std::memcpy(output+used, replacement.ptr, replacement_size);
            used += replacement_size;
        }
        previous_end = range.end;
    }

    const std::size_t suffix =
        static_cast<std::size_t>(subject.len-previous_end);
    if (suffix > 0) {
        std::memcpy(output+used, subject.ptr+previous_end, suffix);
        used += suffix;
    }
    if (used != output_size)
        throw std::logic_error("replacement writer size mismatch");
}


bool replacement_is_ascii(
    const StringView& subject,
    const std::vector<ByteRange>& ranges,
    const StringView& replacement
)
{
    validate_inputs(subject, replacement);
    validate_ranges(subject, ranges);

    bool ascii = true;
    int previous_end = 0;
    const std::size_t count = ranges.size();
    for (std::size_t i = 0; i < count; ++i) {
        const ByteRange& range = ranges[i];
        if (!span_is_ascii(
                subject.ptr+previous_end,
                range.start-previous_end
            )) {
            ascii = false;
        }
        previous_end = range.end;
    }

    if (!span_is_ascii(
            subject.ptr+previous_end,
            subject.len-previous_end
        )) {
        ascii = false;
    }
    if (count > 0 && !span_is_ascii(replacement.ptr, replacement.len))
        ascii = false;
    return ascii;
}

} // namespace shared
} // namespace charr
