#include "raw_list_input.h"

#include "../ci_stringi.h"

namespace charr {
namespace base_backend {
namespace io {

RawListInput::RawListInput(SEXP source)
    : shape_(), data_(), missing_()
{
    if (Rf_isNull(source)) {
        shape_.reset(1, 1);
        append_missing();
        return;
    }

    if (isRaw(source)) {
        shape_.reset(1, 1);
        append(
            reinterpret_cast<const char*>(RAW(source)), LENGTH(source)
        );
        return;
    }

    if (Rf_isVectorList(source)) {
        const R_len_t size = LENGTH(source);
        shape_.reset(size, size);
        data_.reserve(static_cast<std::size_t>(size));
        missing_.reserve(static_cast<std::size_t>(size));
        for (R_len_t i = 0; i < size; ++i) {
            SEXP current = VECTOR_ELT(source, i);
            if (Rf_isNull(current)) {
                append_missing();
            }
            else {
                append(
                    reinterpret_cast<const char*>(RAW(current)),
                    LENGTH(current)
                );
            }
        }
        return;
    }

    const R_len_t size = LENGTH(source);
    shape_.reset(size, size);
    data_.reserve(static_cast<std::size_t>(size));
    missing_.reserve(static_cast<std::size_t>(size));
    for (R_len_t i = 0; i < size; ++i) {
        SEXP current = STRING_ELT(source, i);
        if (current == NA_STRING) {
            append_missing();
        }
        else {
            append(CHAR(current), LENGTH(current));
        }
    }
}

void RawListInput::append(
    const char* data, R_len_t length
)
{
    // RAW() materializes ALTREP input if needed; the public argument or its
    // parent list remains rooted throughout this call, so the pointer is a
    // stable borrow. CHARSXP payloads have the same lifetime guarantee.
    data_.push_back(ByteView{data, length});
    missing_.push_back(0);
}

void RawListInput::append_missing()
{
    data_.push_back(ByteView{nullptr, 0});
    missing_.push_back(1);
}

bool RawListInput::isNA(R_len_t index) const
{
#ifndef NDEBUG
    if (index < 0 || index >= shape_.recycle_size())
        throw StriException("raw list index out of bounds");
#endif
    return missing_[static_cast<std::size_t>(shape_.index(index))] != 0;
}

ByteView RawListInput::get(R_len_t index) const
{
#ifndef NDEBUG
    if (index < 0 || index >= shape_.recycle_size())
        throw StriException("raw list index out of bounds");
    if (isNA(index))
        throw StriException("cannot get a missing raw record");
#endif
    return data_[static_cast<std::size_t>(shape_.index(index))];
}

} // namespace io
} // namespace base_backend
} // namespace charr
