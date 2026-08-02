// Derived from stringi.
// Copyright (c) 2013-2025, Marek Gagolewski. See inst/COPYRIGHTS.

#include "collation_search.h"

#include <unicode/stringpiece.h>
#include <unicode/ustring.h>
#include <unicode/utf16.h>

#include <limits>
#include <new>
#include <stdexcept>

namespace charr {
namespace shared {

CollationInputs::CollationInputs() noexcept : values_(), missing_()
{
}


void CollationInputs::resize(std::size_t size)
{
    values_.resize(size);
    missing_.resize(size);
}


void CollationInputs::set(
    std::size_t index, const StringView& source
)
{
    if (index >= values_.size() || index >= missing_.size())
        throw std::out_of_range("collation input index out of bounds");

    if (source.is_na()) {
        missing_[index] = 1;
        return;
    }
    if (source.len < 0 || (source.ptr == nullptr && source.len != 0))
        throw std::invalid_argument("invalid collation input string view");
    if (source.enc != StringEncoding::ascii &&
            source.enc != StringEncoding::utf8 &&
            source.enc != StringEncoding::ascii_or_utf8) {
        throw std::invalid_argument(
            "collation input must be normalized to UTF-8"
        );
    }

    static const char empty = '\0';
    const char* data = source.ptr == nullptr ? &empty : source.ptr;
    icu::UnicodeString& value = values_[index];
    value.setTo(
        icu::UnicodeString::fromUTF8(
            icu::StringPiece(data, source.len)
        )
    );
    if (value.isBogus())
        throw std::bad_alloc();
    missing_[index] = 0;
}


void CollationInputs::set_missing(std::size_t index)
{
    if (index >= values_.size() || index >= missing_.size())
        throw std::out_of_range("collation input index out of bounds");
    missing_[index] = 1;
}


void CollationInputs::swap_value(
    std::size_t index, icu::UnicodeString& value
)
{
    if (index >= values_.size() || index >= missing_.size())
        throw std::out_of_range("collation input index out of bounds");
    if (value.isBogus())
        throw std::bad_alloc();

    values_[index].swap(value);
    missing_[index] = 0;
}


std::size_t CollationInputs::size() const noexcept
{
    return values_.size();
}


CollationInput CollationInputs::get(std::size_t index) const
{
    if (index >= values_.size() || index >= missing_.size())
        throw std::out_of_range("collation input index out of bounds");

    const icu::UnicodeString& value = values_[index];
    return CollationInput{
        &value,
        value.getBuffer(),
        value.length(),
        missing_[index] != 0
    };
}


CollationCursor::CollationCursor() noexcept
    : values_(), active_(0), identity_(nullptr), missing_(true)
{
}


CollationInput CollationCursor::current() const noexcept
{
    if (missing_)
        return CollationInput{identity_, nullptr, -1, true};
    const icu::UnicodeString& value = values_[active_];
    return CollationInput{
        identity_, value.getBuffer(), value.length(), false
    };
}


CollationInput CollationCursor::get(
    const void* identity, const StringView& source
)
{
    if (identity == nullptr)
        throw std::invalid_argument("collation record identity is null");
    if (identity == identity_)
        return current();

    if (source.is_na()) {
        missing_ = true;
        identity_ = identity;
        return current();
    }
    if (source.len < 0 || (source.ptr == nullptr && source.len != 0))
        throw std::invalid_argument("invalid collation input string view");
    if (source.enc != StringEncoding::ascii &&
            source.enc != StringEncoding::utf8 &&
            source.enc != StringEncoding::ascii_or_utf8) {
        throw std::invalid_argument(
            "collation input must be normalized to UTF-8"
        );
    }

    static const char empty = '\0';
    const char* data = source.ptr == nullptr ? &empty : source.ptr;
    const unsigned char next = active_ == 0 ? 1 : 0;
    icu::UnicodeString& value = values_[next];
    value.setTo(
        icu::UnicodeString::fromUTF8(
            icu::StringPiece(data, source.len)
        )
    );
    if (value.isBogus())
        throw std::bad_alloc();

    active_ = next;
    missing_ = false;
    identity_ = identity;
    return current();
}


CollationPositionCursor::CollationPositionCursor(
    const CollationInput& subject
) noexcept
    : data_(subject.missing ? nullptr : subject.data),
      length_(subject.missing || subject.length < 0 ? 0 : subject.length),
      utf16_(0), position_(0)
{
}


int CollationPositionCursor::at_utf16(int target) noexcept
{
    if (data_ == nullptr || length_ <= 0)
        return 0;
    if (target < 0)
        target = 0;
    if (target > length_)
        target = length_;
    if (target < utf16_) {
        utf16_ = 0;
        position_ = 0;
    }
    while (utf16_ < target) {
        U16_FWD_1(data_, utf16_, length_);
        ++position_;
    }
    return position_;
}


CollationRange CollationPositionCursor::to_r_range(
    const CollationRange& range, bool return_length
) noexcept
{
    const int start = at_utf16(range.start)+1;
    const int end = at_utf16(range.end);
    return CollationRange{
        start, return_length ? end-start+1 : end
    };
}


CollationMatcher::CollationMatcher() noexcept
    : matcher_(nullptr), collator_(nullptr), pattern_identity_(nullptr)
{
}


CollationMatcher::~CollationMatcher() noexcept
{
    close();
}


void CollationMatcher::close() noexcept
{
    if (matcher_ != nullptr) {
        usearch_close(matcher_);
        matcher_ = nullptr;
    }
    collator_ = nullptr;
    pattern_identity_ = nullptr;
}


bool CollationMatcher::prepare(
    UCollator* collator,
    const CollationInput& subject,
    const CollationInput& pattern,
    UErrorCode& status
) noexcept {
    status = U_ZERO_ERROR;
    if (collator == nullptr || subject.missing || pattern.missing ||
            subject.length < 0 || pattern.length <= 0 ||
            (subject.data == nullptr && subject.length != 0) ||
            pattern.data == nullptr || pattern.identity == nullptr) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return false;
    }

    if (matcher_ == nullptr || collator_ != collator) {
        close();
        matcher_ = usearch_openFromCollator(
            pattern.data, pattern.length,
            subject.data, subject.length,
            collator, nullptr, &status
        );
        if (U_FAILURE(status) || matcher_ == nullptr) {
            if (U_SUCCESS(status))
                status = U_MEMORY_ALLOCATION_ERROR;
            close();
            return false;
        }
        collator_ = collator;
        pattern_identity_ = pattern.identity;
    }
    else {
        if (pattern_identity_ != pattern.identity) {
            usearch_setPattern(
                matcher_, pattern.data, pattern.length, &status
            );
            if (U_FAILURE(status)) {
                close();
                return false;
            }
            pattern_identity_ = pattern.identity;
        }

        status = U_ZERO_ERROR;
        usearch_setText(
            matcher_, subject.data, subject.length, &status
        );
        if (U_FAILURE(status)) {
            close();
            return false;
        }
    }

    return true;
}


int CollationMatcher::count(
    UCollator* collator,
    const CollationInput& subject,
    const CollationInput& pattern,
    UErrorCode& status
) noexcept {
    if (!prepare(collator, subject, pattern, status))
        return 0;

    int result = 0;
    status = U_ZERO_ERROR;
    while (usearch_next(matcher_, &status) != USEARCH_DONE &&
            U_SUCCESS(status)) {
        ++result;
    }
    if (U_FAILURE(status))
        close();
    return result;
}


bool CollationMatcher::contains(
    UCollator* collator,
    const CollationInput& subject,
    const CollationInput& pattern,
    UErrorCode& status
) noexcept {
    if (!prepare(collator, subject, pattern, status))
        return false;

    const int match = usearch_next(matcher_, &status);
    if (U_FAILURE(status)) {
        close();
        return false;
    }
    return match != USEARCH_DONE;
}


bool CollationMatcher::find_first(
    UCollator* collator,
    const CollationInput& subject,
    const CollationInput& pattern,
    CollationRange& match,
    UErrorCode& status
) noexcept {
    match = CollationRange{0, 0};
    if (!prepare(collator, subject, pattern, status))
        return false;

    const int start = usearch_first(matcher_, &status);
    if (U_FAILURE(status)) {
        close();
        return false;
    }
    if (start == USEARCH_DONE)
        return false;

    match.start = start;
    match.end = start + usearch_getMatchedLength(matcher_);
    return true;
}


void CollationMatcher::find_all(
    UCollator* collator,
    const CollationInput& subject,
    const CollationInput& pattern,
    std::vector<CollationRange>& matches,
    UErrorCode& status
)
{
    matches.clear();
    if (!prepare(collator, subject, pattern, status))
        return;

    int start = usearch_first(matcher_, &status);
    while (start != USEARCH_DONE && U_SUCCESS(status)) {
        matches.push_back(CollationRange{
            start, start + usearch_getMatchedLength(matcher_)
        });
        start = usearch_next(matcher_, &status);
    }
    if (U_FAILURE(status))
        close();
}


CollationSplitResult CollationMatcher::split(
    UCollator* collator,
    const CollationInput& subject,
    const CollationInput& pattern,
    int n,
    bool omit_empty,
    bool tokens_only,
    std::vector<CollationRange>& fields,
    UErrorCode& status
)
{
    fields.clear();
    status = U_ZERO_ERROR;
    if (!prepare(collator, subject, pattern, status))
        return CollationSplitResult::ok;
    if (n >= std::numeric_limits<int>::max()-1)
        return CollationSplitResult::limit_too_large;
    if (n == 0)
        return CollationSplitResult::ok;

    const int field_limit = n < 0
        ? std::numeric_limits<int>::max()
        : n;
    int search_limit = field_limit;
    if (tokens_only && search_limit < std::numeric_limits<int>::max())
        ++search_limit;

    fields.push_back(CollationRange{0, 0});
    int field_count = 1;
    while (field_count < search_limit && U_SUCCESS(status)) {
        const int start = usearch_next(matcher_, &status);
        if (start == USEARCH_DONE || U_FAILURE(status))
            break;
        const int end = start + usearch_getMatchedLength(matcher_);
        CollationRange& current = fields.back();
        if (omit_empty && current.start == start) {
            current.start = end;
        }
        else {
            current.end = start;
            fields.push_back(CollationRange{end, end});
            ++field_count;
        }
    }
    if (U_FAILURE(status)) {
        close();
        return CollationSplitResult::ok;
    }

    fields.back().end = subject.length;
    if (omit_empty && fields.back().start == fields.back().end)
        fields.pop_back();

    if (tokens_only && field_limit < std::numeric_limits<int>::max()) {
        while (fields.size() > static_cast<std::size_t>(field_limit))
            fields.pop_back();
    }
    return CollationSplitResult::ok;
}


bool CollationMatcher::starts_with(
    UCollator* collator,
    const CollationInput& subject,
    const CollationInput& pattern,
    int from,
    UErrorCode& status
) noexcept {
    status = U_ZERO_ERROR;
    if (subject.missing || from < 0 || from >= subject.length ||
            subject.data == nullptr) {
        if (from < 0 || from > subject.length)
            status = U_ILLEGAL_ARGUMENT_ERROR;
        return false;
    }

    const CollationInput suffix{
        subject.identity,
        subject.data + from,
        subject.length - from,
        false
    };
    if (!prepare(collator, suffix, pattern, status))
        return false;

    const int match = usearch_first(matcher_, &status);
    if (U_FAILURE(status)) {
        close();
        return false;
    }
    return match == 0;
}


bool CollationMatcher::ends_with(
    UCollator* collator,
    const CollationInput& subject,
    const CollationInput& pattern,
    int to,
    UErrorCode& status
) noexcept {
    status = U_ZERO_ERROR;
    if (subject.missing || to <= 0 || to > subject.length ||
            subject.data == nullptr) {
        if (to < 0 || to > subject.length)
            status = U_ILLEGAL_ARGUMENT_ERROR;
        return false;
    }

    const CollationInput prefix{
        subject.identity, subject.data, to, false
    };
    if (!prepare(collator, prefix, pattern, status))
        return false;

    const int match = usearch_last(matcher_, &status);
    if (U_FAILURE(status)) {
        close();
        return false;
    }
    return match != USEARCH_DONE &&
        match + usearch_getMatchedLength(matcher_) == to;
}


int utf16_start_offset(
    const CollationInput& subject, int position
) noexcept {
    if (subject.missing || subject.length <= 0 || subject.data == nullptr)
        return 0;
    if (position == 1)
        return 0;

    int offset;
    if (position >= 0) {
        int remaining = position-1;
        offset = 0;
        U16_FWD_N(subject.data, offset, subject.length, remaining);
    }
    else {
        int remaining = -position;
        offset = subject.length;
        U16_BACK_N(subject.data, 0, offset, remaining);
    }
    return offset;
}


int utf16_end_offset(
    const CollationInput& subject, int position
) noexcept {
    if (subject.missing || subject.length <= 0 || subject.data == nullptr)
        return 0;
    if (position == -1)
        return subject.length;

    int offset;
    if (position >= 0) {
        int remaining = position;
        offset = 0;
        U16_FWD_N(subject.data, offset, subject.length, remaining);
    }
    else {
        int remaining = -position-1;
        offset = subject.length;
        U16_BACK_N(subject.data, 0, offset, remaining);
    }
    return offset;
}


void write_collation_replacement(
    const CollationInput& subject,
    const CollationInput& replacement,
    const std::vector<CollationRange>& ranges,
    icu::UnicodeString& output
)
{
    if (subject.missing || replacement.missing || subject.length < 0 ||
            replacement.length < 0 ||
            (subject.data == nullptr && subject.length != 0) ||
            (replacement.data == nullptr && replacement.length != 0)) {
        throw std::invalid_argument("invalid collation replacement input");
    }

    std::size_t removed = 0;
    int previous_end = 0;
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        const CollationRange& range = ranges[i];
        if (range.start < previous_end || range.start < 0 ||
                range.end < range.start || range.end > subject.length) {
            throw std::out_of_range(
                "collation replacement range is out of bounds"
            );
        }
        removed += static_cast<std::size_t>(range.end-range.start);
        previous_end = range.end;
    }

    const std::size_t source_size =
        static_cast<std::size_t>(subject.length);
    if (removed > source_size)
        throw std::length_error("collation replacement range overflow");

    const std::size_t unmatched = source_size-removed;
    const std::size_t replacement_size =
        static_cast<std::size_t>(replacement.length);
    const std::size_t maximum =
        static_cast<std::size_t>(std::numeric_limits<int>::max());
    if (replacement_size > 0 && ranges.size() >
            (maximum-unmatched)/replacement_size) {
        throw std::length_error(
            "collation replacement output exceeds ICU's length limit"
        );
    }
    const std::size_t output_size =
        unmatched+ranges.size()*replacement_size;

    icu::UnicodeString answer(
        static_cast<int>(output_size), static_cast<UChar>(0xfffd), 0
    );
    if (answer.isBogus())
        throw std::bad_alloc();

    int source_offset = 0;
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        const CollationRange& range = ranges[i];
        answer.append(
            subject.data, source_offset, range.start-source_offset
        );
        answer.append(replacement.data, 0, replacement.length);
        source_offset = range.end;
    }
    answer.append(
        subject.data, source_offset, subject.length-source_offset
    );
    if (answer.isBogus())
        throw std::bad_alloc();

    output.swap(answer);
}

} // namespace shared
} // namespace charr
