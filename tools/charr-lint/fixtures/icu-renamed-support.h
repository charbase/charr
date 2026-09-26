#ifndef CHARR_LINT_ICU_RENAMED_SUPPORT_H
#define CHARR_LINT_ICU_RENAMED_SUPPORT_H

/* A library outside ICU that names ICU's namespace in its signatures and
   template arguments. */

namespace icu_renamed_support {

int take(const icu_renamed::Thing& thing) noexcept;

template <typename T>
class Holder {
public:
    int size() const noexcept;
};

} // namespace icu_renamed_support

#endif
