#include "ci_stringi.h"
#include "ci_container_listutf8.h"

namespace charr {
namespace base {

StriContainerListUTF8::StriContainerListUTF8(
    SEXP source, R_len_t recycle_size, bool shallow_recycle
) : StriContainerBase(), data_()
{
    (void)shallow_recycle;
#ifndef NDEBUG
    if (!Rf_isVectorList(source))
        throw StriException("UTF-8 list input requires an R list");
#endif
    const R_len_t size = LENGTH(source);
    init_Base(size, size, true);
    data_.reserve(static_cast<std::size_t>(size));

    for (R_len_t i = 0; i < size; ++i) {
        const R_len_t current_size = LENGTH(VECTOR_ELT(source, i));
        // Empty children recycle to an empty result. They cannot participate
        // in the divisibility check and must not reach the modulo operation.
        if (current_size > 0 && recycle_size % current_size != 0) {
            Rf_warning(MSG__WARN_RECYCLING_RULE);
            break;
        }
    }
    for (R_len_t i = 0; i < size; ++i) {
        data_.push_back(std::make_unique<Utf8Input>(
            VECTOR_ELT(source, i), recycle_size
        ));
    }
}

bool StriContainerListUTF8::isNA(R_len_t index) const
{
#ifndef NDEBUG
    if (index < 0 || index >= nrecycle)
        throw StriException("UTF-8 list index out of bounds");
#endif
    return data_[static_cast<std::size_t>(index % n)] == nullptr;
}

const Utf8Input& StriContainerListUTF8::get(R_len_t index) const
{
#ifndef NDEBUG
    if (index < 0 || index >= nrecycle)
        throw StriException("UTF-8 list index out of bounds");
#endif
    const std::unique_ptr<Utf8Input>& value =
        data_[static_cast<std::size_t>(index % n)];
    if (!value)
        throw StriException("missing UTF-8 list element");
    return *value;
}

} // namespace base
} // namespace charr
