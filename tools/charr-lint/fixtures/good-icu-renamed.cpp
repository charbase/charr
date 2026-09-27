#include "../../../src/shared/lint.h"

// External effect keys do not depend on the ICU mode. A bundled ICU appends
// "_charr" to its versioned C entry points and namespace; the key drops it,
// so a system and a bundled build share one reviewed manifest. Only names
// declared in ICU headers, and ICU's namespace, are rewritten: the same
// names declared elsewhere keep the suffix.
#if defined(ICU_RENAMED_SYSTEM_HEADER)
#include <unicode/icu-renamed.h>
#elif defined(ICU_RENAMED_NON_ICU_HEADER)
#include "non-icu/icu-renamed.h"
#else
#include "src/icu78/unicode/icu-renamed.h"
#endif
#include "icu-renamed-support.h"

CHARR_CXX_HELPER int icu_renamed_calls(const icu_renamed::Thing& thing)
{
    const icu_renamed_support::Holder<icu_renamed::Thing> holder;
    return icu_renamed_open(thing.value()) +
        icu_renamed_support::take(thing) + holder.size() +
        legacy_1_charr::legacy_value() +
        icu_renamed_support::icu_2_charr::nested_value();
}
