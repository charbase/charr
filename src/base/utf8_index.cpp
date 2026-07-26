#include "utf8_input.h"

#include <unicode/utf8.h>

#include <algorithm>

namespace charr {
namespace base {

R_len_t IndexedUtf8Input::UChar32_to_UTF8_index_back(
    R_len_t index, R_len_t position
)
{
    const Utf8Record& record = get(index);
    if (position <= 0)
        return record.len;
    if (record.isASCII())
        return std::max(record.len-position, 0);

    if (last_back_record_ != record.ptr) {
        last_back_codepoint_ = 0;
        last_back_utf8_ = record.len;
        last_back_record_ = record.ptr;
    }

    R_len_t codepoint = 0;
    R_len_t byte = record.len;
    if (last_back_codepoint_ > 0) {
        if (position < last_back_codepoint_) {
            if (last_back_codepoint_-position < position) {
                codepoint = last_back_codepoint_;
                byte = last_back_utf8_;
                while (codepoint > position && byte < record.len) {
                    U8_FWD_1(
                        reinterpret_cast<const uint8_t*>(record.ptr),
                        byte, record.len
                    );
                    --codepoint;
                }
                last_back_codepoint_ = position;
                last_back_utf8_ = byte;
                return byte;
            }
        }
        else {
            codepoint = last_back_codepoint_;
            byte = last_back_utf8_;
        }
    }

    while (codepoint < position && byte > 0) {
        U8_BACK_1(
            reinterpret_cast<const uint8_t*>(record.ptr), 0, byte
        );
        ++codepoint;
    }
    last_back_codepoint_ = codepoint;
    last_back_utf8_ = byte;
    return byte;
}

R_len_t IndexedUtf8Input::UChar32_to_UTF8_index_fwd(
    R_len_t index, R_len_t position
)
{
    if (position <= 0)
        return 0;
    const Utf8Record& record = get(index);
    if (record.isASCII())
        return std::min(position, record.len);

    if (last_fwd_record_ != record.ptr) {
        last_fwd_codepoint_ = 0;
        last_fwd_utf8_ = 0;
        last_fwd_record_ = record.ptr;
    }

    R_len_t codepoint = 0;
    R_len_t byte = 0;
    if (last_fwd_codepoint_ > 0) {
        if (position < last_fwd_codepoint_) {
            if (last_fwd_codepoint_-position < position) {
                codepoint = last_fwd_codepoint_;
                byte = last_fwd_utf8_;
                while (codepoint > position && byte > 0) {
                    U8_BACK_1(
                        reinterpret_cast<const uint8_t*>(record.ptr), 0, byte
                    );
                    --codepoint;
                }
                last_fwd_codepoint_ = position;
                last_fwd_utf8_ = byte;
                return byte;
            }
        }
        else {
            codepoint = last_fwd_codepoint_;
            byte = last_fwd_utf8_;
        }
    }

    while (codepoint < position && byte < record.len) {
        U8_FWD_1(
            reinterpret_cast<const uint8_t*>(record.ptr), byte, record.len
        );
        ++codepoint;
    }
    last_fwd_codepoint_ = codepoint;
    last_fwd_utf8_ = byte;
    return byte;
}

void IndexedUtf8Input::UTF8_to_UChar32_index(
    R_len_t index, int* first, int* second, int size,
    int first_adjustment, int second_adjustment
)
{
    const Utf8Record& record = get(index);
    if (record.isASCII()) {
        for (int i = 0; i < size; ++i) {
            first[i] += first_adjustment;
            second[i] += second_adjustment;
        }
        return;
    }

    int first_index = 0;
    int second_index = 0;
    int byte = 0;
    int codepoint = 0;
    while (byte < record.len &&
            (first_index < size || second_index < size)) {
        if (first_index < size && first[first_index] <= byte) {
#ifndef NDEBUG
            if (first_index < size-1 &&
                    first[first_index] >= first[first_index+1])
                throw StriException("invalid sorted UTF-8 index array");
#endif
            first[first_index] = codepoint+first_adjustment;
            ++first_index;
        }
        if (second_index < size && second[second_index] <= byte) {
#ifndef NDEBUG
            if (second_index < size-1 &&
                    second[second_index] >= second[second_index+1])
                throw StriException("invalid sorted UTF-8 index array");
#endif
            second[second_index] = codepoint+second_adjustment;
            ++second_index;
        }
        U8_FWD_1(record.ptr, byte, record.len);
        ++codepoint;
    }

    if (first_index < size && first[first_index] <= record.len) {
        first[first_index] = codepoint+first_adjustment;
        ++first_index;
    }
    if (second_index < size && second[second_index] <= record.len) {
        second[second_index] = codepoint+second_adjustment;
        ++second_index;
    }

#ifndef NDEBUG
    if (byte >= record.len &&
            (first_index < size || second_index < size))
        throw StriException("UTF-8 index exceeds record length");
#endif
}

} // namespace base
} // namespace charr
