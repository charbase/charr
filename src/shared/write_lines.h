#ifndef CHARR_SHARED_WRITE_LINES_H
#define CHARR_SHARED_WRITE_LINES_H

#include "lint.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <type_traits>

namespace charr {
namespace shared {
namespace write_lines {

// An open, write, or close failure, carrying errno. The entry point turns it
// into an R error naming the path; other exceptions pass through unchanged.
class FileWriteError {
private:
    int error_;

public:
    CHARR_NEUTRAL_HELPER explicit FileWriteError(int error) noexcept
        : error_(error)
    {
    }

    CHARR_NEUTRAL_HELPER int error() const noexcept
    {
        return error_;
    }
};

static_assert(
    std::is_trivially_destructible<FileWriteError>::value,
    "FileWriteError must not own resources"
);

// The file handle begins empty so it can live in the Frame before the primary
// unwind callback. reset(), write(), and close() are native-only operations.
// Records are staged in an owned buffer and passed to fwrite() in large
// blocks, so stdio is not entered once per record. The destructor closes a
// handle left open by an error or an R unwind and discards staged bytes.
class CHARR_OWNER_TYPE FileWriter {
private:
    std::FILE* handle_ = nullptr;
    std::size_t used_ = 0;
    std::array<char, 65536> buffer_ = {};

    CHARR_CXX_HELPER void flush();

public:
    CHARR_NEUTRAL_HELPER FileWriter() noexcept = default;
    CHARR_CXX_HELPER ~FileWriter() noexcept;

    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;
    FileWriter(FileWriter&&) = delete;
    FileWriter& operator=(FileWriter&&) = delete;

    CHARR_CXX_HELPER void reset(const char* path);
    CHARR_CXX_HELPER void write(const char* data, int length);
    // Write UTF-8 bytes, replacing each maximal ill-formed subsequence with
    // U+FFFD as ICU's UTF-8 conversion does, so the output matches the
    // encode route used for connections and other encodings.
    CHARR_CXX_HELPER void write_utf8(const char* data, int length);
    CHARR_CXX_HELPER void close();
    CHARR_CXX_HELPER void close_noexcept() noexcept;
};

} // namespace write_lines
} // namespace shared
} // namespace charr

#endif
