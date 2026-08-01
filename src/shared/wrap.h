#ifndef CHARR_SHARED_WRAP_H
#define CHARR_SHARED_WRAP_H

#include "lint.h"
#include "string_view.h"
#include "boundary_iterator.h"

#include <unicode/normalizer2.h>
#include <unicode/uniset.h>
#include <unicode/utypes.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace charr {
namespace shared {
namespace wrap {

struct Options {
    const char* locale;
    int width;
    double exponent;
    bool whitespace_only;
    bool use_length;
    bool normalize;
};


struct OpenResult {
    UErrorCode status;
    bool root_fallback;
};


struct Line {
    const char* prefix;
    std::size_t prefix_size;
    const char* body;
    std::size_t body_size;
    std::size_t size;
    bool ascii;
};


struct Joined {
    std::size_t size;
    bool ascii;
};


// Owns one copied prefix and its precomputed code-point and display widths.
// reset() accepts an already normalized UTF-8 view.
class CHARR_OWNER_TYPE LineStart {
public:
    CHARR_CXX_HELPER LineStart() noexcept;
    CHARR_CXX_HELPER ~LineStart() noexcept;

    LineStart(const LineStart&) = delete;
    LineStart& operator=(const LineStart&) = delete;
    LineStart(LineStart&&) = delete;
    LineStart& operator=(LineStart&&) = delete;

    CHARR_CXX_HELPER void reset(
        const StringView& source, int spaces
    );

    CHARR_NEUTRAL_HELPER bool is_na() const noexcept
    {
        return missing_;
    }

    CHARR_NEUTRAL_HELPER int count() const noexcept
    {
        return count_;
    }

    CHARR_NEUTRAL_HELPER std::int64_t width() const noexcept
    {
        return width_;
    }

    CHARR_NEUTRAL_HELPER bool is_ascii() const noexcept
    {
        return ascii_;
    }

    CHARR_NEUTRAL_HELPER const char* data() const noexcept
    {
        return bytes_.data();
    }

    CHARR_NEUTRAL_HELPER std::size_t size() const noexcept
    {
        return bytes_.size();
    }

private:
    std::string bytes_;
    int count_;
    std::int64_t width_;
    bool ascii_;
    bool missing_;
};


// Reusable native state for one wrapping operation. Input and output remain
// length-delimited; the class never calls R or owns an R object.
class CHARR_OWNER_TYPE Engine {
public:
    CHARR_CXX_HELPER Engine() noexcept;
    CHARR_CXX_HELPER ~Engine() noexcept;

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    CHARR_CXX_HELPER OpenResult reset(const Options& options);

    // Plan one non-missing, normalized UTF-8 record. Returned ICU failures
    // follow the same status channel used by the other shared ICU owners.
    CHARR_CXX_HELPER UErrorCode plan(
        const StringView& source,
        const LineStart& first,
        const LineStart& later
    );

    CHARR_NEUTRAL_HELPER int line_count() const noexcept
    {
        return line_count_;
    }

    CHARR_CXX_HELPER CHARR_ALWAYS_INLINE Line line(
        int index,
        const LineStart& first,
        const LineStart& later,
        bool classify_ascii
    ) const {
        if (index < 0 || index >= line_count_)
            throw std::out_of_range("wrapped line index is out of range");

        if (passthrough_) {
            return Line{
                record_data_, 0, record_data_,
                static_cast<std::size_t>(record_length_),
                static_cast<std::size_t>(record_length_),
                classify_ascii && record_ascii_
            };
        }

        const LineStart& start = index == 0 ? first : later;
        int begin = 0;
        int end = direct_end_;
        if (direct_end_ < 0) {
            if (index > 0) {
                const int previous = wrap_after_[
                    static_cast<std::size_t>(index-1)
                ];
                begin = end_orig_[static_cast<std::size_t>(previous)];
            }
            if (index < static_cast<int>(wrap_after_.size())) {
                const int current = wrap_after_[
                    static_cast<std::size_t>(index)
                ];
                end = end_trim_[static_cast<std::size_t>(current)];
            }
            else {
                end = end_trim_.back();
            }
        }

        const std::size_t body_size = static_cast<std::size_t>(end-begin);
        const std::size_t output_size = checked_output_sum(
            start.size(), body_size
        );
        const bool body_ascii = classify_ascii &&
            (record_ascii_ || bytes_are_ascii(record_data_+begin, end-begin));
        return Line{
            start.data(), start.size(), record_data_+begin, body_size,
            output_size, classify_ascii && start.is_ascii() && body_ascii
        };
    }

    CHARR_CXX_HELPER Joined joined(
        const LineStart& first,
        const LineStart& later,
        bool classify_ascii
    ) const {
        if (line_count_ <= 0)
            throw std::logic_error("wrapped record has not been planned");

        std::size_t size = static_cast<std::size_t>(line_count_-1);
        bool ascii = true;
        for (int i = 0; i < line_count_; ++i) {
            const Line current = line(i, first, later, classify_ascii);
            size = checked_output_sum(size, current.size);
            ascii = ascii && current.ascii;
        }
        return Joined{size, ascii};
    }

    CHARR_NEUTRAL_HELPER static void write_line(
        const Line& line, char* destination
    ) noexcept {
        if (line.prefix_size > 0)
            std::memcpy(destination, line.prefix, line.prefix_size);
        if (line.body_size > 0) {
            std::memcpy(
                destination+line.prefix_size, line.body, line.body_size
            );
        }
    }

    CHARR_CXX_HELPER void write_joined(
        char* destination,
        const LineStart& first,
        const LineStart& later
    ) const {
        std::size_t position = 0;
        for (int i = 0; i < line_count_; ++i) {
            if (i > 0)
                destination[position++] = '\n';
            const Line current = line(i, first, later, false);
            if (current.size > 0)
                write_line(current, destination+position);
            position += current.size;
        }
    }

private:
    BoundaryIterator iterator_;
    icu::UnicodeSet linebreaks_;
    icu::UnicodeSet whitespaces_;
    const icu::Normalizer2* normalizer_;
    bool sets_ready_;
    std::string prepared_;
    std::string normalized_;

    std::vector<int> end_orig_;
    std::vector<std::uint32_t> widths_orig_;
    std::vector<std::uint32_t> widths_trim_;
    std::vector<int> end_trim_;
    std::vector<int> wrap_after_;
    std::vector<double> cost_;
    std::vector<double> best_;
    std::vector<std::uint8_t> breaks_;

    Options options_;
    const char* record_data_;
    int record_length_;
    int direct_end_;
    int line_count_;
    bool record_ascii_;
    bool passthrough_;

    CHARR_CXX_HELPER UErrorCode prepare_record(
        const StringView& source
    );
    CHARR_NEUTRAL_HELPER void clear_words() noexcept;
    CHARR_CXX_HELPER bool ascii_fits(
        std::int64_t first_width, int& output_end
    ) const;
    CHARR_CXX_HELPER void add_word(int begin, int end);
    CHARR_NEUTRAL_HELPER bool fits_one_line(
        std::int64_t first_width
    ) const noexcept;
    CHARR_CXX_HELPER void greedy(
        std::int64_t first_width, std::int64_t later_width
    );
    CHARR_CXX_HELPER void dynamic(
        std::int64_t first_width, std::int64_t later_width
    );

    CHARR_CXX_HELPER static std::size_t checked_output_sum(
        std::size_t left, std::size_t right
    ) {
        const std::size_t maximum = static_cast<std::size_t>(
            std::numeric_limits<int>::max()
        );
        if (left > maximum || right > maximum-left) {
            throw std::length_error(
                "wrapped string exceeds R's string length limit"
            );
        }
        return left+right;
    }

    CHARR_NEUTRAL_HELPER static bool bytes_are_ascii(
        const char* data, int length
    ) noexcept {
        for (int i = 0; i < length; ++i) {
            if (static_cast<unsigned char>(data[i]) >= 0x80U)
                return false;
        }
        return true;
    }
};

} // namespace wrap
} // namespace shared
} // namespace charr

#endif
