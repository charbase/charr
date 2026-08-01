#ifndef CHARR_SHARED_PROTECT_H
#define CHARR_SHARED_PROTECT_H

#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif
#include <Rinternals.h>

#include "lint.h"

#include <type_traits>

namespace charr {
namespace shared {

// Counts one manually managed R protection domain. Destruction does nothing.
class ProtHelper {
private:
    int count_ = 0;

public:
    CHARR_NEUTRAL_HELPER ProtHelper() noexcept = default;
    ProtHelper(const ProtHelper&) = delete;
    ProtHelper& operator=(const ProtHelper&) = delete;

    CHARR_R_HELPER SEXP protect_one(SEXP value) noexcept
    {
        ::Rf_protect(value);
        ++count_;
        return value;
    }

    CHARR_R_HELPER void protect_with_index(
        SEXP value, PROTECT_INDEX* index
    ) noexcept {
        ::R_ProtectWithIndex(value, index);
        ++count_;
    }

    CHARR_R_HELPER SEXP reprotect_one(
        SEXP value, PROTECT_INDEX index
    ) noexcept {
        ::R_Reprotect(value, index);
        return value;
    }

    CHARR_R_HELPER SEXP reprotect_slot(
        SEXP value, PROTECT_INDEX index
    ) noexcept {
        ::R_Reprotect(value, index);
        return value;
    }

    CHARR_NEUTRAL_HELPER int count() const noexcept
    {
        return count_;
    }

    CHARR_NEUTRAL_HELPER void clear() noexcept
    {
        count_ = 0;
    }

    CHARR_NEUTRAL_HELPER void adopt(int count) noexcept
    {
        count_ += count;
    }

    CHARR_NEUTRAL_HELPER void release(int count) noexcept
    {
        UNPROTECT(count);
        count_ -= count;
    }

    CHARR_NEUTRAL_HELPER void release_all() noexcept
    {
        UNPROTECT(count_);
        count_ = 0;
    }
};

static_assert(
    std::is_trivially_destructible<ProtHelper>::value,
    "ProtHelper must remain trivially destructible"
);

} // namespace shared
} // namespace charr

#endif
