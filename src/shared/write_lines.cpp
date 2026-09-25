#include "write_lines.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>

#include <unicode/utf8.h>

namespace charr {
namespace shared {
namespace write_lines {

FileWriter::~FileWriter() noexcept
{
    close_noexcept();
}

void FileWriter::reset(const char* path)
{
    close_noexcept();
    errno = 0;
    handle_ = std::fopen(path, "wb");
    if (handle_ == nullptr)
        throw FileWriteError(errno != 0 ? errno : EIO);
}

void FileWriter::flush()
{
    if (used_ == 0)
        return;
    errno = 0;
    if (std::fwrite(buffer_.data(), 1, used_, handle_) != used_ ||
            std::ferror(handle_)) {
        throw FileWriteError(errno != 0 ? errno : EIO);
    }
    used_ = 0;
}

void FileWriter::write(const char* data, int length)
{
    if (length < 0 || (data == nullptr && length != 0))
        throw std::invalid_argument("invalid output byte view");
    if (handle_ == nullptr)
        throw std::logic_error("file is not open for writing");

    std::size_t remaining = static_cast<std::size_t>(length);
    while (remaining > 0) {
        if (used_ == buffer_.size())
            flush();
        const std::size_t available = buffer_.size()-used_;
        const std::size_t count = remaining < available ? remaining : available;
        std::memcpy(buffer_.data()+used_, data, count);
        used_ += count;
        data += count;
        remaining -= count;
    }
}

void FileWriter::write_utf8(const char* data, int length)
{
    if (length < 0 || (data == nullptr && length != 0))
        throw std::invalid_argument("invalid output byte view");

    static const char replacement[] = "\xef\xbf\xbd";
    int run_begin = 0;
    for (int i = 0; i < length;) {
        if (static_cast<unsigned char>(data[i]) < 0x80U) {
            ++i;
            continue;
        }

        const int begin = i;
        UChar32 codepoint;
        U8_NEXT(data, i, length, codepoint);
        if (codepoint >= 0)
            continue;
        if (i <= begin)
            i = begin+1;
        write(data+run_begin, begin-run_begin);
        write(replacement, 3);
        run_begin = i;
    }
    write(data == nullptr ? nullptr : data+run_begin, length-run_begin);
}

void FileWriter::close()
{
    if (handle_ == nullptr)
        return;
    flush();
    std::FILE* handle = handle_;
    handle_ = nullptr;
    errno = 0;
    const bool failed = std::ferror(handle) != 0;
    if (std::fclose(handle) != 0 || failed)
        throw FileWriteError(errno != 0 ? errno : EIO);
}

void FileWriter::close_noexcept() noexcept
{
    if (handle_ != nullptr) {
        std::fclose(handle_);
        handle_ = nullptr;
    }
    used_ = 0;
}

} // namespace write_lines
} // namespace shared
} // namespace charr
