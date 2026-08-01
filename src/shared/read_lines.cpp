#include "read_lines.h"

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
    if (::stat(path, &info) == 0 && S_ISDIR(info.st_mode))
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

    if (std::fseek(handle_, 0, SEEK_END) != 0)
        throw std::runtime_error("cannot determine file size");
    const long end = std::ftell(handle_);
    if (end < 0)
        throw std::runtime_error("cannot determine file size");
    if (static_cast<unsigned long long>(end) >
            static_cast<unsigned long long>(
                std::numeric_limits<int>::max()
            )) {
        throw std::length_error("file is too large");
    }
    std::rewind(handle_);
    size_ = static_cast<int>(end);
}

void FileReader::read(char* destination)
{
    if (size_ > 0 && (destination == nullptr || std::fread(
            destination, 1, static_cast<std::size_t>(size_), handle_
        ) != static_cast<std::size_t>(size_))) {
        throw std::runtime_error("cannot read the connection");
    }
}

void FileReader::close() noexcept
{
    if (handle_ != nullptr) {
        std::fclose(handle_);
        handle_ = nullptr;
    }
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
