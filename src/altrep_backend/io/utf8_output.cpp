#include "utf8_output.h"

#include <stdexcept>
#include <utility>

namespace charr {
namespace altrep_backend {
namespace io {

namespace utf8_output {

const char* empty_payload() noexcept
{
    static const char value = '\0';
    return &value;
}

int checked_length(std::size_t length)
{
    if (length > static_cast<std::size_t>(R_LEN_T_MAX))
        throw std::length_error("character output exceeds R's string length limit");
    return static_cast<int>(length);
}

bool is_ascii(const char* data, std::size_t length) noexcept
{
    for (std::size_t i = 0; i < length; ++i) {
        if (static_cast<unsigned char>(data[i]) > 0x7fU)
            return false;
    }
    return true;
}

cetype_ext_t resolved_encoding(
    const char* data, std::size_t length, cetype_ext_t encoding
)
{
    switch (encoding) {
    case cetype_ext_t::CE_ASCII:
    case cetype_ext_t::CE_UTF8:
    case cetype_ext_t::CE_BYTES:
    case cetype_ext_t::CE_LATIN1:
    case cetype_ext_t::CE_NATIVE:
        return encoding;
    case cetype_ext_t::CE_ASCII_OR_UTF8:
        return is_ascii(data, length)
            ? cetype_ext_t::CE_ASCII
            : cetype_ext_t::CE_UTF8;
    case cetype_ext_t::CE_NA:
        return cetype_ext_t::CE_NA;
    default:
        throw std::invalid_argument("unknown character output encoding");
    }
}

cetype_ext_t reserve_encoding(cetype_ext_t encoding)
{
    switch (encoding) {
    case cetype_ext_t::CE_ASCII:
    case cetype_ext_t::CE_UTF8:
    case cetype_ext_t::CE_BYTES:
    case cetype_ext_t::CE_LATIN1:
    case cetype_ext_t::CE_NATIVE:
    case cetype_ext_t::CE_NA:
        return encoding;
    case cetype_ext_t::CE_ASCII_OR_UTF8:
        throw std::invalid_argument(
            "cannot reserve output with an unresolved ASCII-or-UTF-8 mark"
        );
    default:
        throw std::invalid_argument("unknown character output encoding");
    }
}

} // namespace utf8_output

using namespace utf8_output;

OutputRecord missing_output_record() noexcept
{
    return make_strview(nullptr, NA_INTEGER, cetype_ext_t::CE_NA);
}

OutputRecord output_record(
    const char* data, std::size_t length, cetype_ext_t encoding
)
{
    if (encoding == cetype_ext_t::CE_NA)
        return missing_output_record();
    if (data == nullptr) {
        if (length != 0)
            throw std::invalid_argument("null character output has nonzero length");
        data = empty_payload();
    }

    const int stored_length = checked_length(length);
    return make_strview(
        data, stored_length, resolved_encoding(data, length, encoding)
    );
}

OutputRecord output_record(
    std::string_view value, cetype_ext_t encoding
)
{
    return output_record(value.data(), value.size(), encoding);
}

OutputRecord output_record(const charport::StrView& value)
{
    if (value.is_na())
        return missing_output_record();
    if (value.len < 0)
        throw std::invalid_argument("character output has a negative length");
    return output_record(
        value.ptr, static_cast<std::size_t>(value.len), value.enc
    );
}

OutputStore scalar_store(const OutputRecord& value)
{
    const OutputRecord normalized = output_record(value);
    if (normalized.is_na()) {
        return OutputStore::scalar(
            nullptr, 0, cetype_ext_t::CE_NA
        );
    }
    return OutputStore::scalar(
        normalized.ptr, static_cast<std::size_t>(normalized.len),
        normalized.enc
    );
}

OutputStore scalar_store(
    const char* data, std::size_t length, cetype_ext_t encoding
)
{
    return scalar_store(output_record(data, length, encoding));
}

OutputStore scalar_store(
    std::string_view value, cetype_ext_t encoding
)
{
    return scalar_store(output_record(value, encoding));
}

SEXP finalize(OutputStore&& store)
{
    return charport::charvec::wrap(std::move(store));
}

OutputBuilder::OutputBuilder(R_xlen_t size)
    : size_(size), builder_(size)
{
}

void OutputBuilder::reset(R_xlen_t size)
{
    builder_.reset(size);
    size_ = size;
}

R_xlen_t OutputBuilder::size() const noexcept
{
    return size_;
}

void OutputBuilder::set(R_xlen_t index, const OutputRecord& value)
{
    const OutputRecord normalized = output_record(value);
    if (normalized.is_na()) {
        builder_.set_na(index);
        return;
    }
    builder_.set(index, normalized);
}

void OutputBuilder::set(
    R_xlen_t index, const char* data, std::size_t length,
    cetype_ext_t encoding
)
{
    set(index, output_record(data, length, encoding));
}

void OutputBuilder::set(
    R_xlen_t index, std::string_view value, cetype_ext_t encoding
)
{
    set(index, output_record(value, encoding));
}

void OutputBuilder::set_na(R_xlen_t index)
{
    builder_.set_na(index);
}

char* OutputBuilder::reserve(
    R_xlen_t index, std::size_t length, cetype_ext_t encoding
)
{
    const cetype_ext_t checked_encoding = reserve_encoding(encoding);
    if (checked_encoding == cetype_ext_t::CE_NA)
        return builder_.reserve(index, 0, checked_encoding);
    (void)checked_length(length);
    return builder_.reserve(index, length, checked_encoding);
}

OutputStore OutputBuilder::release_store()
{
    OutputStore store = builder_.release_store();
    size_ = 0;
    return store;
}

SEXP OutputBuilder::to_sexp()
{
    size_ = 0;
    return builder_.to_sexp();
}

GrowableOutputBuilder::GrowableOutputBuilder()
    : builder_()
{
}

void GrowableOutputBuilder::reset() noexcept
{
    builder_ = charport::charvec::GrowableBuilder();
}

std::size_t GrowableOutputBuilder::size() const noexcept
{
    return builder_.size();
}

void GrowableOutputBuilder::append(const OutputRecord& value)
{
    const OutputRecord normalized = output_record(value);
    if (normalized.is_na()) {
        builder_.append(nullptr, 0, cetype_ext_t::CE_NA);
        return;
    }
    builder_.append(normalized);
}

void GrowableOutputBuilder::append(
    const char* data, std::size_t length, cetype_ext_t encoding
)
{
    append(output_record(data, length, encoding));
}

void GrowableOutputBuilder::append(
    std::string_view value, cetype_ext_t encoding
)
{
    append(output_record(value, encoding));
}

void GrowableOutputBuilder::append_na()
{
    builder_.append(nullptr, 0, cetype_ext_t::CE_NA);
}

char* GrowableOutputBuilder::append_reserve(
    std::size_t length, cetype_ext_t encoding
)
{
    const cetype_ext_t checked_encoding = reserve_encoding(encoding);
    if (checked_encoding == cetype_ext_t::CE_NA)
        return builder_.append_reserve(0, checked_encoding);
    (void)checked_length(length);
    return builder_.append_reserve(length, checked_encoding);
}

OutputStore GrowableOutputBuilder::release_store()
{
    OutputStore store = builder_.release_store();
    reset();
    return store;
}

SEXP GrowableOutputBuilder::to_sexp()
{
    SEXP result = builder_.to_sexp();
    reset();
    return result;
}

} // namespace io
} // namespace altrep_backend
} // namespace charr
