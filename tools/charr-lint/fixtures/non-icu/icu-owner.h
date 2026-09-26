#ifndef CHARR_LINT_NON_ICU_ICU_OWNER_H
#define CHARR_LINT_NON_ICU_ICU_OWNER_H

/* Declares the same owner outside every ICU header, so the ICU-only override
   must be rejected. */

namespace icu_fixture {

class IcuOwner {
public:
    IcuOwner();
    ~IcuOwner();

    static IcuOwner fromBytes(const char* bytes);
};

int plainValue(int value) noexcept;

} // namespace icu_fixture

#endif
