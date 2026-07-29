#ifndef CHARR_BASE_UTF8_LIST_INPUT_H
#define CHARR_BASE_UTF8_LIST_INPUT_H

#include "utf8_input.h"
#include "vectorized_size.h"

#include <memory>
#include <vector>

namespace charr {
namespace base_backend {
namespace io {

class Utf8ListInput {
public:
    Utf8ListInput(
        SEXP source, R_len_t recycle_size, bool shallow_recycle = true
    );

    Utf8ListInput(const Utf8ListInput&) = delete;
    Utf8ListInput& operator=(const Utf8ListInput&) = delete;

    bool isNA(R_len_t index) const;
    const Utf8Input& get(R_len_t index) const;
    R_len_t get_n() const noexcept { return shape_.data_size(); }
    R_len_t get_nrecycle() const noexcept {
        return shape_.recycle_size();
    }

private:
    VectorizedSize shape_;
    std::vector<std::unique_ptr<Utf8Input>> data_;
};

} // namespace io
} // namespace base_backend
} // namespace charr

#endif
