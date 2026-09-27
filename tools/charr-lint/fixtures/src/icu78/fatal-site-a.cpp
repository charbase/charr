// Stands in for an ICU source file. Sites are counted after preprocessing:
// the commented and disabled expansions below are not sites.
#include "fatal-site.h"

namespace icu_78 {

int loop_exit(int value)
{
    for (;;) {
        if (value > 0)
            return value;
        ++value;
    }
    UPRV_UNREACHABLE_EXIT;
}

// UPRV_UNREACHABLE_EXIT;

#if 0
void disabled()
{
    UPRV_UNREACHABLE_EXIT;
}
#endif

int two_sites(int kind)
{
    switch (kind) {
    case 0:
        return 0;
    case 1:
        UPRV_UNREACHABLE_EXIT;
    default:
        UPRV_UNREACHABLE_EXIT;
    }
}

// One invocation of a macro that expands the fatal macro twice is two sites.
#define FIXTURE_TWO_EXITS(kind) \
    do { \
        if ((kind) == 1) \
            UPRV_UNREACHABLE_EXIT; \
        if ((kind) == 2) \
            UPRV_UNREACHABLE_EXIT; \
    } while (0)

int nested_sites(int kind)
{
    FIXTURE_TWO_EXITS(kind);
    return kind;
}

// A row names a const method and a variadic function as declared.
class ConstSite {
public:
    int check(int kind) const;
};

int ConstSite::check(int kind) const
{
    if (kind != 0)
        UPRV_UNREACHABLE_EXIT;
    return kind;
}

int variadic_site(int kind, ...)
{
    if (kind != 0)
        UPRV_UNREACHABLE_EXIT;
    return kind;
}

// A site in a lambda that may throw belongs to the enclosing function.
int lambda_site(int kind)
{
    auto check = [kind]() {
        if (kind != 0)
            UPRV_UNREACHABLE_EXIT;
    };
    check();
    return kind;
}

} // namespace icu_78

// Only a whole versioned "icu_<digits>" namespace is written "icu".
namespace xicu_78 {
int boundary_site(int kind)
{
    if (kind != 0)
        UPRV_UNREACHABLE_EXIT;
    return kind;
}
} // namespace xicu_78

namespace icu_impl {
int unversioned_site(int kind)
{
    if (kind != 0)
        UPRV_UNREACHABLE_EXIT;
    return kind;
}
} // namespace icu_impl

int icu_4byte_site(int kind)
{
    if (kind != 0)
        UPRV_UNREACHABLE_EXIT;
    return kind;
}

// The renaming suffix is "_<digits>_charr" after a name: these keep theirs.
namespace fixture_names {
int _7_charr(int kind)
{
    if (kind != 0)
        UPRV_UNREACHABLE_EXIT;
    return kind;
}

int nodigits__charr(int kind)
{
    if (kind != 0)
        UPRV_UNREACHABLE_EXIT;
    return kind;
}

int version7_charr(int kind)
{
    if (kind != 0)
        UPRV_UNREACHABLE_EXIT;
    return kind;
}
} // namespace fixture_names

// A bundled C entry point keeps its name without the renaming suffix.
extern "C" void c_entry_78_charr(int kind)
{
    if (kind != 0)
        UPRV_UNREACHABLE_EXIT;
}

#if defined(BAD_DIRECT_CALL)
void direct_call()
{
    charr::shared::icu_invariant_failure("direct");
}
#endif

#if defined(BAD_NOEXCEPT_SITE)
void noexcept_site() noexcept
{
    UPRV_UNREACHABLE_EXIT;
}
#endif

// A destructor is noexcept unless declared otherwise, even while its
// exception specification is still unevaluated.
#if defined(BAD_DESTRUCTOR_SITE)
struct DestructorSite {
    int kind;
    ~DestructorSite()
    {
        if (kind != 0)
            UPRV_UNREACHABLE_EXIT;
    }
};
#endif

// In a class template the destructor's exception specification stays
// unevaluated, so only the destructor rule sees that it cannot throw.
#if defined(BAD_TEMPLATE_DESTRUCTOR_SITE)
template <typename T>
struct TemplateDestructorSite {
    T kind;
    ~TemplateDestructorSite()
    {
        if (kind != 0)
            UPRV_UNREACHABLE_EXIT;
    }
};
#endif

#if defined(BAD_NOEXCEPT_LAMBDA_SITE)
void noexcept_lambda_site(int kind)
{
    auto check = [kind]() noexcept {
        if (kind != 0)
            UPRV_UNREACHABLE_EXIT;
    };
    check();
}
#endif
