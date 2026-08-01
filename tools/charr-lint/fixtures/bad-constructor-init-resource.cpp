#include "../../../src/shared/lint.h"
#include "resource-support.h"

class BadOwner {
private:
    void* handle_;

public:
    CHARR_R_HELPER BadOwner() noexcept
        : handle_(raw_open())
    {
    }
};
