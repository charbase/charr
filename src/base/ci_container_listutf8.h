#ifndef CHARR_BASE_CI_CONTAINER_LISTUTF8_H
#define CHARR_BASE_CI_CONTAINER_LISTUTF8_H

#include "ci_container_base.h"
#include "utf8_input.h"

#include <memory>
#include <vector>

namespace charr {
namespace base {

class StriContainerListUTF8 : public StriContainerBase {
public:
    StriContainerListUTF8(
        SEXP source, R_len_t recycle_size, bool shallow_recycle = true
    );

    StriContainerListUTF8(const StriContainerListUTF8&) = delete;
    StriContainerListUTF8& operator=(const StriContainerListUTF8&) = delete;

    bool isNA(R_len_t index) const;
    const Utf8Input& get(R_len_t index) const;

private:
    std::vector<std::unique_ptr<Utf8Input>> data_;
};

} // namespace base
} // namespace charr

#endif
