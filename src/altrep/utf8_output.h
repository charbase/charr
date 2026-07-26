#ifndef CHARR_ALTREP_UTF8_OUTPUT_H
#define CHARR_ALTREP_UTF8_OUTPUT_H

#include <charport.h>

#include <cstddef>
#include <string_view>

namespace charr {
namespace altrep {

using OutputRecord = charport::StrView;
using OutputStore = charport::charvec::Store;

[[nodiscard]] OutputRecord missing_output_record() noexcept;
[[nodiscard]] OutputRecord output_record(
    const char* data, std::size_t length, cetype_ext_t encoding
);
[[nodiscard]] OutputRecord output_record(
    std::string_view value, cetype_ext_t encoding
);
[[nodiscard]] OutputRecord output_record(const charport::StrView& value);

[[nodiscard]] OutputStore scalar_store(const OutputRecord& value);
[[nodiscard]] OutputStore scalar_store(
    const char* data, std::size_t length, cetype_ext_t encoding
);
[[nodiscard]] OutputStore scalar_store(
    std::string_view value, cetype_ext_t encoding
);

SEXP finalize(OutputStore&& store);
SEXP scalar_sexp(const OutputRecord& value);
SEXP scalar_sexp(
    const char* data, std::size_t length, cetype_ext_t encoding
);
SEXP scalar_sexp(std::string_view value, cetype_ext_t encoding);

class OutputBuilder {
public:
    explicit OutputBuilder(R_xlen_t size);

    OutputBuilder(const OutputBuilder&) = delete;
    OutputBuilder& operator=(const OutputBuilder&) = delete;
    OutputBuilder(OutputBuilder&&) = delete;
    OutputBuilder& operator=(OutputBuilder&&) = delete;

    void reset(R_xlen_t size);
    R_xlen_t size() const noexcept;

    void set(R_xlen_t index, const OutputRecord& value);
    void set(
        R_xlen_t index, const char* data, std::size_t length,
        cetype_ext_t encoding
    );
    void set(
        R_xlen_t index, std::string_view value, cetype_ext_t encoding
    );
    void set_na(R_xlen_t index);
    [[nodiscard]] char* reserve(
        R_xlen_t index, std::size_t length, cetype_ext_t encoding
    );

    [[nodiscard]] OutputStore release_store();
    SEXP to_sexp();

private:
    R_xlen_t size_;
    charport::charvec::Builder builder_;
};

class GrowableOutputBuilder {
public:
    GrowableOutputBuilder();

    GrowableOutputBuilder(const GrowableOutputBuilder&) = delete;
    GrowableOutputBuilder& operator=(const GrowableOutputBuilder&) = delete;
    GrowableOutputBuilder(GrowableOutputBuilder&&) = delete;
    GrowableOutputBuilder& operator=(GrowableOutputBuilder&&) = delete;

    void reset() noexcept;
    std::size_t size() const noexcept;

    void append(const OutputRecord& value);
    void append(
        const char* data, std::size_t length, cetype_ext_t encoding
    );
    void append(std::string_view value, cetype_ext_t encoding);
    void append_na();
    [[nodiscard]] char* append_reserve(
        std::size_t length, cetype_ext_t encoding
    );

    [[nodiscard]] OutputStore release_store();
    SEXP to_sexp();

private:
    charport::charvec::GrowableBuilder builder_;
};

} // namespace altrep
} // namespace charr

#endif
