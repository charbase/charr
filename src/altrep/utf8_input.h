#ifndef CHARR_ALTREP_UTF8_INPUT_H
#define CHARR_ALTREP_UTF8_INPUT_H

#include "native_to_utf8.h"
#include "stable_slice_arena.h"

#include <charport.h>

#include <deque>
#include <memory>
#include <vector>

namespace ci {
class ReaderBorrow;
class ReaderContext;
}

namespace charr {
namespace altrep {

enum class Utf8RecordState : unsigned char {
    missing,
    ascii,
    utf8,
    ascii_or_utf8,
    bytes
};

enum class Utf8BomPolicy : unsigned char {
    strip,
    preserve
};

class Utf8Record {
public:
    Utf8Record() noexcept;
    Utf8Record(const char* data, R_len_t length, Utf8RecordState state) noexcept;

    bool isNA() const noexcept
    {
        return state_ == Utf8RecordState::missing;
    }
    bool isASCII() const noexcept;
    bool isUTF8() const noexcept;
    bool isBytes() const noexcept;
    Utf8RecordState state() const noexcept { return state_; }
    const char* data() const;
    R_len_t length() const;
    R_len_t countCodePoints() const;
    charport::StrView view() const noexcept
    {
        if (state_ == Utf8RecordState::missing) {
            return charport::StrView{
                nullptr, NA_INTEGER, cetype_ext_t::CE_NA
            };
        }
        const cetype_ext_t encoding = state_ == Utf8RecordState::ascii
            ? cetype_ext_t::CE_ASCII
            : (state_ == Utf8RecordState::bytes
                ? cetype_ext_t::CE_BYTES
                : (state_ == Utf8RecordState::ascii_or_utf8
                    ? cetype_ext_t::CE_ASCII_OR_UTF8
                    : cetype_ext_t::CE_UTF8));
        return charport::StrView{data_, length_, encoding};
    }

    bool endsWith(
        R_len_t byteindex, const char* pattern, R_len_t pattern_length,
        bool case_insensitive
    ) const;
    bool startsWith(
        R_len_t byteindex, const char* pattern, R_len_t pattern_length,
        bool case_insensitive
    ) const;

private:
    const char* data_;
    R_len_t length_;
    mutable Utf8RecordState state_;
};

class ByteView {
public:
    ByteView() noexcept : data_(nullptr), length_(0), missing_(true) {}
    ByteView(const char* data, R_len_t length) noexcept
        : data_(data), length_(length), missing_(false) {}

    bool isNA() const noexcept { return missing_; }
    const char* data() const;
    R_len_t length() const;

private:
    const char* data_;
    R_len_t length_;
    bool missing_;
};

class Utf8Input {
public:
    Utf8Input() noexcept;
    Utf8Input(
        ci::ReaderContext& context, SEXP source, R_xlen_t recycle_size,
        bool shallow_recycle = true,
        Utf8BomPolicy bom_policy = Utf8BomPolicy::strip
    );
    Utf8Input(
        const std::shared_ptr<ci::ReaderBorrow>& borrow,
        R_xlen_t recycle_size,
        Utf8BomPolicy bom_policy = Utf8BomPolicy::strip
    );
    Utf8Input(
        const std::shared_ptr<ci::ReaderBorrow>& borrow,
        const charport::StrView& value, R_xlen_t recycle_size,
        bool shallow_recycle = true,
        Utf8BomPolicy bom_policy = Utf8BomPolicy::strip
    );

    Utf8Input(const Utf8Input&) noexcept = default;
    Utf8Input& operator=(const Utf8Input&) noexcept = default;

    R_xlen_t source_size() const noexcept;
    R_xlen_t size() const noexcept;
    // The source-order table is not recycled. Its address remains stable for
    // this immutable input's lifetime, including the underlying Reader lease.
    const Utf8Record* source_data() const noexcept;
    R_len_t get_n() const noexcept;
    R_len_t get_nrecycle() const noexcept;
    void set_nrecycle(R_len_t value);

    R_len_t vectorize_init() const noexcept;
    R_len_t vectorize_end() const noexcept;
    R_len_t vectorize_next(R_len_t index) const noexcept;

    const Utf8Record& record(R_xlen_t index) const;
    charport::StrView text(R_xlen_t index) const;
    bool is_na(R_xlen_t index) const;
    bool is_bytes(R_xlen_t index) const;

    bool isNA(R_len_t index) const { return is_na(index); }
    const Utf8Record& get(R_len_t index) const;
    const Utf8Record& getNAble(R_len_t index) const;
    R_len_t getMaxNumBytes() const;
    R_len_t getMaxLength() const;

private:
    struct Storage;

    R_xlen_t source_size_;
    R_xlen_t recycle_size_;
    std::shared_ptr<Storage> storage_;

    R_xlen_t source_index(R_xlen_t index) const;
};

class Utf8Workspace {
public:
    Utf8Workspace(
        ci::ReaderContext& context, SEXP source, R_xlen_t recycle_size
    );

    R_len_t get_n() const noexcept;
    R_len_t get_nrecycle() const noexcept;
    bool isNA(R_len_t index) const;
    const Utf8Record& get(R_len_t index) const;
    const Utf8Record& getNAble(R_len_t index) const;
    void setNA(R_len_t index);
    void set(R_len_t index, const Utf8Record& value);
    void replaceAllAtPos(
        R_len_t index, R_len_t output_size,
        const char* replacement, R_len_t replacement_length,
        std::deque<std::pair<R_len_t, R_len_t> >& occurrences
    );

private:
    Utf8Input input_;
    std::vector<Utf8Record> records_;
    StableSliceArena replacements_;

    R_len_t checked_index(R_len_t index) const;
    Utf8Record copy_record(const Utf8Record& value);
};

class IndexedUtf8Input : public Utf8Input {
public:
    IndexedUtf8Input() noexcept;
    IndexedUtf8Input(
        ci::ReaderContext& context, SEXP source, R_xlen_t recycle_size,
        bool shallow_recycle = true
    );
    IndexedUtf8Input(
        const std::shared_ptr<ci::ReaderBorrow>& borrow,
        const charport::StrView& value, R_xlen_t recycle_size,
        bool shallow_recycle = true
    );

    IndexedUtf8Input(const IndexedUtf8Input& other) noexcept;
    IndexedUtf8Input& operator=(const IndexedUtf8Input& other) noexcept;

    void UTF8_to_UChar32_index(
        R_len_t i, int* i1, int* i2, int ni, int adj1, int adj2
    );
    R_len_t UChar32_to_UTF8_index_back(R_len_t i, R_len_t wh);
    R_len_t UChar32_to_UTF8_index_fwd(R_len_t i, R_len_t wh);

private:
    R_len_t last_fwd_codepoint_;
    R_len_t last_fwd_utf8_;
    const char* last_fwd_data_;
    R_len_t last_back_codepoint_;
    R_len_t last_back_utf8_;
    const char* last_back_data_;

    void reset_index_cache() noexcept;
};

} // namespace altrep
} // namespace charr

#endif
