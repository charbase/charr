#ifndef CHARR_LINT_NON_ICU_ICU_RENAMED_H
#define CHARR_LINT_NON_ICU_ICU_RENAMED_H

/* Declares the same names outside every ICU header, so a name ending in
   _78_charr keeps its suffix. */

/* Mirrors ICU's renaming: urename.h pastes a suffix onto every C entry point
   and U_ICU_NAMESPACE pastes the same suffix onto "icu". A system ICU uses
   the version alone; a bundled ICU built with U_LIB_SUFFIX_C_NAME=_charr
   appends "_charr" to it. */
#define ICU_RENAMED_PASTE2(name, suffix) name##suffix
#define ICU_RENAMED_PASTE(name, suffix) ICU_RENAMED_PASTE2(name, suffix)
#ifdef ICU_RENAMED_BUNDLED
#define ICU_RENAMED_SUFFIX _78_charr
#else
#define ICU_RENAMED_SUFFIX _78
#endif
#define ICU_RENAMED(name) ICU_RENAMED_PASTE(name, ICU_RENAMED_SUFFIX)

#define icu_renamed_open ICU_RENAMED(icu_renamed_open)
#define ICU_RENAMED_NAMESPACE ICU_RENAMED(icu)

extern "C" int icu_renamed_open(int value);

namespace ICU_RENAMED_NAMESPACE {

class Thing {
public:
    int value() const noexcept;
};

} // namespace ICU_RENAMED_NAMESPACE

namespace icu_renamed = ICU_RENAMED_NAMESPACE;

#endif
