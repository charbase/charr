#ifndef CHARR_SHARED_DEFERRED_WARNINGS_H
#define CHARR_SHARED_DEFERRED_WARNINGS_H

#include "lint.h"

#include <R.h>

#include <cstddef>
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

    CHARR_NEUTRAL_HELPER std::size_t size() const noexcept
    {
        return messages_.size();
    }

    CHARR_R_HELPER void emit_prefix_r(std::size_t count) const noexcept
    {
        if (count > messages_.size())
            count = messages_.size();
        for (std::size_t i = 0; i < count; ++i)
            Rf_warning("%s", messages_[i].c_str());
    }

    CHARR_R_HELPER void emit_r() const noexcept
    {
        emit_prefix_r(messages_.size());
    }
};

} // namespace shared
} // namespace charr

#endif
