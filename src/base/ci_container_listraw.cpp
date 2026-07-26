#include "ci_stringi.h"
#include "ci_container_listraw.h"

namespace charr {
namespace base {

StriContainerListRaw::StriContainerListRaw(SEXP source)
    : StriContainerBase(), data_(), missing_()
{
    if (Rf_isNull(source)) {
        init_Base(1, 1, true);
        append_missing();
        return;
    }

    if (isRaw(source)) {
        init_Base(1, 1, true);
        append(
            reinterpret_cast<const char*>(RAW(source)), LENGTH(source)
        );
        return;
    }

    if (Rf_isVectorList(source)) {
        const R_len_t size = LENGTH(source);
        init_Base(size, size, true);
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
    init_Base(size, size, true);
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

void StriContainerListRaw::append(
    const char* data, R_len_t length
)
{
    // RAW() materializes ALTREP input if needed; the public argument or its
    // parent list remains rooted throughout this call, so the pointer is a
    // stable borrow. CHARSXP payloads have the same lifetime guarantee.
    data_.push_back(ByteView{data, length});
    missing_.push_back(0);
}

void StriContainerListRaw::append_missing()
{
    data_.push_back(ByteView{nullptr, 0});
    missing_.push_back(1);
}

bool StriContainerListRaw::isNA(R_len_t index) const
{
#ifndef NDEBUG
    if (index < 0 || index >= nrecycle)
        throw StriException("raw list index out of bounds");
#endif
    return missing_[static_cast<std::size_t>(index % n)] != 0;
}

ByteView StriContainerListRaw::get(R_len_t index) const
{
#ifndef NDEBUG
    if (index < 0 || index >= nrecycle)
        throw StriException("raw list index out of bounds");
    if (isNA(index))
        throw StriException("cannot get a missing raw record");
#endif
    return data_[static_cast<std::size_t>(index % n)];
}

} // namespace base
} // namespace charr
