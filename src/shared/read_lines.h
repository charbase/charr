#ifndef CHARR_SHARED_READ_LINES_H
#define CHARR_SHARED_READ_LINES_H

#include "line_split.h"
#include "lint.h"

#include <cstddef>
#include <cstdio>
#include <type_traits>
#include <vector>

namespace charr {
namespace shared {
namespace read_lines {

enum class FileCondition {
    directory,
    open_failed
};

class FileConditionError {
private:
    FileCondition condition_;
    int error_;

public:
    CHARR_NEUTRAL_HELPER FileConditionError(
        FileCondition condition, int error
    ) noexcept : condition_(condition), error_(error)
    {
    }

    CHARR_NEUTRAL_HELPER FileCondition condition() const noexcept
    {
        return condition_;
    }

    CHARR_NEUTRAL_HELPER int error() const noexcept
    {
        return error_;
    }
};

static_assert(
    std::is_trivially_destructible<FileConditionError>::value,
    "FileConditionError must remain safe across the R-only warning phase"
);

// The file handle begins empty so it can live in the Frame before the primary
// unwind callback. reset(), read(), and close() are native-only operations.
class CHARR_OWNER_TYPE FileReader {
private:
    std::FILE* handle_ = nullptr;
    int size_ = 0;

public:
    CHARR_NEUTRAL_HELPER FileReader() noexcept = default;
    CHARR_CXX_HELPER ~FileReader() noexcept;

    FileReader(const FileReader&) = delete;
    FileReader& operator=(const FileReader&) = delete;
    FileReader(FileReader&&) = delete;
    FileReader& operator=(FileReader&&) = delete;

    CHARR_CXX_HELPER void reset(const char* path);
    CHARR_CXX_HELPER void read(char* destination);
    CHARR_CXX_HELPER void close() noexcept;

    CHARR_NEUTRAL_HELPER int size() const noexcept
    {
        return size_;
    }
};

constexpr std::size_t invalid_warning_size = 256;

CHARR_CXX_HELPER void repair_utf8(
    const char* data,
    int length,
    const std::vector<line_split::InvalidSequence>& invalid,
    std::vector<char>& output
);

CHARR_NEUTRAL_HELPER void format_invalid_warning(
    const char* data,
    const line_split::InvalidSequence& invalid,
    char* output,
    std::size_t capacity
) noexcept;

CHARR_NEUTRAL_HELPER bool has_utf8_bom(
    const char* data, int length
) noexcept;

} // namespace read_lines
} // namespace shared
} // namespace charr

#endif
