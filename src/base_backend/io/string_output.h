#ifndef CHARR_BASE_STRING_OUTPUT_H
#define CHARR_BASE_STRING_OUTPUT_H

#include "../ci_exception.h"

#include <Rinternals.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace charr {
namespace base_backend {
namespace io {

enum class OutputEncoding : unsigned char {
    native,
    utf8,
    latin1,
    bytes,
    ascii,
    missing
};

namespace output_detail {

struct Record {
    std::size_t offset;
    int length;
    OutputEncoding encoding;
};

inline Record missing_record() noexcept
{
    return Record{0, 0, OutputEncoding::missing};
}

inline std::size_t checked_record_count(R_xlen_t size)
{
    if (size < 0)
        throw std::length_error("negative string output size");

    const auto unsigned_size = static_cast<unsigned long long>(size);
    if (unsigned_size >
        static_cast<unsigned long long>(
            std::numeric_limits<std::size_t>::max()
        )) {
        throw std::length_error("string output size exceeds C++ limits");
    }

    return static_cast<std::size_t>(size);
}

inline int checked_string_length(std::size_t size)
{
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::length_error("string exceeds R's maximum element size");
    return static_cast<int>(size);
}

inline cetype_t to_cetype(OutputEncoding encoding)
{
    switch (encoding) {
    case OutputEncoding::native:
    case OutputEncoding::ascii:
        return CE_NATIVE;
    case OutputEncoding::utf8:
        return CE_UTF8;
    case OutputEncoding::latin1:
        return CE_LATIN1;
    case OutputEncoding::bytes:
        return CE_BYTES;
    case OutputEncoding::missing:
        break;
    }

    throw std::logic_error("missing strings do not have an R encoding");
}

inline std::size_t append_uninitialized(
    std::vector<char>& payload,
    std::size_t size
)
{
    if (size > payload.max_size() - payload.size())
        throw std::length_error("string output payload exceeds C++ limits");

    const std::size_t offset = payload.size();
    payload.resize(offset + size);
    return offset;
}

inline std::size_t append_copy(
    std::vector<char>& payload,
    const char* data,
    std::size_t size
)
{
    if (size > 0 && data == nullptr)
        throw std::invalid_argument("non-empty string output has null data");

    const std::size_t offset = append_uninitialized(payload, size);
    if (size > 0)
        std::memcpy(payload.data() + offset, data, size);
    return offset;
}

inline const char* record_data(
    const std::vector<char>& payload,
    const Record& record
) noexcept
{
    static const char empty = 0;
    return record.length == 0 ? &empty : payload.data() + record.offset;
}

inline SEXP make_charsxp(
    const std::vector<char>& payload,
    const Record& record
)
{
    if (record.encoding == OutputEncoding::missing)
        return NA_STRING;

    return Rf_mkCharLenCE(
        record_data(payload, record),
        record.length,
        to_cetype(record.encoding)
    );
}

inline SEXP finalize(
    const std::vector<char>& payload,
    const Record* records,
    R_xlen_t size
)
{
    return unwind_protect([&]() -> SEXP {
        SEXP output = PROTECT(Rf_allocVector(STRSXP, size));
        try {
            for (R_xlen_t i = 0; i < size; ++i) {
                SET_STRING_ELT(
                    output,
                    i,
                    make_charsxp(
                        payload,
                        records[static_cast<std::size_t>(i)]
                    )
                );
            }
        }
        catch (...) {
            UNPROTECT(1);
            throw;
        }
        UNPROTECT(1);
        return output;
    });
}

} // namespace output_detail

class FixedStringBuilder {
public:
    explicit FixedStringBuilder(R_xlen_t size = 0)
        : records_(
              output_detail::checked_record_count(size),
              output_detail::missing_record()
          ),
          payload_(),
          size_(size)
    {
    }

    FixedStringBuilder(const FixedStringBuilder&) = delete;
    FixedStringBuilder& operator=(
        const FixedStringBuilder&
    ) = delete;
    FixedStringBuilder(FixedStringBuilder&&) noexcept =
        default;
    FixedStringBuilder& operator=(
        FixedStringBuilder&&
    ) noexcept = default;

    R_xlen_t size() const noexcept
    {
        return size_;
    }

    void reset(R_xlen_t size)
    {
        const std::size_t count =
            output_detail::checked_record_count(size);
        if (count > records_.size())
            records_.resize(count);
        std::fill_n(
            records_.begin(),
            count,
            output_detail::missing_record()
        );
        payload_.clear();
        size_ = size;
    }

    void reserve_bytes(std::size_t size)
    {
        payload_.reserve(size);
    }

    void set(
        R_xlen_t index,
        const char* data,
        std::size_t size,
        OutputEncoding encoding
    )
    {
        validate_encoding(encoding);
        const std::size_t position = checked_index(index);
        const int length = output_detail::checked_string_length(size);
        const std::size_t offset =
            output_detail::append_copy(payload_, data, size);
        records_[position] =
            output_detail::Record{offset, length, encoding};
    }

    void set(
        R_xlen_t index,
        std::string_view value,
        OutputEncoding encoding
    )
    {
        set(index, value.data(), value.size(), encoding);
    }

    char* set_uninitialized(
        R_xlen_t index,
        std::size_t size,
        OutputEncoding encoding
    )
    {
        validate_encoding(encoding);
        const std::size_t position = checked_index(index);
        const int length = output_detail::checked_string_length(size);
        const std::size_t offset =
            output_detail::append_uninitialized(payload_, size);
        records_[position] =
            output_detail::Record{offset, length, encoding};
        return size == 0 ? nullptr : payload_.data() + offset;
    }

    void set_na(R_xlen_t index)
    {
        records_[checked_index(index)] =
            output_detail::missing_record();
    }

    SEXP to_sexp() const
    {
        return output_detail::finalize(
            payload_, records_.data(), size_
        );
    }

private:
    std::vector<output_detail::Record> records_;
    std::vector<char> payload_;
    R_xlen_t size_;

    std::size_t checked_index(R_xlen_t index) const
    {
        if (index < 0 || index >= size_)
            throw std::out_of_range("string output index is out of range");
        return static_cast<std::size_t>(index);
    }

    static void validate_encoding(OutputEncoding encoding)
    {
        if (encoding == OutputEncoding::missing)
            throw std::invalid_argument("use set_na() for a missing string");
    }
};

class ScalarStringBuilder {
public:
    ScalarStringBuilder() noexcept
        : payload_(), encoding_(OutputEncoding::missing)
    {
    }

    ScalarStringBuilder(const ScalarStringBuilder&) = delete;
    ScalarStringBuilder& operator=(
        const ScalarStringBuilder&
    ) = delete;
    ScalarStringBuilder(ScalarStringBuilder&&) noexcept =
        default;
    ScalarStringBuilder& operator=(
        ScalarStringBuilder&&
    ) noexcept = default;

    void reset() noexcept
    {
        payload_.clear();
        encoding_ = OutputEncoding::missing;
    }

    void reserve_bytes(std::size_t size)
    {
        payload_.reserve(size);
    }

    void set(
        const char* data,
        std::size_t size,
        OutputEncoding encoding
    )
    {
        validate_encoding(encoding);
        output_detail::checked_string_length(size);
        if (size > 0 && data == nullptr) {
            throw std::invalid_argument(
                "non-empty string output has null data"
            );
        }
        payload_.resize(size);
        if (size > 0)
            std::memcpy(payload_.data(), data, size);
        encoding_ = encoding;
    }

    void set(
        std::string_view value,
        OutputEncoding encoding
    )
    {
        set(value.data(), value.size(), encoding);
    }

    char* set_uninitialized(
        std::size_t size,
        OutputEncoding encoding
    )
    {
        validate_encoding(encoding);
        output_detail::checked_string_length(size);
        payload_.resize(size);
        encoding_ = encoding;
        return size == 0 ? nullptr : payload_.data();
    }

    void set_na() noexcept
    {
        payload_.clear();
        encoding_ = OutputEncoding::missing;
    }

    SEXP to_sexp() const
    {
        const output_detail::Record record{
            0,
            output_detail::checked_string_length(payload_.size()),
            encoding_
        };
        return output_detail::finalize(payload_, &record, 1);
    }

private:
    std::vector<char> payload_;
    OutputEncoding encoding_;

    static void validate_encoding(OutputEncoding encoding)
    {
        if (encoding == OutputEncoding::missing)
            throw std::invalid_argument("use set_na() for a missing string");
    }
};

} // namespace io
} // namespace base_backend
} // namespace charr

#endif
