#ifndef CHARR_ALTREP_VECTORIZED_SIZE_H
#define CHARR_ALTREP_VECTORIZED_SIZE_H

#include "../ci_external.h"

#include <stdexcept>

namespace charr {
namespace altrep_backend {
namespace io {

class VectorizedSize {
public:
    VectorizedSize() noexcept : data_size_(0), recycle_size_(0) {}

    VectorizedSize(
        R_len_t source_size, R_len_t recycle_size,
        bool materialize_recycle = false
    ) : data_size_(0), recycle_size_(0)
    {
        reset(source_size, recycle_size, materialize_recycle);
    }

    void reset(
        R_len_t source_size, R_len_t recycle_size,
        bool materialize_recycle = false
    )
    {
        if (source_size < 0 || recycle_size < 0)
            throw std::invalid_argument("negative vectorized size");

        if (source_size == 0 || recycle_size == 0) {
            data_size_ = 0;
            recycle_size_ = 0;
            return;
        }

        data_size_ = materialize_recycle ? recycle_size : source_size;
        recycle_size_ = recycle_size;
        if (data_size_ < source_size || data_size_ > recycle_size_)
            throw std::invalid_argument("incompatible vectorized sizes");
    }

    R_len_t data_size() const noexcept { return data_size_; }
    R_len_t recycle_size() const noexcept { return recycle_size_; }

    void set_recycle_size(R_len_t value)
    {
        if (value < 0 || (data_size_ == 0 && value != 0))
            throw std::invalid_argument("incompatible recycle size");
        recycle_size_ = value;
    }

    R_len_t index(R_len_t value) const
    {
        if (value < 0 || value >= recycle_size_ || data_size_ == 0)
            throw std::out_of_range("vectorized index out of bounds");
        return value % data_size_;
    }

    R_len_t vectorize_init() const noexcept
    {
        return data_size_ <= 0 ? recycle_size_ : 0;
    }

    R_len_t vectorize_end() const noexcept { return recycle_size_; }

    R_len_t vectorize_next(R_len_t value) const noexcept
    {
        if (data_size_ <= 0)
            return recycle_size_;
        if (value == recycle_size_ - 1 - (recycle_size_ % data_size_))
            return recycle_size_;
        value += data_size_;
        return value >= recycle_size_ ? (value % data_size_) + 1 : value;
    }

private:
    R_len_t data_size_;
    R_len_t recycle_size_;
};

} // namespace io
} // namespace altrep_backend
} // namespace charr

#endif
