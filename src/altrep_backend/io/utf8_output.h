#ifndef CHARR_ALTREP_UTF8_OUTPUT_H
#define CHARR_ALTREP_UTF8_OUTPUT_H

#include "../../shared/lint.h"

#include <charport.h>

#include <cstddef>
#include <string_view>
#include <utility>

namespace charr {
namespace altrep_backend {
namespace io {

using OutputRecord = charport::StrView;
using OutputStore = charport::charvec::Store;

CHARR_NEUTRAL_HELPER [[nodiscard]] OutputRecord
missing_output_record() noexcept;
CHARR_CXX_HELPER [[nodiscard]] OutputRecord output_record(
    const char* data, std::size_t length, cetype_ext_t encoding
);
CHARR_CXX_HELPER [[nodiscard]] OutputRecord output_record(
    std::string_view value, cetype_ext_t encoding
);
CHARR_CXX_HELPER [[nodiscard]] OutputRecord output_record(
    const charport::StrView& value
);

CHARR_CXX_HELPER [[nodiscard]] OutputStore scalar_store(
    const OutputRecord& value
);
CHARR_CXX_HELPER [[nodiscard]] OutputStore scalar_store(
    const char* data, std::size_t length, cetype_ext_t encoding
);
CHARR_CXX_HELPER [[nodiscard]] OutputStore scalar_store(
    std::string_view value, cetype_ext_t encoding
);

CHARR_R_HELPER inline SEXP finalize(OutputStore&& store) noexcept
{
    return charport::charvec::wrap(std::move(store));
}

class CHARR_OWNER_TYPE OutputBuilder {
public:
    CHARR_CXX_HELPER explicit OutputBuilder(R_xlen_t size);

    OutputBuilder(const OutputBuilder&) = delete;
    OutputBuilder& operator=(const OutputBuilder&) = delete;
    OutputBuilder(OutputBuilder&&) = delete;
    OutputBuilder& operator=(OutputBuilder&&) = delete;

    CHARR_CXX_HELPER void reset(R_xlen_t size)
    {
        builder_.reset(size);
        size_ = size;
    }
    CHARR_NEUTRAL_HELPER R_xlen_t size() const noexcept;

    CHARR_CXX_HELPER void set(
        R_xlen_t index, const OutputRecord& value
    );
    CHARR_CXX_HELPER void set(
        R_xlen_t index, const char* data, std::size_t length,
        cetype_ext_t encoding
    );
    CHARR_CXX_HELPER void set(
        R_xlen_t index, std::string_view value, cetype_ext_t encoding
    );
    // Hot paths may use this after validating the pointer, length, and
    // encoding while constructing the record. CETYPE_EXT_ASCII_OR_UTF8 is
    // valid: charport resolves that mark when R materializes the CHARSXP.
    CHARR_CXX_HELPER void set_validated(
        R_xlen_t index, const OutputRecord& value
    )
    {
        if (value.is_na()) {
            builder_.set_na(index);
            return;
        }
        builder_.set(index, value);
    }
    CHARR_CXX_HELPER void set_na(R_xlen_t index)
    {
        builder_.set_na(index);
    }
    CHARR_CXX_HELPER [[nodiscard]] char* reserve(
        R_xlen_t index, std::size_t length, cetype_ext_t encoding
    );

    CHARR_CXX_HELPER [[nodiscard]] OutputStore release_store() noexcept
    {
        OutputStore store = builder_.release_store();
        size_ = 0;
        return store;
    }
    CHARR_R_HELPER SEXP to_sexp() noexcept;

private:
    R_xlen_t size_;
    charport::charvec::Builder builder_;
};

class CHARR_OWNER_TYPE GrowableOutputBuilder {
public:
    CHARR_CXX_HELPER GrowableOutputBuilder();

    GrowableOutputBuilder(const GrowableOutputBuilder&) = delete;
    GrowableOutputBuilder& operator=(const GrowableOutputBuilder&) = delete;
    GrowableOutputBuilder(GrowableOutputBuilder&&) = delete;
    GrowableOutputBuilder& operator=(GrowableOutputBuilder&&) = delete;

    CHARR_CXX_HELPER void reset() noexcept;
    CHARR_NEUTRAL_HELPER std::size_t size() const noexcept;

    CHARR_CXX_HELPER void append(const OutputRecord& value);
    CHARR_CXX_HELPER void append(
        const char* data, std::size_t length, cetype_ext_t encoding
    );
    CHARR_CXX_HELPER void append(
        std::string_view value, cetype_ext_t encoding
    );
    CHARR_CXX_HELPER void append_na();
    CHARR_CXX_HELPER [[nodiscard]] char* append_reserve(
        std::size_t length, cetype_ext_t encoding
    );

    CHARR_CXX_HELPER [[nodiscard]] OutputStore release_store() noexcept;
    CHARR_R_HELPER SEXP to_sexp() noexcept;

private:
    charport::charvec::GrowableBuilder builder_;
};

} // namespace io
} // namespace altrep_backend
} // namespace charr

#endif
