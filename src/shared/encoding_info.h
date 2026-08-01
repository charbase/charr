#ifndef CHARR_SHARED_ENCODING_INFO_H
#define CHARR_SHARED_ENCODING_INFO_H

#include "lint.h"

#include <unicode/ucnv.h>

#include <string>
#include <vector>

namespace charr {
namespace shared {

enum class EncodingInfoValueKind : unsigned char {
    unset,
    character,
    logical,
    integer
};


struct EncodingInfoValue {
    EncodingInfoValueKind kind;
    bool missing;
    const char* data;
    int length;
    int scalar;
};


enum class EncodingInfoDiagnosticKind : unsigned char {
    standard_name,
    ascii_conversion,
    conversion,
    non_single_code_point,
    round_trip
};


struct EncodingInfoDiagnostic {
    EncodingInfoDiagnosticKind kind;
    int input_byte;
    UChar32 code_point;
    int output_byte;
    const char* converter_name;
};


// Owns the ICU converter and all native staging needed to describe one
// encoding. The staged values are consumed by backend-specific R assembly.
class CHARR_OWNER_TYPE EncodingInfo {
public:
    CHARR_CXX_HELPER EncodingInfo() noexcept;
    CHARR_CXX_HELPER ~EncodingInfo() noexcept;

    EncodingInfo(const EncodingInfo&) = delete;
    EncodingInfo& operator=(const EncodingInfo&) = delete;
    EncodingInfo(EncodingInfo&&) = delete;
    EncodingInfo& operator=(EncodingInfo&&) = delete;

    CHARR_CXX_HELPER UErrorCode reset(
        const char* encoding
    ) noexcept;

    CHARR_CXX_HELPER void inspect();

    CHARR_NEUTRAL_HELPER int size() const noexcept;
    CHARR_NEUTRAL_HELPER bool has_name(int index) const noexcept;
    CHARR_NEUTRAL_HELPER const char* name_data(
        int index
    ) const noexcept;
    CHARR_NEUTRAL_HELPER int name_length(int index) const noexcept;
    CHARR_NEUTRAL_HELPER EncodingInfoValue value(
        int index
    ) const noexcept;
    CHARR_NEUTRAL_HELPER bool warn_get_name() const noexcept;
    CHARR_NEUTRAL_HELPER int diagnostic_count() const noexcept;
    CHARR_NEUTRAL_HELPER EncodingInfoDiagnostic diagnostic(
        int index
    ) const noexcept;

private:
    UConverter* converter_;
    const char* requested_name_;
    std::vector<const char*> standards_;
    std::vector<std::string> names_;
    std::vector<unsigned char> has_names_;
    std::vector<EncodingInfoValue> values_;
    std::vector<EncodingInfoDiagnostic> diagnostics_;
    bool warn_get_name_;

    CHARR_CXX_HELPER void close() noexcept;
    CHARR_CXX_HELPER bool has_ascii_subset();
    CHARR_CXX_HELPER bool is_one_to_one();
    CHARR_NEUTRAL_HELPER const char* converter_name() const noexcept;
};

} // namespace shared
} // namespace charr

#endif
