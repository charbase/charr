#include "read_lines.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <stdexcept>

#include <sys/stat.h>

#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#endif

namespace charr {
namespace shared {
namespace read_lines {

FileReader::~FileReader() noexcept
{
    close();
}

void FileReader::reset(const char* path)
{
    close();

    struct stat info;
    const bool have_info = ::stat(path, &info) == 0;
    if (have_info && S_ISDIR(info.st_mode))
        throw FileConditionError(FileCondition::directory, 0);

    errno = 0;
    handle_ = std::fopen(path, "rb");
    if (handle_ == nullptr) {
        const int saved_errno = errno;
        throw FileConditionError(
            FileCondition::open_failed,
            saved_errno != 0 ? saved_errno : ENOENT
        );
    }

    capacity_ = 4096;
    if (have_info && info.st_size > 0) {
        const auto size = static_cast<unsigned long long>(info.st_size);
        if (size > static_cast<unsigned long long>(
                std::numeric_limits<int>::max()
            )) {
            throw std::length_error("file is too large");
        }
        // Leave room for EOF on the first read of a regular file.
        capacity_ = std::max(capacity_, static_cast<std::size_t>(size) + 1);
    }
}

bool FileReader::read(char* destination)
{
    constexpr std::size_t limit = std::numeric_limits<int>::max();
    const std::size_t room = capacity_ - size_;
    const std::size_t count = std::fread(destination + size_, 1, room, handle_);
    if (std::ferror(handle_))
        throw std::runtime_error("cannot read the connection");
    size_ += count;
    if (size_ > limit)
        throw std::length_error("file is too large");
    if (count < room)
        return false;
    capacity_ = std::min(capacity_ * 2, limit + 1);
    return true;
}

void FileReader::close() noexcept
{
    if (handle_ != nullptr) {
        std::fclose(handle_);
        handle_ = nullptr;
    }
    capacity_ = 0;
    size_ = 0;
}

void repair_utf8(
    const char* data,
    int length,
    const std::vector<line_split::InvalidSequence>& invalid,
    std::vector<char>& output
)
{
    if (length < 0 || (data == nullptr && length != 0))
        throw std::invalid_argument("invalid UTF-8 byte view");

    std::size_t output_length = static_cast<std::size_t>(length);
    int copied = 0;
    for (std::size_t i = 0; i < invalid.size(); ++i) {
        const line_split::InvalidSequence& sequence = invalid[i];
        if (sequence.length <= 0 || sequence.begin < copied ||
                sequence.begin > length - sequence.length) {
            throw std::invalid_argument("invalid UTF-8 repair range");
        }

        if (sequence.length < 3) {
            const std::size_t added = static_cast<std::size_t>(
                3 - sequence.length
            );
            if (output_length >
                    static_cast<std::size_t>(
                        std::numeric_limits<int>::max()
                    ) - added) {
                throw std::length_error(
                    "converted UTF-8 string exceeds R string size"
                );
            }
            output_length += added;
        }
        else {
            output_length -= static_cast<std::size_t>(
                sequence.length - 3
            );
        }
        copied = sequence.begin + sequence.length;
    }

    output.resize(output_length);
    char* destination = output_length == 0 ? nullptr : output.data();
    copied = 0;
    std::size_t written = 0;
    for (std::size_t i = 0; i < invalid.size(); ++i) {
        const line_split::InvalidSequence& sequence = invalid[i];
        const std::size_t prefix = static_cast<std::size_t>(
            sequence.begin - copied
        );
        if (prefix > 0) {
            std::memcpy(destination + written, data + copied, prefix);
            written += prefix;
        }
        destination[written++] = static_cast<char>(0xef);
        destination[written++] = static_cast<char>(0xbf);
        destination[written++] = static_cast<char>(0xbd);
        copied = sequence.begin + sequence.length;
    }

    const std::size_t suffix = static_cast<std::size_t>(length - copied);
    if (suffix > 0)
        std::memcpy(destination + written, data + copied, suffix);
}

void format_invalid_warning(
    const char* data,
    const line_split::InvalidSequence& invalid,
    char* output,
    std::size_t capacity
) noexcept {
    if (output == nullptr || capacity == 0)
        return;

    if (invalid.length < 1 || invalid.length > 4) {
        std::snprintf(
            output, capacity,
            "some input data in the current source encoding could not be "
            "converted to Unicode"
        );
        return;
    }

    int used = std::snprintf(output, capacity, "input data ");
    if (used < 0) {
        output[0] = '\0';
        return;
    }
    std::size_t position = static_cast<std::size_t>(used);
    if (position >= capacity)
        position = capacity - 1;

    for (int i = 0; i < invalid.length && position < capacity - 1; ++i) {
        const int added = std::snprintf(
            output + position, capacity - position,
            "\\x%02x", static_cast<unsigned int>(
                static_cast<unsigned char>(data[invalid.begin + i])
            )
        );
        if (added < 0)
            break;
        position += static_cast<std::size_t>(added);
        if (position >= capacity)
            position = capacity - 1;
    }

    std::snprintf(
        output + position, capacity - position,
        " in the current source encoding could not be converted to Unicode"
    );
}

bool has_utf8_bom(const char* data, int length) noexcept
{
    return length >= 3 &&
        static_cast<unsigned char>(data[0]) == 0xefU &&
        static_cast<unsigned char>(data[1]) == 0xbbU &&
        static_cast<unsigned char>(data[2]) == 0xbfU;
}

} // namespace read_lines
} // namespace shared
} // namespace charr
