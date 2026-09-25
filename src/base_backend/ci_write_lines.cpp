#include "ci_stringi.h"
#include "io/string_view.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/utf8.h"
#include "../shared/write_lines.h"

#include <cstring>
#include <string>

namespace charr { namespace base_backend {

CHARR_ENTRYPOINT SEXP ci_write_lines(
    SEXP str, SEXP path, SEXP sep
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    path = entry_protections.protect_one(
        ci__prepare_arg_string_1_r(path, "con")
    );
    sep = entry_protections.protect_one(
        ci__prepare_arg_string_1_r(sep, "sep")
    );

    try {
        shared::write_lines::FileWriter file;
        shared::NativeToUtf8 converter;
        std::string description;
        std::string expanded_path;
        std::string separator_bytes;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const SEXP path_string = STRING_ELT(path, 0);
                if (path_string == NA_STRING)
                    throw StriException("invalid 'con' value");

                const shared::StringView separator_source =
                    io::as_shared_view(STRING_ELT(sep, 0));
                if (separator_source.is_na())
                    throw StriException("invalid 'sep' value");
                if (separator_source.enc == shared::StringEncoding::bytes)
                    throw StriException(MSG__BYTESENC);

                const R_xlen_t size = XLENGTH(str);
                for (R_xlen_t i = 0; i < size; ++i) {
                    const shared::StringView value = io::as_shared_view(
                        STRING_ELT(str, i)
                    );
                    if (value.is_na())
                        throw StriException("missing strings are not supported");
                    if (value.enc == shared::StringEncoding::bytes)
                        throw StriException(MSG__BYTESENC);
                }

                description = Rf_translateChar(path_string);
                expanded_path = R_ExpandFileName(description.c_str());

                const shared::StringView separator_utf8 =
                    shared::normalize_utf8_transient(
                        separator_source, converter
                    );
                if (separator_utf8.len > 0) {
                    separator_bytes.assign(
                        separator_utf8.ptr,
                        static_cast<std::size_t>(separator_utf8.len)
                    );
                }

                try {
                    file.reset(expanded_path.c_str());
                    for (R_xlen_t i = 0; i < size; ++i) {
                        const shared::StringView source = io::as_shared_view(
                            STRING_ELT(str, i)
                        );
                        const shared::StringView value =
                            shared::normalize_utf8_transient(source, converter);
                        file.write_utf8(value.ptr, value.len);
                        file.write_utf8(
                            separator_bytes.data(),
                            static_cast<int>(separator_bytes.size())
                        );
                    }
                    file.close();
                }
                catch (const shared::write_lines::FileWriteError& error) {
                    throw StriException(
                        "cannot write file '%s': %s",
                        description.c_str(), std::strerror(error.error())
                    );
                }

                result = entry_protections.reprotect_one(
                    str, result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
        CHARR_UNWIND_KEEP_RESULT();
    }
    CHARR_ENTRYPOINT_END();
}

} } // namespace charr::base_backend
