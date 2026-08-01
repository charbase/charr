#ifndef CHARR_SHARED_COLLATOR_H
#define CHARR_SHARED_COLLATOR_H

#include "lint.h"

#include <unicode/ucol.h>

namespace charr {
namespace shared {

// Parsed entirely in an R-only phase. Locale strings point into R_alloc
// storage and therefore remain valid until the enclosing .Call returns.
struct CollatorOptions {
    bool custom;
    const char* locale;
    UColAttributeValue french_collation;
    UColAttributeValue alternate_handling;
    UColAttributeValue case_first;
    UColAttributeValue case_level;
    UColAttributeValue normalization_mode;
    UColAttributeValue strength;
    UColAttributeValue numeric_collation;
};


struct CollatorOpenResult {
    UErrorCode status;
    bool root_fallback;
};


// Owns the ICU handle. Construction is deliberately empty so an instance can
// live in an entry point's Frame region and acquire its handle only inside the
// unwind-protected operation.
class CHARR_OWNER_TYPE Collator {
public:
    CHARR_CXX_HELPER Collator() noexcept;
    CHARR_CXX_HELPER ~Collator() noexcept;

    Collator(const Collator&) = delete;
    Collator& operator=(const Collator&) = delete;
    Collator(Collator&&) = delete;
    Collator& operator=(Collator&&) = delete;

    CHARR_CXX_HELPER CollatorOpenResult reset(
        const CollatorOptions& options
    ) noexcept;

    CHARR_NEUTRAL_HELPER UCollator* get() const noexcept;

private:
    UCollator* value_;

    CHARR_CXX_HELPER void close() noexcept;
};

} // namespace shared
} // namespace charr

#endif
