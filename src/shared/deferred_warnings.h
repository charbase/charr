#ifndef CHARR_SHARED_DEFERRED_WARNINGS_H
#define CHARR_SHARED_DEFERRED_WARNINGS_H

#include "lint.h"

#include <R.h>

#include <string>
#include <vector>

namespace charr {
namespace shared {

class CHARR_OWNER_TYPE DeferredWarnings {
private:
    std::vector<std::string> messages_;

public:
    CHARR_CXX_HELPER DeferredWarnings() = default;
    CHARR_CXX_HELPER ~DeferredWarnings() noexcept = default;

    CHARR_CXX_HELPER void push(const char* message)
    {
        messages_.push_back(message);
    }

    CHARR_R_HELPER void emit_r() const noexcept
    {
        for (std::vector<std::string>::const_iterator it = messages_.begin();
                it != messages_.end(); ++it) {
            Rf_warning("%s", it->c_str());
        }
    }
};

} // namespace shared
} // namespace charr

#endif
