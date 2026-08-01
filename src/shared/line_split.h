#ifndef CHARR_SHARED_LINE_SPLIT_H
#define CHARR_SHARED_LINE_SPLIT_H

#include "lint.h"

#include <vector>

namespace charr {
namespace shared {
namespace line_split {

struct LineSlice {
    int begin;
    int length;
    bool ascii;
};

struct InvalidSequence {
    int begin;
    int length;
};

struct CHARR_OWNER_TYPE ScanResult {
    CHARR_CXX_HELPER ScanResult() = default;

    std::vector<LineSlice> lines;
    std::vector<InvalidSequence> invalid;
    bool embedded_nul = false;
};

// Scan normalized UTF-8 into byte slices. Ordinary vector splitting keeps an
// empty field after a terminal separator; scalar splitting and file reading do
// not. The caller selects that distinction independently from omit_empty.
CHARR_CXX_HELPER void scan_utf8(
    const char* data, int length, bool omit_empty,
    bool keep_trailing_empty, ScanResult& result
);

} // namespace line_split
} // namespace shared
} // namespace charr

#endif
