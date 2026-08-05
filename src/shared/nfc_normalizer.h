#ifndef CHARR_SHARED_NFC_NORMALIZER_H
#define CHARR_SHARED_NFC_NORMALIZER_H

#include "lint.h"
#include "native_to_utf8.h"
#include "string_view.h"

#include <unicode/normalizer2.h>
#include <unicode/unistr.h>

#include <vector>

namespace charr {
namespace shared {

// Reusable pure-C++ scratch for NFC normalization. The returned view remains
// valid until the next normalize() call.
class CHARR_OWNER_TYPE NfcNormalizer {
public:
    CHARR_CXX_HELPER NfcNormalizer();

    NfcNormalizer(const NfcNormalizer&) = delete;
    NfcNormalizer& operator=(const NfcNormalizer&) = delete;
    NfcNormalizer(NfcNormalizer&&) = delete;
    NfcNormalizer& operator=(NfcNormalizer&&) = delete;

    // ICU owns the returned singleton. reset() only records a borrowed pointer.
    CHARR_CXX_HELPER UErrorCode reset();

    CHARR_CXX_HELPER StringView normalize(
        const StringView& source, UErrorCode& status
    );

    CHARR_CXX_HELPER StringView normalize_utf8(
        const StringView& source, UErrorCode& status
    );

private:
    const icu::Normalizer2* normalizer_;
    NativeToUtf8 converter_;
    icu::UnicodeString input_;
    icu::UnicodeString output_;
    std::vector<char> utf8_;

    CHARR_CXX_HELPER StringView normalize_bytes(
        const char* data, int length, UErrorCode& status
    );
    CHARR_CXX_HELPER StringView normalize_impl(
        const StringView& source, UErrorCode& status,
        NativeToUtf8* converter
    );
};

} // namespace shared
} // namespace charr

#endif
