#ifndef CHARR_SHARED_READ_LINES_H
#define CHARR_SHARED_READ_LINES_H

#include <unicode/utf8.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/stat.h>

#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#endif

namespace charr { namespace shared { namespace read_lines {

struct LineSlice {
    int begin;
    int length;
    bool ascii;
};

struct InvalidSequence {
    int begin;
    int length;
};

struct ScanResult {
    std::vector<LineSlice> lines;
    std::vector<InvalidSequence> invalid;
    bool embedded_nul;
};

enum class FileCondition {
    directory,
    open_failed
};

class FileConditionError {
private:
    FileCondition condition_;
    int error_;

public:
    FileConditionError(FileCondition condition, int error)
        : condition_(condition), error_(error)
    {
    }

    FileCondition condition() const noexcept { return condition_; }
    int error() const noexcept { return error_; }
};

class FileHandle {
private:
    std::FILE* handle_;

public:
    explicit FileHandle(std::FILE* handle) : handle_(handle) {}
    ~FileHandle()
    {
        if (handle_ != NULL)
            std::fclose(handle_);
    }

    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    std::FILE* get() const noexcept { return handle_; }
};

template<class Allocate>
inline int read_file_into(const char* path, Allocate allocate)
{
    struct stat info;
    if (stat(path, &info) == 0 && S_ISDIR(info.st_mode))
        throw FileConditionError(FileCondition::directory, 0);

    errno = 0;
    std::FILE* raw_handle = std::fopen(path, "rb");
    if (raw_handle == NULL) {
        const int saved_errno = errno;
        throw FileConditionError(
            FileCondition::open_failed,
            saved_errno != 0 ? saved_errno : ENOENT
        );
    }
    FileHandle handle(raw_handle);

    if (std::fseek(handle.get(), 0, SEEK_END) != 0)
        throw std::runtime_error("cannot determine file size");
    const long end = std::ftell(handle.get());
    if (end < 0)
        throw std::runtime_error("cannot determine file size");
    if (static_cast<unsigned long long>(end) >
            static_cast<unsigned long long>(
                std::numeric_limits<int>::max()
            ))
        throw std::length_error("file is too large");
    std::rewind(handle.get());

    const size_t size = static_cast<size_t>(end);
    char* destination = allocate(size);
    if (size > 0 && (destination == NULL || std::fread(
            destination, 1, size, handle.get()
        ) != size))
        throw std::runtime_error("cannot read the connection");
    return static_cast<int>(size);
}

inline std::vector<char> read_file(const char* path)
{
    std::vector<char> bytes;
    read_file_into(path, [&bytes](size_t size) -> char* {
        bytes.resize(size);
        return bytes.empty() ? NULL : bytes.data();
    });
    return bytes;
}

// `stri_split_lines()` keeps an empty field after a terminal separator.
// File reading and `stri_split_lines1()` deliberately do not, so callers
// select that distinction independently from ordinary empty-field omission.
inline ScanResult scan_utf8(
    const char* data, int length, bool omit_empty,
    bool keep_trailing_empty = false
)
{
    ScanResult result;
    result.embedded_nul = false;
    if (length < 0 || (data == NULL && length != 0))
        throw std::invalid_argument("invalid UTF-8 byte view");

    size_t likely_lines = static_cast<size_t>(length) / 32U + 1U;
    if (likely_lines > 131072U)
        likely_lines = 131072U;
    result.lines.reserve(likely_lines);

    int line_begin = 0;
    bool line_ascii = true;
    bool pending_line = true;

    for (int i=0; i<length;) {
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

    return result;
}

inline std::vector<char> repair_utf8(
    const char* data, int length,
    const std::vector<InvalidSequence>& invalid
)
{
    if (invalid.empty())
        return std::vector<char>();

    std::vector<char> repaired;
    const size_t extra = invalid.size() >
        (std::numeric_limits<size_t>::max()-static_cast<size_t>(length))/2U
        ? std::numeric_limits<size_t>::max()
        : invalid.size()*2U;
    if (extra > static_cast<size_t>(std::numeric_limits<int>::max())-
            static_cast<size_t>(length))
        throw std::length_error("converted UTF-8 string exceeds R string size");
    repaired.reserve(static_cast<size_t>(length)+extra);

    int copied = 0;
    for (size_t i=0; i<invalid.size(); ++i) {
        const InvalidSequence& sequence = invalid[i];
        repaired.insert(
            repaired.end(), data+copied, data+sequence.begin
        );
        repaired.push_back(static_cast<char>(0xef));
        repaired.push_back(static_cast<char>(0xbf));
        repaired.push_back(static_cast<char>(0xbd));
        copied = sequence.begin+sequence.length;
    }
    repaired.insert(repaired.end(), data+copied, data+length);
    return repaired;
}

inline std::string invalid_warning(
    const char* data, const InvalidSequence& invalid
)
{
    if (invalid.length < 1 || invalid.length > 4) {
        return "some input data in the current source encoding could not be converted to Unicode";
    }

    char message[256];
    int used = std::snprintf(message, sizeof(message), "input data ");
    for (int i=0; i<invalid.length && used > 0 &&
            static_cast<size_t>(used) < sizeof(message); ++i) {
        const int added = std::snprintf(
            message+used, sizeof(message)-static_cast<size_t>(used),
            "\\x%02x", static_cast<unsigned int>(
                static_cast<unsigned char>(data[invalid.begin+i])
            )
        );
        if (added < 0)
            break;
        used += added;
    }
    std::snprintf(
        message+used, sizeof(message)-static_cast<size_t>(used),
        " in the current source encoding could not be converted to Unicode"
    );
    return std::string(message);
}

inline bool has_utf8_bom(const char* data, int length) noexcept
{
    return length >= 3 &&
        static_cast<unsigned char>(data[0]) == 0xefU &&
        static_cast<unsigned char>(data[1]) == 0xbbU &&
        static_cast<unsigned char>(data[2]) == 0xbfU;
}

} } } // namespace charr::shared::read_lines

#endif
