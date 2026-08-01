#ifndef CHARR_SHARED_BOUNDARY_ITERATOR_H
#define CHARR_SHARED_BOUNDARY_ITERATOR_H

#include "lint.h"
#include "string_view.h"

#include <unicode/ubrk.h>
#include <unicode/utext.h>

#include <cstddef>
#include <cstdint>

namespace charr {
namespace shared {

// Parsed before the Frame region begins. Locale and rule bytes, and the skip
// table, point into R_alloc storage that remains valid for the full .Call.
struct BoundaryOptions {
    const char* locale;
    const char* rules;
    std::int32_t rules_length;
    UBreakIteratorType type;
    const std::int32_t* skip_rules;
    std::size_t skip_size;
    bool custom_rules;
};


struct BoundaryOpenResult {
    UErrorCode status;
    bool root_fallback;
};


struct BoundaryRange {
    int start;
    int end;
};


// Owns one ICU iterator and its reusable UText. Construction is empty so the
// owner can live in an entry point's Frame and acquire both handles later.
class CHARR_OWNER_TYPE BoundaryIterator {
public:
    CHARR_CXX_HELPER BoundaryIterator() noexcept;
    CHARR_CXX_HELPER ~BoundaryIterator() noexcept;

    BoundaryIterator(const BoundaryIterator&) = delete;
    BoundaryIterator& operator=(const BoundaryIterator&) = delete;
    BoundaryIterator(BoundaryIterator&&) = delete;
    BoundaryIterator& operator=(BoundaryIterator&&) = delete;

    CHARR_CXX_HELPER BoundaryOpenResult reset(
        const BoundaryOptions& options
    ) noexcept;

    // The source must already be normalized to UTF-8. Errors are returned as
    // ICU status values so this owner does not choose a backend error policy.
    CHARR_CXX_HELPER UErrorCode set_text(
        const StringView& source
    ) noexcept
    {
        if (iterator_ == nullptr || source.is_na() || source.len < 0 ||
                (source.ptr == nullptr && source.len != 0) ||
                (source.enc != StringEncoding::ascii &&
                    source.enc != StringEncoding::utf8 &&
                    source.enc != StringEncoding::ascii_or_utf8)) {
            return U_ILLEGAL_ARGUMENT_ERROR;
        }

        static const char empty = '\0';
        const char* data = source.ptr == nullptr ? &empty : source.ptr;
        UErrorCode status = U_ZERO_ERROR;
        UText* opened = utext_openUTF8(text_, data, source.len, &status);
        if (U_FAILURE(status) || opened == nullptr) {
            if (U_SUCCESS(status))
                status = U_MEMORY_ALLOCATION_ERROR;
            return status;
        }
        text_ = opened;

        status = U_ZERO_ERROR;
        ubrk_setUText(iterator_, text_, &status);
        position_ = U_FAILURE(status) ? UBRK_DONE : 0;
        return status;
    }
    CHARR_NEUTRAL_HELPER void first() noexcept
    {
        position_ = iterator_ == nullptr ? UBRK_DONE : ubrk_first(iterator_);
    }

    CHARR_NEUTRAL_HELPER void last() noexcept
    {
        if (iterator_ == nullptr) {
            position_ = UBRK_DONE;
            return;
        }
        ubrk_first(iterator_);
        position_ = ubrk_last(iterator_);
    }

    CHARR_NEUTRAL_HELPER bool next(BoundaryRange& range) noexcept
    {
        if (iterator_ == nullptr || position_ == UBRK_DONE)
            return false;

        int start = position_;
        for (;;) {
            const int end = ubrk_next(iterator_);
            if (end == UBRK_DONE) {
                position_ = UBRK_DONE;
                return false;
            }
            position_ = end;
            if (!skip()) {
                range.start = start;
                range.end = end;
                return true;
            }
            start = end;
        }
    }

    CHARR_NEUTRAL_HELPER bool previous(BoundaryRange& range) noexcept
    {
        if (iterator_ == nullptr || position_ == UBRK_DONE)
            return false;

        do {
            if (!skip()) {
                range.end = position_;
                position_ = ubrk_previous(iterator_);
                if (position_ == UBRK_DONE)
                    return false;
                range.start = position_;
                return true;
            }
            position_ = ubrk_previous(iterator_);
        }
        while (position_ != UBRK_DONE);
        return false;
    }

    CHARR_CXX_HELPER int count(
        const StringView& source, UErrorCode& status
    ) noexcept;

private:
    UBreakIterator* iterator_;
    UText* text_;
    const std::int32_t* skip_rules_;
    std::size_t skip_size_;
    int position_;

    CHARR_CXX_HELPER void close() noexcept;
    CHARR_NEUTRAL_HELPER bool skip() const noexcept
    {
        if (iterator_ == nullptr || skip_size_ == 0)
            return false;
        const int rule = ubrk_getRuleStatus(iterator_);
        for (std::size_t i = 0; i < skip_size_; i += 2) {
            if (rule >= skip_rules_[i] && rule < skip_rules_[i+1])
                return true;
        }
        return false;
    }
};


CHARR_NEUTRAL_HELPER bool boundary_ascii_word_first(
    const BoundaryOptions& options
) noexcept;


CHARR_NEUTRAL_HELPER bool boundary_ascii_initial_word(
    const char* value, int length, int& end
) noexcept;

} // namespace shared
} // namespace charr

#endif
