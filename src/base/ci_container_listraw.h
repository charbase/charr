#ifndef CHARR_BASE_CI_CONTAINER_LISTRAW_H
#define CHARR_BASE_CI_CONTAINER_LISTRAW_H

#include "ci_container_base.h"
#include "utf8_views.h"

#include <vector>

namespace charr {
namespace base {

class StriContainerListRaw : public StriContainerBase {
public:
    explicit StriContainerListRaw(SEXP source);

    StriContainerListRaw(const StriContainerListRaw&) = delete;
    StriContainerListRaw& operator=(const StriContainerListRaw&) = delete;

    bool isNA(R_len_t index) const;
    ByteView get(R_len_t index) const;

private:
    std::vector<ByteView> data_;
    std::vector<unsigned char> missing_;

    void append(const char* data, R_len_t length);
    void append_missing();
};

} // namespace base
} // namespace charr

#endif
