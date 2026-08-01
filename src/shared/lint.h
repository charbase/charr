#ifndef CHARR_SHARED_LINT_H
#define CHARR_SHARED_LINT_H

#if defined(CHARR_LINT) && defined(__clang__)
#define CHARR_LINT_ANNOTATE(value) [[clang::annotate(value)]]
#else
#define CHARR_LINT_ANNOTATE(value)
#endif

#define CHARR_CXX_HELPER \
    CHARR_LINT_ANNOTATE("charr.cxx_helper")
#define CHARR_R_HELPER \
    CHARR_LINT_ANNOTATE("charr.r_helper")
#define CHARR_NEUTRAL_HELPER \
    CHARR_LINT_ANNOTATE("charr.neutral_helper")
#define CHARR_ENTRYPOINT \
    CHARR_LINT_ANNOTATE("charr.entrypoint")
#define CHARR_ABI_SHIM \
    CHARR_LINT_ANNOTATE("charr.abi_shim")
#define CHARR_OWNER_TYPE \
    CHARR_LINT_ANNOTATE("charr.owner_type")
#define CHARR_TRUSTED_UNWIND \
    CHARR_LINT_ANNOTATE("charr.trusted_unwind")

#if defined(_MSC_VER)
#define CHARR_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define CHARR_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define CHARR_ALWAYS_INLINE inline
#endif

#endif
