#include "utf8_list_input.h"

#include "../ci_stringi.h"

namespace charr {
namespace base_backend {
namespace io {

Utf8ListInput::Utf8ListInput(
    SEXP source, R_len_t recycle_size, bool shallow_recycle
) : shape_(), data_()
{
    (void)shallow_recycle;
#ifndef NDEBUG
    if (!Rf_isVectorList(source))
        throw StriException("UTF-8 list input requires an R list");
#endif
    const R_len_t size = LENGTH(source);
    shape_.reset(size, size);
    data_.reserve(static_cast<std::size_t>(size));

    for (R_len_t i = 0; i < size; ++i) {
        const R_len_t current_size = LENGTH(VECTOR_ELT(source, i));
        // Empty children recycle to an empty result. They cannot participate
        // in the divisibility check and must not reach the modulo operation.
        if (current_size > 0 && recycle_size % current_size != 0) {
            r_warning(MSG__WARN_RECYCLING_RULE);
            break;
        }
    }
    for (R_len_t i = 0; i < size; ++i) {
        data_.push_back(std::make_unique<Utf8Input>(
            VECTOR_ELT(source, i), recycle_size
        ));
    }
}

bool Utf8ListInput::isNA(R_len_t index) const
{
#ifndef NDEBUG
    if (index < 0 || index >= shape_.recycle_size())
        throw StriException("UTF-8 list index out of bounds");
#endif
    return data_[static_cast<std::size_t>(shape_.index(index))] == nullptr;
}

const Utf8Input& Utf8ListInput::get(R_len_t index) const
{
#ifndef NDEBUG
    if (index < 0 || index >= shape_.recycle_size())
        throw StriException("UTF-8 list index out of bounds");
#endif
    const std::unique_ptr<Utf8Input>& value = data_[
        static_cast<std::size_t>(shape_.index(index))
    ];
    if (!value)
        throw StriException("missing UTF-8 list element");
    return *value;
}

} // namespace io
} // namespace base_backend
} // namespace charr
