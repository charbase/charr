#ifndef CHARR_LINT_SRC_ICU78_UNICODE_ICU_OWNER_H
#define CHARR_LINT_SRC_ICU78_UNICODE_ICU_OWNER_H

/* Stands in for a vendored ICU public header under src/icu78/unicode/. */

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
