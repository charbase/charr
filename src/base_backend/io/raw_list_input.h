#ifndef CHARR_BASE_RAW_LIST_INPUT_H
#define CHARR_BASE_RAW_LIST_INPUT_H

#include "vectorized_size.h"
#include "utf8_views.h"

#include <vector>

namespace charr {
namespace base_backend {
namespace io {

class RawListInput {
public:
    explicit RawListInput(SEXP source);

    RawListInput(const RawListInput&) = delete;
    RawListInput& operator=(const RawListInput&) = delete;

    bool isNA(R_len_t index) const;
    ByteView get(R_len_t index) const;
    R_len_t get_n() const noexcept { return shape_.data_size(); }
    R_len_t get_nrecycle() const noexcept {
        return shape_.recycle_size();
    }

private:
    VectorizedSize shape_;
    std::vector<ByteView> data_;
    std::vector<unsigned char> missing_;

    void append(const char* data, R_len_t length);
    void append_missing();
};

} // namespace io
} // namespace base_backend
} // namespace charr

#endif
