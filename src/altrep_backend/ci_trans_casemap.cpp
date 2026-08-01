// Derived from stringi 19e9586ba39b3320df49355e32bd18d74ed6098f.
// Copyright (c) 2013-2025, Marek Gagolewski. See inst/COPYRIGHTS.

#include "ci_stringi.h"
#include "io/reader_utils.h"
#include "../shared/case_mapper.h"
#include "../shared/entrypoint.h"
#include "../shared/protect.h"
#include "../shared/unwind.h"
#include "altrep_backend/io/string_view.h"
#include "altrep_backend/io/utf8_output.h"

#include <charport.h>

#include <cstddef>
#include <exception>
#include <stdexcept>

namespace charr { namespace altrep_backend {


namespace trans_casemap {

CHARR_CXX_HELPER void require_icu_success(UErrorCode status)
{
    if (U_FAILURE(status))
        throw StriException(status);
}

} // namespace trans_casemap

using namespace trans_casemap;


/**
 * Convert to lower case
 *
 * @param str character vector
 * @param locale single string identifying the locale
 * @return character vector
 */
CHARR_ENTRYPOINT SEXP ci_trans_tolower(
    SEXP str, SEXP locale
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const char* qloc = ci__prepare_arg_locale_r(locale, "locale");
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );

    try {
        charport::Reader reader;
        charport::StrViews values;
        io::OutputBuilder builder(0);
        shared::CaseMapper mapper;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t str_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );

                reader.reset(str);
                if (reader.size() != str_length) {
                    throw std::runtime_error(
                        "Reader length changed during lowercase conversion"
                    );
                }
                values.resize(str_length);
                if (str_length > 0) {
                    reader.views(
                        0, str_length,
                        values.ptrs(), values.lengths(), values.encodings()
                    );
                }
                builder.reset(str_length);
                mapper.reset(qloc, shared::CaseMapMode::lower);

                for (R_len_t i = 0; i < str_length; ++i) {
                    const charport::StrView source_view = values[i];
                    if (source_view.is_na()) {
                        builder.set_na(i);
                        continue;
                    }

                    const shared::CaseMapInput input = mapper.prepare(
                        io::as_shared_view(source_view)
                    );
                    if (mapper.has_ascii_fast_path(input)) {
                        char* output = builder.reserve(
                            i, static_cast<std::size_t>(input.length),
                            cetype_ext_t::CE_ASCII
                        );
                        mapper.map_ascii(input, output);
                        continue;
                    }

                    UErrorCode status = U_ZERO_ERROR;
                    const shared::StringView mapped = mapper.map_icu(
                        input, status
                    );
                    require_icu_success(status);
                    builder.set(i, io::as_charport_view(mapped));
                }

                result = entry_protections.reprotect_one(
                    builder.to_sexp(), result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}


/**
 * Convert to upper case
 *
 * @param str character vector
 * @param locale single string identifying the locale
 * @return character vector
 */
CHARR_ENTRYPOINT SEXP ci_trans_toupper(
    SEXP str, SEXP locale
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const char* qloc = ci__prepare_arg_locale_r(locale, "locale");
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );

    try {
        charport::Reader reader;
        charport::StrViews values;
        io::OutputBuilder builder(0);
        shared::CaseMapper mapper;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t str_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );

                reader.reset(str);
                if (reader.size() != str_length) {
                    throw std::runtime_error(
                        "Reader length changed during uppercase conversion"
                    );
                }
                values.resize(str_length);
                if (str_length > 0) {
                    reader.views(
                        0, str_length,
                        values.ptrs(), values.lengths(), values.encodings()
                    );
                }
                builder.reset(str_length);
                mapper.reset(qloc, shared::CaseMapMode::upper);

                for (R_len_t i = 0; i < str_length; ++i) {
                    const charport::StrView source_view = values[i];
                    if (source_view.is_na()) {
                        builder.set_na(i);
                        continue;
                    }

                    const shared::CaseMapInput input = mapper.prepare(
                        io::as_shared_view(source_view)
                    );
                    if (mapper.has_ascii_fast_path(input)) {
                        char* output = builder.reserve(
                            i, static_cast<std::size_t>(input.length),
                            cetype_ext_t::CE_ASCII
                        );
                        mapper.map_ascii(input, output);
                        continue;
                    }

                    UErrorCode status = U_ZERO_ERROR;
                    const shared::StringView mapped = mapper.map_icu(
                        input, status
                    );
                    require_icu_success(status);
                    builder.set(i, io::as_charport_view(mapped));
                }

                result = entry_protections.reprotect_one(
                    builder.to_sexp(), result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}

} } // namespace charr::altrep_backend
