#ifndef CHARR_SHARED_TITLE_CASE_H
#define CHARR_SHARED_TITLE_CASE_H

#include "lint.h"
#include "native_to_utf8.h"
#include "string_view.h"

#include <unicode/ubrk.h>
#include <unicode/ucasemap.h>

#include <cstdint>
#include <vector>

namespace charr {
namespace shared {

// An R-only parser supplies this specification. The locale and rule bytes
// point into R_alloc storage and remain valid until the enclosing .Call ends.
struct TitleCaseOptions {
    const char* locale;
    const char* rules;
    std::int32_t rules_length;
    UBreakIteratorType type;
    bool custom_rules;
    bool has_skip_rules;
};


struct TitleCaseOpenResult {
    UErrorCode status;
    bool root_fallback;
};


struct TitleCaseInput {
    const char* data;
    std::int32_t length;
    bool ascii;
};


// Owns the ICU case map and its adopted break iterator. Construction leaves
// both handles empty so the object can live in an entry point's Frame region.
class CHARR_OWNER_TYPE TitleCaseMapper {
public:
    CHARR_CXX_HELPER TitleCaseMapper();
    CHARR_CXX_HELPER ~TitleCaseMapper() noexcept;

    TitleCaseMapper(const TitleCaseMapper&) = delete;
    TitleCaseMapper& operator=(const TitleCaseMapper&) = delete;
    TitleCaseMapper(TitleCaseMapper&&) = delete;
    TitleCaseMapper& operator=(TitleCaseMapper&&) = delete;

    CHARR_CXX_HELPER TitleCaseOpenResult reset(
        const TitleCaseOptions& options
    ) noexcept;

    CHARR_CXX_HELPER TitleCaseInput prepare(
        const StringView& source
    );

    CHARR_CXX_HELPER TitleCaseInput prepare_utf8(
        const StringView& source
    );

    CHARR_NEUTRAL_HELPER bool has_ascii_fast_path(
        const TitleCaseInput& input
    ) const noexcept;

    CHARR_NEUTRAL_HELPER void map_ascii(
        const TitleCaseInput& input, char* output
    ) const noexcept;

    CHARR_CXX_HELPER StringView map_icu(
        const TitleCaseInput& input, UErrorCode& status
    );

private:
    UCaseMap* casemap_;
    UBreakIterator* iterator_;
    bool ascii_fast_path_;
    bool turkic_;
    NativeToUtf8 converter_;
    std::vector<char> output_;

    CHARR_CXX_HELPER void close() noexcept;
    CHARR_CXX_HELPER TitleCaseInput prepare_impl(
        const StringView& source, NativeToUtf8* converter
    );
};

} // namespace shared
} // namespace charr

#endif
