// Derived from stringi.
// Copyright (c) 2013-2025, Marek Gagolewski. See inst/COPYRIGHTS.

#include "wrap.h"

#include <unicode/bytestream.h>
#include <unicode/stringpiece.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>
#include <unicode/utf8.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace charr {
namespace shared {
namespace wrap {

namespace wrap_detail {

const char empty = '\0';


CHARR_NEUTRAL_HELPER bool has_bom(
    const char* data, int length
) noexcept {
    return length >= 3 &&
        static_cast<unsigned char>(data[0]) == 0xefU &&
        static_cast<unsigned char>(data[1]) == 0xbbU &&
        static_cast<unsigned char>(data[2]) == 0xbfU;
}


CHARR_NEUTRAL_HELPER bool is_ascii(
    const char* data, int length
) noexcept {
    for (int i = 0; i < length; ++i) {
        if (static_cast<unsigned char>(data[i]) >= 0x80U)
            return false;
    }
    return true;
}


CHARR_NEUTRAL_HELPER int ascii_char_width(
    unsigned char value
) noexcept {
    return value >= 0x20U && value != 0x7fU ? 1 : 0;
}


CHARR_NEUTRAL_HELPER int character_width(UChar32 value) noexcept
{
    if (value >= 0 && value <= 0x7f)
        return ascii_char_width(static_cast<unsigned char>(value));

    const std::uint32_t category = U_GET_GC_MASK(value);
    const int east_asian = static_cast<int>(u_getIntPropertyValue(
        value, UCHAR_EAST_ASIAN_WIDTH
    ));

    if (value == 0x00ad)
        return 1;
    if (value == 0x200b)
        return 0;
    if (category &
            (U_GC_MN_MASK | U_GC_ME_MASK | U_GC_CF_MASK | U_GC_CC_MASK)) {
        return 0;
    }

    const int hangul = static_cast<int>(u_getIntPropertyValue(
        value, UCHAR_HANGUL_SYLLABLE_TYPE
    ));
    if (hangul == U_HST_VOWEL_JAMO || hangul == U_HST_TRAILING_JAMO)
        return 0;
    if (value >= 0xfe00 && value <= 0xfe0f)
        return 0;

#if U_ICU_VERSION_MAJOR_NUM >= 57
    if (u_hasBinaryProperty(value, UCHAR_EMOJI_MODIFIER))
        return 0;
#endif

    if (east_asian == U_EA_FULLWIDTH || east_asian == U_EA_WIDE)
        return 2;
    if (category & U_GC_SO_MASK)
        return 2;

#if U_ICU_VERSION_MAJOR_NUM >= 57
    if (east_asian == U_EA_NEUTRAL &&
            u_hasBinaryProperty(value, UCHAR_EMOJI_PRESENTATION)) {
        return 2;
    }
#endif

    return 1;
}


CHARR_NEUTRAL_HELPER int character_width_with_context(
    UChar32 value, UChar32 previous, bool& reset
) noexcept {
    if (reset) {
        previous = 0;
        reset = false;
    }

#if U_ICU_VERSION_MAJOR_NUM >= 57
    if (previous == 0x200d &&
            (u_hasBinaryProperty(value, UCHAR_EMOJI_MODIFIER) ||
             u_hasBinaryProperty(value, UCHAR_EMOJI_PRESENTATION) ||
             value == 0x2640 || value == 0x2642 || value == 0x26a7 ||
             value == 0x2695 || value == 0x2696 || value == 0x1f5e8 ||
             value == 0x1f32b || value == 0x2708 || value == 0x2764 ||
             value == 0x2744 || value == 0x2620)) {
        return 0;
    }
    if (previous >= 0x1f1e6 && previous <= 0x1f1ff &&
            value >= 0x1f1e6 && value <= 0x1f1ff) {
        reset = true;
        return 0;
    }
#endif

    return character_width(value);
}


CHARR_CXX_HELPER int code_point_count(const char* data, int length)
{
    int cursor = 0;
    int count = 0;
    while (cursor < length) {
        UChar32 code_point;
        U8_NEXT(data, cursor, length, code_point);
        if (code_point < 0) {
            throw std::runtime_error(
                "invalid UTF-8 byte sequence detected; try calling ci_enc_toutf8()"
            );
        }
        ++count;
    }
    return count;
}


CHARR_CXX_HELPER std::int64_t string_width(
    const char* data, int length
)
{
    std::int64_t width = 0;
    UChar32 previous;
    UChar32 current = 0;
    int cursor = 0;
    bool reset = true;
    while (cursor < length) {
        previous = current;
        const unsigned char byte = static_cast<unsigned char>(data[cursor]);
        if (byte < 0x80U) {
            current = static_cast<UChar32>(byte);
            ++cursor;
            if (reset)
                reset = false;
            width += ascii_char_width(byte);
            continue;
        }

        U8_NEXT(data, cursor, length, current);
        if (current < 0) {
            throw std::runtime_error(
                "invalid UTF-8 byte sequence detected; try calling ci_enc_toutf8()"
            );
        }
        width += character_width_with_context(current, previous, reset);
    }
    return width;
}


CHARR_CXX_HELPER std::size_t checked_sum(
    std::size_t left, std::size_t right
) {
    const std::size_t maximum = static_cast<std::size_t>(
        std::numeric_limits<int>::max()
    );
    if (left > maximum || right > maximum-left)
        throw std::length_error("wrapped string exceeds R's string length limit");
    return left+right;
}

} // namespace wrap_detail

using namespace wrap_detail;


LineStart::LineStart() noexcept
    : bytes_(), count_(0), width_(0), ascii_(false), missing_(true)
{
}


LineStart::~LineStart() noexcept = default;


void LineStart::reset(const StringView& source, int spaces)
{
    if (spaces < 0)
        throw std::invalid_argument("negative wrap indentation");

    bytes_.clear();
    count_ = 0;
    width_ = 0;
    ascii_ = false;
    missing_ = source.is_na();
    if (missing_)
        return;
    if (source.len < 0 || (source.ptr == nullptr && source.len != 0))
        throw std::invalid_argument("invalid wrap prefix string view");
    if (source.enc != StringEncoding::ascii &&
            source.enc != StringEncoding::utf8 &&
            source.enc != StringEncoding::ascii_or_utf8) {
        throw std::invalid_argument("wrap prefix is not normalized UTF-8");
    }

    const char* data = source.ptr == nullptr ? &empty : source.ptr;
    const std::size_t output_size = checked_sum(
        static_cast<std::size_t>(source.len),
        static_cast<std::size_t>(spaces)
    );
    bytes_.reserve(output_size);
    bytes_.assign(data, static_cast<std::size_t>(source.len));
    bytes_.append(static_cast<std::size_t>(spaces), ' ');
    ascii_ = source.enc == StringEncoding::ascii ||
        (source.enc == StringEncoding::ascii_or_utf8 &&
            wrap_detail::is_ascii(data, source.len));
    count_ = (ascii_ ? source.len : code_point_count(data, source.len)) + spaces;
    width_ = string_width(data, source.len) + spaces;
}


Engine::Engine() noexcept
    : iterator_(), linebreaks_(), whitespaces_(),
      normalizer_(nullptr),
      sets_ready_(false), prepared_(), normalized_(),
      end_orig_(), widths_orig_(),
      widths_trim_(), end_trim_(), wrap_after_(), cost_(), best_(), breaks_(),
      options_{nullptr, 0, 0.0, false, false, false},
      record_data_(&empty), record_length_(0), direct_end_(-1),
      line_count_(0), record_ascii_(true), passthrough_(false)
{
}


Engine::~Engine() noexcept = default;


OpenResult Engine::reset(const Options& options)
{
    options_ = options;
    normalizer_ = nullptr;
    record_data_ = &empty;
    record_length_ = 0;
    direct_end_ = -1;
    line_count_ = 0;
    record_ascii_ = true;
    passthrough_ = false;
    clear_words();

    const BoundaryOpenResult opened = iterator_.reset(BoundaryOptions{
        options.locale, nullptr, 0, UBRK_LINE, nullptr, 0, false
    });
    UErrorCode status = opened.status;
    const bool root_fallback = opened.root_fallback;
    if (U_FAILURE(status))
        return OpenResult{status, root_fallback};

    status = U_ZERO_ERROR;
    if (!sets_ready_) {
        linebreaks_.applyPattern(
            icu::UnicodeString::fromUTF8(
                icu::StringPiece(
                    "[\\u000A-\\u000D\\u0085\\u2028\\u2029]"
                )
            ),
            status
        );
        if (U_FAILURE(status))
            return OpenResult{status, root_fallback};

        status = U_ZERO_ERROR;
        whitespaces_.applyPattern(
            icu::UnicodeString::fromUTF8(
                icu::StringPiece("\\p{White_space}")
            ),
            status
        );
        if (U_FAILURE(status))
            return OpenResult{status, root_fallback};

        linebreaks_.freeze();
        whitespaces_.freeze();
        if (linebreaks_.isBogus() || whitespaces_.isBogus()) {
            return OpenResult{
                U_MEMORY_ALLOCATION_ERROR, root_fallback
            };
        }
        sets_ready_ = true;
    }

    if (options.normalize) {
        status = U_ZERO_ERROR;
        normalizer_ = icu::Normalizer2::getNFCInstance(status);
        if (normalizer_ == nullptr && U_SUCCESS(status))
            status = U_MEMORY_ALLOCATION_ERROR;
        if (U_FAILURE(status))
            return OpenResult{status, root_fallback};
    }

    return OpenResult{U_ZERO_ERROR, root_fallback};
}


UErrorCode Engine::prepare_record(const StringView& source)
{
    if (source.is_na())
        throw std::invalid_argument("cannot wrap a missing string");
    if (source.len < 0 || (source.ptr == nullptr && source.len != 0))
        throw std::invalid_argument("invalid wrap input string view");
    if (source.enc != StringEncoding::ascii &&
            source.enc != StringEncoding::utf8 &&
            source.enc != StringEncoding::ascii_or_utf8) {
        throw std::invalid_argument("wrap input is not normalized UTF-8");
    }

    const char* input = source.ptr == nullptr ? &empty : source.ptr;
    if (!options_.normalize) {
        record_data_ = input;
        record_length_ = source.len;
        record_ascii_ = source.enc == StringEncoding::ascii ||
            (source.enc == StringEncoding::ascii_or_utf8 &&
                is_ascii(input, source.len));
        return U_ZERO_ERROR;
    }
    if (normalizer_ == nullptr)
        throw std::logic_error("wrap normalizer has not been initialized");

    prepared_.clear();
    prepared_.reserve(static_cast<std::size_t>(source.len));

    int cursor = 0;
    bool field_start = true;
    bool in_space_run = false;
    int leading_bom_stages = 3;
    while (leading_bom_stages > 0 &&
            has_bom(input+cursor, source.len-cursor)) {
        cursor += 3;
        --leading_bom_stages;
        field_start = false;
    }

    while (cursor < source.len) {
        if (field_start && has_bom(input+cursor, source.len-cursor)) {
            cursor += 3;
            field_start = false;
            continue;
        }

        const int begin = cursor;
        UChar32 code_point;
        U8_NEXT(input, cursor, source.len, code_point);
        if (code_point < 0) {
            throw std::runtime_error(
                "invalid UTF-8 byte sequence detected; try calling ci_enc_toutf8()"
            );
        }

        if (linebreaks_.contains(code_point)) {
            if (code_point == '\r' && cursor < source.len &&
                    input[cursor] == '\n') {
                ++cursor;
            }
            if (!in_space_run)
                prepared_.push_back(' ');
            in_space_run = true;
            field_start = true;
            continue;
        }

        field_start = false;
        if (code_point == ' ' || code_point == '\t') {
            if (!in_space_run)
                prepared_.push_back(' ');
            in_space_run = true;
            continue;
        }

        prepared_.append(
            input+begin, static_cast<std::size_t>(cursor-begin)
        );
        in_space_run = false;
    }

    int begin = 0;
    const int prepared_length = static_cast<int>(prepared_.size());
    while (begin < prepared_length) {
        const int previous = begin;
        UChar32 code_point;
        U8_NEXT(prepared_.data(), begin, prepared_length, code_point);
        if (!whitespaces_.contains(code_point)) {
            begin = previous;
            break;
        }
    }

    int end = prepared_length;
    while (end > begin) {
        int previous = end;
        UChar32 code_point;
        U8_PREV(prepared_.data(), 0, previous, code_point);
        if (!whitespaces_.contains(code_point))
            break;
        end = previous;
    }

    const char* trimmed = end == begin ? &empty : prepared_.data()+begin;
    const int trimmed_length = end-begin;
    if (is_ascii(trimmed, trimmed_length)) {
        record_data_ = trimmed;
        record_length_ = trimmed_length;
        record_ascii_ = true;
        return U_ZERO_ERROR;
    }

    normalized_.clear();
    UErrorCode status = U_ZERO_ERROR;
    icu::StringByteSink<std::string> sink(&normalized_);
    normalizer_->normalizeUTF8(
        0, icu::StringPiece(trimmed, trimmed_length),
        sink, nullptr, status
    );
    if (U_FAILURE(status))
        return status;
    if (normalized_.size() >
            static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("normalized string exceeds R's string length limit");
    }

    record_data_ = normalized_.empty() ? &empty : normalized_.data();
    record_length_ = static_cast<int>(normalized_.size());
    if (has_bom(record_data_, record_length_)) {
        record_data_ += 3;
        record_length_ -= 3;
    }
    record_ascii_ = is_ascii(record_data_, record_length_);
    return U_ZERO_ERROR;
}


void Engine::clear_words() noexcept
{
    end_orig_.clear();
    widths_orig_.clear();
    widths_trim_.clear();
    end_trim_.clear();
    wrap_after_.clear();
}


bool Engine::ascii_fits(
    std::int64_t first_width, int& output_end
) const
{
    if (record_length_ <= 0)
        return false;

    std::int64_t measured = 0;
    int last_width = 0;
    bool last_whitespace = false;
    bool reset = true;
    UChar32 previous = 0;
    UChar32 current = 0;
    for (int i = 0; i < record_length_; ++i) {
        const unsigned char byte = static_cast<unsigned char>(record_data_[i]);
        if (byte >= 0x80U)
            return false;
        if (byte >= 0x0aU && byte <= 0x0dU)
            throw std::runtime_error("newline character found in a string");

        previous = current;
        current = static_cast<UChar32>(byte);
        last_width = options_.use_length
            ? 1
            : character_width_with_context(current, previous, reset);
        measured += last_width;
        last_whitespace = whitespaces_.contains(current);
    }

    output_end = record_length_-(last_whitespace ? 1 : 0);
    const std::int64_t trimmed = measured-
        (last_whitespace ? last_width : 0);
    return first_width+trimmed <= options_.width;
}


void Engine::add_word(int begin, int end)
{
    std::uint32_t width_orig = 0;
    std::uint32_t width_trim = 0;
    int count_orig = 0;
    int count_trim = 0;
    int end_trim = begin;
    UChar32 previous;
    UChar32 current = 0;
    bool reset = true;

    int cursor = begin;
    while (cursor < end) {
        const int previous_byte = cursor;
        previous = current;
        U8_NEXT(record_data_, cursor, end, current);
        if (current < 0) {
            throw std::runtime_error(
                "invalid UTF-8 byte sequence detected; try calling ci_enc_toutf8()"
            );
        }
        if (linebreaks_.contains(current))
            throw std::runtime_error("newline character found in a string");

        width_orig += character_width_with_context(
            current, previous, reset
        );
        ++count_orig;
        if (whitespaces_.contains(current)) {
            width_trim = character_width_with_context(
                current, previous, reset
            );
            count_trim = 1;
            end_trim = previous_byte;
        }
        else {
            width_trim = 0;
            count_trim = 0;
            end_trim = cursor;
        }
    }

    end_orig_.push_back(end);
    widths_orig_.push_back(
        options_.use_length ? count_orig : width_orig
    );
    widths_trim_.push_back(
        options_.use_length
            ? count_orig-count_trim
            : width_orig-width_trim
    );
    end_trim_.push_back(end_trim);
}


bool Engine::fits_one_line(std::int64_t first_width) const noexcept
{
    std::int64_t measured = first_width;
    for (std::size_t i = 0; i < widths_orig_.size(); ++i)
        measured += widths_orig_[i];
    measured -= widths_orig_.back()-widths_trim_.back();
    return measured <= options_.width;
}


void Engine::greedy(
    std::int64_t first_width, std::int64_t later_width
)
{
    const int words = static_cast<int>(widths_orig_.size());
    std::int64_t current = first_width+widths_orig_[0];
    for (int i = 1; i < words; ++i) {
        if (current+widths_trim_[static_cast<std::size_t>(i)] >
                options_.width) {
            current = later_width+widths_orig_[static_cast<std::size_t>(i)];
            wrap_after_.push_back(i-1);
        }
        else {
            current += widths_orig_[static_cast<std::size_t>(i)];
        }
    }
}


void Engine::dynamic(
    std::int64_t first_width, std::int64_t later_width
)
{
    const std::size_t words = widths_orig_.size();
    if (words != 0 && words > std::numeric_limits<std::size_t>::max()/words)
        throw std::length_error("word-wrap matrix is too large");
    const std::size_t matrix_size = words*words;
    if (matrix_size > cost_.max_size() || matrix_size > breaks_.max_size())
        throw std::length_error("word-wrap matrix is too large");

    cost_.resize(matrix_size);
    best_.resize(words);
    breaks_.assign(matrix_size, 0);
#define CHARR_WRAP_INDEX(i, j) \
    static_cast<std::size_t>(i)*words+static_cast<std::size_t>(j)

    for (int i = 0; i < static_cast<int>(words); ++i) {
        std::int64_t sum = 0;
        for (int j = i; j < static_cast<int>(words); ++j) {
            if (j > i) {
                if (cost_[CHARR_WRAP_INDEX(i, j-1)] < 0.0) {
                    cost_[CHARR_WRAP_INDEX(i, j)] = -1.0;
                    continue;
                }
                sum -= widths_trim_[static_cast<std::size_t>(j-1)];
                sum += widths_orig_[static_cast<std::size_t>(j-1)];
            }
            sum += widths_trim_[static_cast<std::size_t>(j)];
            std::int64_t remaining = options_.width-sum;
            remaining -= i == 0 ? first_width : later_width;

            if (j == static_cast<int>(words)-1) {
                cost_[CHARR_WRAP_INDEX(i, j)] =
                    j == i || remaining >= 0 ? 0.0 : -1.0;
            }
            else if (j == i) {
                cost_[CHARR_WRAP_INDEX(i, j)] = remaining < 0
                    ? 0.0
                    : std::pow(static_cast<double>(remaining), options_.exponent);
            }
            else {
                cost_[CHARR_WRAP_INDEX(i, j)] = remaining < 0
                    ? -1.0
                    : std::pow(static_cast<double>(remaining), options_.exponent);
            }
        }
    }

    for (int j = 0; j < static_cast<int>(words); ++j) {
        if (cost_[CHARR_WRAP_INDEX(0, j)] >= 0.0) {
            best_[static_cast<std::size_t>(j)] =
                cost_[CHARR_WRAP_INDEX(0, j)];
            continue;
        }

        int i = 0;
        while (i <= j) {
            if (cost_[CHARR_WRAP_INDEX(i+1, j)] >= 0.0)
                break;
            ++i;
        }

        double best = best_[static_cast<std::size_t>(i)] +
            cost_[CHARR_WRAP_INDEX(i+1, j)];
        for (int k = i+1; k < j; ++k) {
            if (cost_[CHARR_WRAP_INDEX(k+1, j)] < 0.0)
                continue;
            const double candidate = best_[static_cast<std::size_t>(k)] +
                cost_[CHARR_WRAP_INDEX(k+1, j)];
            if (candidate < best) {
                best = candidate;
                i = k;
            }
        }
        for (int k = 0; k < i; ++k) {
            breaks_[CHARR_WRAP_INDEX(k, j)] =
                breaks_[CHARR_WRAP_INDEX(k, i)];
        }
        breaks_[CHARR_WRAP_INDEX(i, j)] = 1;
        best_[static_cast<std::size_t>(j)] = best;
    }

    for (int i = 0; i < static_cast<int>(words); ++i) {
        if (breaks_[CHARR_WRAP_INDEX(i, static_cast<int>(words)-1)])
            wrap_after_.push_back(i);
    }
#undef CHARR_WRAP_INDEX
}


UErrorCode Engine::plan(
    const StringView& source,
    const LineStart& first,
    const LineStart& later
) {
    if (first.is_na() || later.is_na())
        throw std::invalid_argument("missing wrap line start");

    clear_words();
    direct_end_ = -1;
    line_count_ = 0;
    passthrough_ = false;

    const UErrorCode prepared = prepare_record(source);
    if (U_FAILURE(prepared))
        return prepared;

    const std::int64_t first_width = options_.use_length
        ? first.count()
        : first.width();
    const std::int64_t later_width = options_.use_length
        ? later.count()
        : later.width();

    int ascii_end = 0;
    if (ascii_fits(first_width, ascii_end)) {
        direct_end_ = ascii_end;
        line_count_ = 1;
        return U_ZERO_ERROR;
    }

    UErrorCode status = iterator_.set_text(StringView{
        record_data_, record_length_,
        record_ascii_ ? StringEncoding::ascii : StringEncoding::utf8
    });
    if (U_FAILURE(status))
        return status;

    iterator_.first();
    BoundaryRange range{0, 0};
    int previous_end = 0;
    while (iterator_.next(range)) {
        const int match = range.end;
        bool accepted = !options_.whitespace_only;
        if (options_.whitespace_only) {
            if (match > 0 && match < record_length_) {
                UChar32 code_point;
                U8_GET(
                    reinterpret_cast<const std::uint8_t*>(record_data_),
                    0, match-1, record_length_, code_point
                );
                accepted = whitespaces_.contains(code_point);
            }
            else {
                accepted = true;
            }
        }

        if (accepted && match > 0) {
            add_word(previous_end, match);
            previous_end = match;
        }
    }

    if (end_orig_.empty()) {
        passthrough_ = true;
        line_count_ = 1;
        return U_ZERO_ERROR;
    }

    if (!fits_one_line(first_width)) {
        if (options_.exponent <= 0.0)
            greedy(first_width, later_width);
        else
            dynamic(first_width, later_width);
    }

    if (wrap_after_.size() >=
            static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("word-wrap result has too many lines");
    }
    line_count_ = static_cast<int>(wrap_after_.size()+1);
    return U_ZERO_ERROR;
}


} // namespace wrap
} // namespace shared
} // namespace charr
