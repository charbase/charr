#include "repeat.h"

#include <cstring>

namespace charr {
namespace shared {

CHARR_NEUTRAL_HELPER bool checked_repeat_size(
    std::size_t source_length,
    int times,
    std::size_t limit,
    std::size_t& output_length
) noexcept
{
    if (times <= 0 || source_length == 0) {
        output_length = 0;
        return true;
    }

    const std::size_t count = static_cast<std::size_t>(times);
    if (source_length > limit / count)
        return false;

    output_length = source_length * count;
    return true;
}


CHARR_NEUTRAL_HELPER void repeat_bytes(
    char* destination,
    const char* source,
    std::size_t source_length,
    std::size_t output_length
) noexcept
{
    if (output_length == 0)
        return;

    std::memcpy(destination, source, source_length);
    std::size_t written = source_length;
    while (written < output_length) {
        const std::size_t remaining = output_length - written;
        const std::size_t amount = written < remaining ? written : remaining;
        std::memcpy(destination + written, destination, amount);
        written += amount;
    }
}

} // namespace shared
} // namespace charr
