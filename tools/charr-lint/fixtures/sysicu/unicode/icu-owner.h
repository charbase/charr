#ifndef CHARR_LINT_SYSICU_UNICODE_ICU_OWNER_H
#define CHARR_LINT_SYSICU_UNICODE_ICU_OWNER_H

/* Stands in for a system ICU header found through a unicode/ system include directory. */

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
