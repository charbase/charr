// Derived from stringi.
// Copyright (c) 2013-2025, Marek Gagolewski. See inst/COPYRIGHTS.

#include "encoding_info.h"

#include <unicode/utf8.h>
#include <unicode/utf16.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace charr {
namespace shared {

namespace encoding_info {

constexpr UChar32 replacement_character = 0xfffd;

CHARR_CXX_HELPER int c_string_length(const char* value)
{
    const std::size_t length = std::strlen(value);
    if (length > static_cast<std::size_t>(
            std::numeric_limits<int>::max())) {
        throw std::length_error(
            "ICU encoding name exceeds R's string limit"
        );
    }
    return static_cast<int>(length);
}


CHARR_NEUTRAL_HELPER EncodingInfoValue unset_value() noexcept
{
    return EncodingInfoValue{
        EncodingInfoValueKind::unset, false, nullptr, 0, 0
    };
}


CHARR_NEUTRAL_HELPER EncodingInfoValue character_value(
    const char* value
) noexcept {
    return EncodingInfoValue{
        EncodingInfoValueKind::character,
        value == nullptr,
        value,
        0,
        0
    };
}


CHARR_NEUTRAL_HELPER EncodingInfoValue scalar_value(
    EncodingInfoValueKind kind, int value, bool missing = false
) noexcept {
    return EncodingInfoValue{kind, missing, nullptr, 0, value};
}

} // namespace encoding_info

using namespace encoding_info;

EncodingInfo::EncodingInfo() noexcept
    : converter_(nullptr), requested_name_(nullptr), standards_(), names_(),
      has_names_(), values_(), diagnostics_(), warn_get_name_(false)
{
}


EncodingInfo::~EncodingInfo() noexcept
{
    close();
}


void EncodingInfo::close() noexcept
{
    if (converter_ != nullptr) {
        ucnv_close(converter_);
        converter_ = nullptr;
    }
}


UErrorCode EncodingInfo::reset(const char* encoding) noexcept
{
    close();
    requested_name_ = encoding;
    standards_.clear();
    names_.clear();
    has_names_.clear();
    values_.clear();
    diagnostics_.clear();
    warn_get_name_ = false;

    UErrorCode status = U_ZERO_ERROR;
    converter_ = ucnv_open(encoding, &status);
    if (U_FAILURE(status) || converter_ == nullptr) {
        close();
        if (U_SUCCESS(status))
            status = U_MEMORY_ALLOCATION_ERROR;
    }
    return status;
}


const char* EncodingInfo::converter_name() const noexcept
{
    if (converter_ != nullptr) {
        UErrorCode status = U_ZERO_ERROR;
        const char* value = ucnv_getName(converter_, &status);
        if (U_SUCCESS(status) && value != nullptr)
            return value;
    }
    return requested_name_ != nullptr ? requested_name_ : "<unknown>";
}


bool EncodingInfo::has_ascii_subset()
{
    if (ucnv_getMinCharSize(converter_) != 1)
        return false;

    const int ascii_from = 1;
    const int ascii_to = 127;
    unsigned char ascii[ascii_to-ascii_from+1];
    for (int value = ascii_from; value <= ascii_to; ++value)
        ascii[value-ascii_from] = static_cast<unsigned char>(value);

    const char* previous = reinterpret_cast<const char*>(ascii);
    const char* current = previous;
    const char* end = reinterpret_cast<const char*>(
        ascii + sizeof(ascii)
    );
    ucnv_reset(converter_);

    while (current < end) {
        UErrorCode status = U_ZERO_ERROR;
        const UChar32 code_point = ucnv_getNextUChar(
            converter_, &current, end, &status
        );
        if (U_FAILURE(status)) {
#ifndef NDEBUG
            diagnostics_.push_back(EncodingInfoDiagnostic{
                EncodingInfoDiagnosticKind::ascii_conversion,
                static_cast<unsigned char>(previous[0]),
                0,
                0,
                converter_name()
            });
#endif
            return false;
        }

        if (previous != current-1 || U8_LENGTH(code_point) != 1 ||
                code_point != static_cast<unsigned char>(previous[0])) {
            return false;
        }
        previous = current;
    }
    return true;
}


bool EncodingInfo::is_one_to_one()
{
    if (ucnv_getMinCharSize(converter_) != 1)
        return false;

    const int byte_from = 32;
    const int byte_to = 255;
    unsigned char bytes[byte_to-byte_from+1];
    for (int value = byte_from; value <= byte_to; ++value)
        bytes[value-byte_from] = static_cast<unsigned char>(value);

    const int buffer_size = UCNV_GET_MAX_BYTES_FOR_STRING(1, 1);
    char buffer[buffer_size];
    const char* previous = reinterpret_cast<const char*>(bytes);
    const char* current = previous;
    const char* end = reinterpret_cast<const char*>(
        bytes + sizeof(bytes)
    );
    ucnv_reset(converter_);

    while (current < end) {
        UErrorCode status = U_ZERO_ERROR;
        const UChar32 code_point = ucnv_getNextUChar(
            converter_, &current, end, &status
        );
        if (U_FAILURE(status)) {
#ifndef NDEBUG
            diagnostics_.push_back(EncodingInfoDiagnostic{
                EncodingInfoDiagnosticKind::conversion,
                static_cast<unsigned char>(previous[0]),
                0,
                0,
                converter_name()
            });
#endif
            return false;
        }

        if (previous != current-1)
            return false;

        const UChar lead = U16_LEAD(code_point);
        if (!U16_IS_SINGLE(lead)) {
#ifndef NDEBUG
            diagnostics_.push_back(EncodingInfoDiagnostic{
                EncodingInfoDiagnosticKind::non_single_code_point,
                static_cast<unsigned char>(previous[0]),
                code_point,
                0,
                converter_name()
            });
#endif
            return false;
        }

        if (code_point != replacement_character) {
            status = U_ZERO_ERROR;
            const UChar code_unit = static_cast<UChar>(code_point);
            const int converted = ucnv_fromUChars(
                converter_, buffer, buffer_size, &code_unit, 1, &status
            );
            if (U_FAILURE(status)) {
#ifndef NDEBUG
                diagnostics_.push_back(EncodingInfoDiagnostic{
                    EncodingInfoDiagnosticKind::conversion,
                    static_cast<unsigned char>(previous[0]),
                    0,
                    0,
                    converter_name()
                });
#endif
                return false;
            }

            if (converted != 1 || buffer[0] != previous[0]) {
#ifndef NDEBUG
                diagnostics_.push_back(EncodingInfoDiagnostic{
                    EncodingInfoDiagnosticKind::round_trip,
                    static_cast<unsigned char>(previous[0]),
                    code_point,
                    converted > 0
                        ? static_cast<unsigned char>(buffer[0])
                        : 0,
                    converter_name()
                });
#endif
                return false;
            }
        }
        previous = current;
    }
    return true;
}


void EncodingInfo::inspect()
{
    if (converter_ == nullptr)
        throw std::logic_error("encoding inspector is not initialized");

    const int standard_count = static_cast<int>(
        ucnv_countStandards()
    ) - 1;
    if (standard_count <= 0) {
        throw std::runtime_error(
            "character encoding could not be set, queried, or selected"
        );
    }
    if (standard_count > std::numeric_limits<int>::max()-7)
        throw std::length_error("too many ICU encoding standards");

    const int field_count = standard_count + 7;
    standards_.resize(static_cast<std::size_t>(standard_count));
    names_.resize(static_cast<std::size_t>(field_count));
    has_names_.assign(static_cast<std::size_t>(field_count), 0);
    values_.assign(
        static_cast<std::size_t>(field_count), unset_value()
    );

    names_[0] = "Name.friendly";
    names_[1] = "Name.ICU";
    has_names_[0] = 1;
    has_names_[1] = 1;

    for (int i = 0; i < standard_count; ++i) {
        UErrorCode status = U_ZERO_ERROR;
        const char* standard = ucnv_getStandard(
            static_cast<std::uint16_t>(i), &status
        );
        if (U_FAILURE(status)) {
#ifndef NDEBUG
            diagnostics_.push_back(EncodingInfoDiagnostic{
                EncodingInfoDiagnosticKind::standard_name,
                0,
                0,
                0,
                nullptr
            });
#endif
            standard = nullptr;
        }
        standards_[static_cast<std::size_t>(i)] = standard;
        if (standard == nullptr)
            continue;

        const int standard_length = c_string_length(standard);
        if (standard_length > std::numeric_limits<int>::max()-5) {
            throw std::length_error(
                "ICU encoding standard name exceeds R's string limit"
            );
        }
        std::string& name = names_[static_cast<std::size_t>(i+2)];
        name = "Name.";
        name.append(
            standard,
            static_cast<std::size_t>(standard_length)
        );
        has_names_[static_cast<std::size_t>(i+2)] = 1;
    }

    names_[static_cast<std::size_t>(field_count-5)] = "ASCII.subset";
    names_[static_cast<std::size_t>(field_count-4)] = "Unicode.1to1";
    names_[static_cast<std::size_t>(field_count-3)] = "CharSize.8bit";
    names_[static_cast<std::size_t>(field_count-2)] = "CharSize.min";
    names_[static_cast<std::size_t>(field_count-1)] = "CharSize.max";
    for (int i = field_count-5; i < field_count; ++i)
        has_names_[static_cast<std::size_t>(i)] = 1;

    UErrorCode status = U_ZERO_ERROR;
    const char* canonical = ucnv_getName(converter_, &status);
    if (U_FAILURE(status) || canonical == nullptr) {
        values_[1] = character_value(nullptr);
        warn_get_name_ = true;
        return;
    }

    values_[1] = character_value(canonical);
    values_[1].length = c_string_length(canonical);

    const char* friendly = nullptr;
    status = U_ZERO_ERROR;
    friendly = ucnv_getStandardName(canonical, "MIME", &status);
    if (U_FAILURE(status) || friendly == nullptr) {
        status = U_ZERO_ERROR;
        friendly = ucnv_getStandardName(canonical, "JAVA", &status);
        if (U_FAILURE(status) || friendly == nullptr)
            friendly = canonical;
    }
    values_[0] = character_value(friendly);
    values_[0].length = c_string_length(friendly);

    values_[static_cast<std::size_t>(field_count-5)] = scalar_value(
        EncodingInfoValueKind::logical,
        static_cast<int>(has_ascii_subset())
    );

    const int minimum = static_cast<int>(ucnv_getMinCharSize(converter_));
    const int maximum = static_cast<int>(ucnv_getMaxCharSize(converter_));
    const bool eight_bit = minimum == 1 && maximum == 1;
    values_[static_cast<std::size_t>(field_count-3)] = scalar_value(
        EncodingInfoValueKind::logical, static_cast<int>(eight_bit)
    );
    values_[static_cast<std::size_t>(field_count-2)] = scalar_value(
        EncodingInfoValueKind::integer, minimum
    );
    values_[static_cast<std::size_t>(field_count-1)] = scalar_value(
        EncodingInfoValueKind::integer, maximum
    );
    values_[static_cast<std::size_t>(field_count-4)] = eight_bit
        ? scalar_value(
            EncodingInfoValueKind::logical,
            static_cast<int>(is_one_to_one())
        )
        : scalar_value(EncodingInfoValueKind::logical, 0, true);

    for (int i = 0; i < standard_count; ++i) {
        const char* standard = standards_[static_cast<std::size_t>(i)];
        if (standard == nullptr)
            continue;

        status = U_ZERO_ERROR;
        const char* name = ucnv_getStandardName(
            canonical, standard, &status
        );
        if (U_FAILURE(status) || name == nullptr) {
            values_[static_cast<std::size_t>(i+2)] =
                character_value(nullptr);
        }
        else {
            values_[static_cast<std::size_t>(i+2)] =
                character_value(name);
            values_[static_cast<std::size_t>(i+2)].length =
                c_string_length(name);
        }
    }
}


int EncodingInfo::size() const noexcept
{
    return static_cast<int>(values_.size());
}


bool EncodingInfo::has_name(int index) const noexcept
{
    return has_names_[static_cast<std::size_t>(index)] != 0;
}


const char* EncodingInfo::name_data(int index) const noexcept
{
    return names_[static_cast<std::size_t>(index)].data();
}


int EncodingInfo::name_length(int index) const noexcept
{
    return static_cast<int>(
        names_[static_cast<std::size_t>(index)].size()
    );
}


EncodingInfoValue EncodingInfo::value(int index) const noexcept
{
    return values_[static_cast<std::size_t>(index)];
}


bool EncodingInfo::warn_get_name() const noexcept
{
    return warn_get_name_;
}


int EncodingInfo::diagnostic_count() const noexcept
{
    return static_cast<int>(diagnostics_.size());
}


EncodingInfoDiagnostic EncodingInfo::diagnostic(int index) const noexcept
{
    return diagnostics_[static_cast<std::size_t>(index)];
}

} // namespace shared
} // namespace charr
