#include "line_split.h"

#include <cstddef>
#include <stdexcept>

#include <unicode/utf8.h>

namespace charr {
namespace shared {
namespace line_split {

CHARR_CXX_HELPER void scan_utf8(
    const char* data, int length, bool omit_empty,
    bool keep_trailing_empty, ScanResult& result
)
{
    if (length < 0 || (data == nullptr && length != 0))
        throw std::invalid_argument("invalid UTF-8 byte view");

    result.lines.clear();
    result.invalid.clear();
    result.embedded_nul = false;

    std::size_t likely_lines = static_cast<std::size_t>(length) / 32U + 1U;
    if (likely_lines > 131072U)
        likely_lines = 131072U;
    if (result.lines.capacity() < likely_lines)
        result.lines.reserve(likely_lines);

    int line_begin = 0;
    bool line_ascii = true;
    bool pending_line = true;

    for (int i = 0; i < length;) {
        unsigned char first = static_cast<unsigned char>(data[i]);
        if (first > 0x0dU && first < 0x80U) {
            do {
                ++i;
                if (i >= length)
                    break;
                first = static_cast<unsigned char>(data[i]);
            } while (first > 0x0dU && first < 0x80U);
            continue;
        }

        const int codepoint_begin = i;
        UChar32 codepoint;
        if (first < 0x80U) {
            codepoint = static_cast<UChar32>(first);
            ++i;
        }
        else {
            U8_NEXT(data, i, length, codepoint);
            if (codepoint < 0) {
                int consumed = i-codepoint_begin;
                if (consumed <= 0) {
                    consumed = 1;
                    i = codepoint_begin+1;
                }
                result.invalid.push_back(
                    InvalidSequence{codepoint_begin, consumed}
                );
                line_ascii = false;
                continue;
            }
        }

        bool newline = false;
        switch (codepoint) {
        case 0:
            result.embedded_nul = true;
            break;
        case 0x0d:
            if (i < length && data[i] == '\n')
                ++i;
            newline = true;
            break;
        case 0x0a:
        case 0x0b:
        case 0x0c:
        case 0x85:
        case 0x2028:
        case 0x2029:
            newline = true;
            break;
        default:
            if (codepoint > 0x7f)
                line_ascii = false;
            break;
        }

        if (!newline)
            continue;

        const int field_length = codepoint_begin-line_begin;
        if (!omit_empty || field_length > 0) {
            result.lines.push_back(LineSlice{
                line_begin, field_length, line_ascii
            });
        }
        line_begin = i;
        line_ascii = true;
        pending_line = i < length;
    }

    if (pending_line || (keep_trailing_empty && !omit_empty)) {
        const int field_length = length-line_begin;
        if (!omit_empty || field_length > 0) {
            result.lines.push_back(LineSlice{
                line_begin, field_length, line_ascii
            });
        }
    }
}

} // namespace line_split
} // namespace shared
} // namespace charr
