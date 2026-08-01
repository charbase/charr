#ifndef CHARR_LINT_FIXTURE_READER_SUPPORT_H
#define CHARR_LINT_FIXTURE_READER_SUPPORT_H

#include "../../../src/shared/lint.h"

namespace charport {

class CHARR_OWNER_TYPE Reader {
public:
    CHARR_NEUTRAL_HELPER Reader() noexcept = default;

    CHARR_R_HELPER explicit Reader(int value) noexcept
        : value_(value) {}

    CHARR_R_HELPER void reset(int value) noexcept
    {
        value_ = value;
    }

    CHARR_NEUTRAL_HELPER int size() const noexcept
    {
        return value_;
    }

private:
    int value_ = 0;
};

} // namespace charport

#endif
