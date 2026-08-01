#include "resource-support.h"

void* unclassified_external_call() noexcept
{
    return raw_open();
}
