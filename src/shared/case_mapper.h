#ifndef CHARR_SHARED_CASE_MAPPER_H
#define CHARR_SHARED_CASE_MAPPER_H

#include "lint.h"
#include "native_to_utf8.h"
#include "string_view.h"

#include <unicode/ucasemap.h>

#include <cstdint>
#include <vector>

namespace charr {
namespace shared {

enum class CaseMapMode : unsigned char {
    lower,
    upper
};


struct CaseMapInput {
    const char* data;
    std::int32_t length;
    bool ascii;
};


// Reusable pure-C++ state for lower- and uppercase transformations. Prepared
// input remains valid until the next prepare() call. ICU output remains valid
// until the next map_icu() call.
class CHARR_OWNER_TYPE CaseMapper {
public:
    CHARR_CXX_HELPER CaseMapper();
    CHARR_CXX_HELPER ~CaseMapper() noexcept;

    CaseMapper(const CaseMapper&) = delete;
    CaseMapper& operator=(const CaseMapper&) = delete;
    CaseMapper(CaseMapper&&) = delete;
    CaseMapper& operator=(CaseMapper&&) = delete;

    CHARR_CXX_HELPER void reset(
        const char* locale, CaseMapMode mode
    ) noexcept;

    CHARR_CXX_HELPER CaseMapInput prepare(
        const StringView& source
    );

    CHARR_NEUTRAL_HELPER bool has_ascii_fast_path(
        const CaseMapInput& input
    ) const noexcept;

    CHARR_NEUTRAL_HELPER bool map_ascii(
        const CaseMapInput& input, char* output
    ) const noexcept;

    CHARR_CXX_HELPER StringView map_icu(
        const CaseMapInput& input, UErrorCode& status
    );

private:
    UCaseMap* casemap_;
    const char* locale_;
    CaseMapMode mode_;
    bool simple_ascii_;
    NativeToUtf8 converter_;
    std::vector<char> output_;

    CHARR_CXX_HELPER void close() noexcept;
    CHARR_CXX_HELPER bool open(UErrorCode& status) noexcept;
};

} // namespace shared
} // namespace charr

#endif
