#ifndef CHARR_CI_UTF16_CURSOR_H
#define CHARR_CI_UTF16_CURSOR_H

#include "ci_reader.h"
#include "ci_stringi.h"
#include "altrep_backend/io/utf8_input.h"

#include <unicode/ustring.h>

namespace charr { namespace altrep_backend {


namespace ci {


// Read-only UTF-16 cursor for operations that consume each subject once.
// The UTF-8 records and Reader lease remain stable; only the ICU work buffer
// is reused as vectorization advances.
class Utf16Cursor {
private:
    charr::altrep_backend::io::Utf8Input input_;
    const charr::altrep_backend::io::Utf8Record* records_;
    R_len_t source_size_;
    bool aligned_;
    UnicodeString current_;
    R_len_t current_index_;
    bool current_utf8_valid_;

    R_len_t raw_index(R_len_t index) const noexcept
    {
        if (aligned_)
            return index;
        if (source_size_ == 1)
            return 0;
        return index % source_size_;
    }

    void load(R_len_t index)
    {
        const R_len_t raw = raw_index(index);
        if (current_index_ == raw)
            return;

        const charr::altrep_backend::io::Utf8Record& value = records_[raw];
        if (value.isNA()) {
            current_.setToBogus();
            current_index_ = raw;
            current_utf8_valid_ = false;
            return;
        }

        const int32_t source_length = value.length();
        if (current_.isBogus())
            current_ = UnicodeString();
        if (source_length == 0) {
            current_.remove();
            current_index_ = raw;
            current_utf8_valid_ = true;
            return;
        }

        UChar* destination = current_.getBuffer(source_length);
        if (!destination)
            throw StriException(MSG__MEM_ALLOC_ERROR);

        int32_t result_length = 0;
        int32_t substitutions = 0;
        UErrorCode status = U_ZERO_ERROR;
        u_strFromUTF8WithSub(
            destination, current_.getCapacity(), &result_length,
            value.data(), source_length, 0xfffd, &substitutions, &status
        );
        current_.releaseBuffer(U_FAILURE(status) ? 0 : result_length);
        if (U_FAILURE(status))
            throw StriException(status);
        current_index_ = raw;
        current_utf8_valid_ = substitutions == 0;
    }

public:
    Utf16Cursor(
        ReaderContext& context, SEXP source, R_len_t recycle_size
    ) : input_(
            context, source, recycle_size, true,
            charr::altrep_backend::io::Utf8BomPolicy::preserve
        ), records_(input_.source_data()), source_size_(input_.get_n()),
        aligned_(source_size_ == recycle_size), current_(),
        current_index_(-1), current_utf8_valid_(false)
    {
    }

    bool isNA(R_len_t index) const
    {
        return records_[raw_index(index)].isNA();
    }

    const UnicodeString& get(R_len_t index)
    {
        load(index);
        return current_;
    }

    const charr::altrep_backend::io::Utf8Record* utf8_if_valid(R_len_t index)
    {
        load(index);
        return current_utf8_valid_ ? &records_[raw_index(index)] : nullptr;
    }

    // The returned substring remains valid until the next get(). Invalidating
    // the cache here preserves recycling if this source record is used again.
    const UnicodeString& substring(
        R_len_t index, int32_t start, int32_t length
    )
    {
        load(index);
        current_.setTo(current_, start, length);
        current_index_ = -1;
        return current_;
    }

    void UChar16_to_UChar32_index(
        R_len_t index, int* i1, int* i2, int ni, int adj1, int adj2
    )
    {
        const UnicodeString& value = get(index);
        const UChar* data = value.getBuffer();
        const int32_t length = value.length();
        int j1 = 0;
        int j2 = 0;
        int i16 = 0;
        int i32 = 0;

        while (i16 < length && (j1 < ni || j2 < ni)) {
            while (j1 < ni && i1[j1] <= i16) {
                if (i1[j1] == NA_INTEGER || i1[j1] < 0) {
                    ++j1;
                    continue;
                }
                i1[j1++] = i32 + adj1;
            }
            while (j2 < ni && i2[j2] <= i16) {
                if (i2[j2] == NA_INTEGER || i2[j2] < 0) {
                    ++j2;
                    continue;
                }
                i2[j2++] = i32 + adj2;
            }
            U16_FWD_1(data, i16, length);
            ++i32;
        }

        while (j1 < ni && i1[j1] <= length) {
            if (i1[j1] == NA_INTEGER || i1[j1] < 0) {
                ++j1;
                continue;
            }
            i1[j1++] = i32 + adj1;
        }
        while (j2 < ni && i2[j2] <= length) {
            if (i2[j2] == NA_INTEGER || i2[j2] < 0) {
                ++j2;
                continue;
            }
            i2[j2++] = i32 + adj2;
        }

#ifndef NDEBUG
        if (i16 >= length && (j1 < ni || j2 < ni))
            throw StriException("DEBUG: Utf16Cursor index conversion");
#endif
    }
};


} // namespace ci


} } // namespace charr::altrep_backend

#endif
