#include "collator.h"

#include <cstring>

namespace charr {
namespace shared {

Collator::Collator() noexcept : value_(nullptr)
{
}


Collator::~Collator() noexcept
{
    close();
}


void Collator::close() noexcept
{
    if (value_ != nullptr) {
        ucol_close(value_);
        value_ = nullptr;
    }
}


CollatorOpenResult Collator::reset(
    const CollatorOptions& options
) noexcept {
    close();

    UErrorCode status = U_ZERO_ERROR;
    value_ = ucol_open(options.locale, &status);
    if (U_FAILURE(status) || value_ == nullptr) {
        close();
        if (U_SUCCESS(status))
            status = U_MEMORY_ALLOCATION_ERROR;
        return CollatorOpenResult{status, false};
    }

    bool root_fallback = false;
    if (options.custom && status == U_USING_DEFAULT_WARNING &&
            options.locale != nullptr) {
        UErrorCode locale_status = U_ZERO_ERROR;
        const char* valid_locale = ucol_getLocaleByType(
            value_, ULOC_VALID_LOCALE, &locale_status
        );
        root_fallback = valid_locale != nullptr &&
            std::strcmp(valid_locale, "root") == 0;
    }

    if (!options.custom)
        return CollatorOpenResult{status, root_fallback};

    if (options.strength != UCOL_DEFAULT_STRENGTH) {
        status = U_ZERO_ERROR;
        ucol_setAttribute(
            value_, UCOL_STRENGTH, options.strength, &status
        );
        if (U_FAILURE(status)) {
            close();
            return CollatorOpenResult{status, root_fallback};
        }
    }

    const UColAttribute attributes[] = {
        UCOL_FRENCH_COLLATION,
        UCOL_ALTERNATE_HANDLING,
        UCOL_CASE_FIRST,
        UCOL_CASE_LEVEL,
        UCOL_NORMALIZATION_MODE,
        UCOL_NUMERIC_COLLATION
    };
    const UColAttributeValue values[] = {
        options.french_collation,
        options.alternate_handling,
        options.case_first,
        options.case_level,
        options.normalization_mode,
        options.numeric_collation
    };
    for (unsigned int i = 0; i < sizeof(attributes)/sizeof(attributes[0]); ++i) {
        if (values[i] == UCOL_DEFAULT)
            continue;
        status = U_ZERO_ERROR;
        ucol_setAttribute(value_, attributes[i], values[i], &status);
        if (U_FAILURE(status)) {
            close();
            return CollatorOpenResult{status, root_fallback};
        }
    }

    return CollatorOpenResult{U_ZERO_ERROR, root_fallback};
}


UCollator* Collator::get() const noexcept
{
    return value_;
}

} // namespace shared
} // namespace charr
