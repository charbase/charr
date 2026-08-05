// Derived from stringi.
// Copyright (c) 2013-2025, Marek Gagolewski. See inst/COPYRIGHTS.

#include "ci_stringi.h"
#include "ci_parallel.h"
#include "io/reader_utils.h"
#include "../shared/case_mapper.h"
#include "../shared/entrypoint.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "altrep_backend/io/string_view.h"
#include "altrep_backend/io/utf8_output.h"

#include <charport.h>

#include <cstddef>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <vector>

namespace charr { namespace altrep_backend {


namespace trans_casemap {

CHARR_CXX_HELPER void require_icu_success(UErrorCode status)
{
    if (U_FAILURE(status))
        throw StriException(status);
}


CHARR_NEUTRAL_HELPER std::size_t no_slot() noexcept
{
    return static_cast<std::size_t>(-1);
}


CHARR_CXX_HELPER void prepare_parallel_inputs(
    const charport::StrViews& values,
    shared::CaseMapper& mapper,
    shared::SliceArena& storage,
    std::vector<std::size_t>& converted_slots,
    std::vector<shared::CaseMapInput>& converted_inputs
)
{
    const R_xlen_t size = values.size();
    for (R_xlen_t i = 0; i < size; ++i) {
        const shared::StringView value = io::as_shared_view(values[i]);
        if (value.is_na())
            continue;

        switch (value.enc) {
        case shared::StringEncoding::ascii:
        case shared::StringEncoding::utf8:
        case shared::StringEncoding::ascii_or_utf8:
            break;
        case shared::StringEncoding::latin1:
        case shared::StringEncoding::native:
            if (converted_slots.empty()) {
                converted_slots.assign(
                    static_cast<std::size_t>(size), no_slot()
                );
            }
            converted_slots[static_cast<std::size_t>(i)] =
                converted_inputs.size();
            {
                shared::CaseMapInput input = mapper.prepare(value);
                if (input.length > 0) {
                    char* stable = storage.allocate(
                        static_cast<std::size_t>(input.length)
                    );
                    std::memcpy(
                        stable, input.data,
                        static_cast<std::size_t>(input.length)
                    );
                    input.data = stable;
                }
                converted_inputs.push_back(input);
            }
            break;
        case shared::StringEncoding::bytes:
            throw std::runtime_error(
                "bytes encoding is not supported by this function"
            );
        case shared::StringEncoding::missing:
            throw std::invalid_argument(
                "non-missing case-map input has NA encoding"
            );
        case shared::StringEncoding::unknown:
            throw std::invalid_argument("unknown case-map input encoding");
        }
    }
}


CHARR_CXX_HELPER shared::CaseMapInput input_at(
    const charport::StrViews& values,
    const std::vector<std::size_t>& converted_slots,
    const std::vector<shared::CaseMapInput>& converted_inputs,
    R_len_t index,
    shared::CaseMapper& mapper
)
{
    if (!converted_slots.empty()) {
        const std::size_t slot =
            converted_slots[static_cast<std::size_t>(index)];
        if (slot != no_slot())
            return converted_inputs[slot];
    }
    return mapper.prepare_utf8(io::as_shared_view(values[index]));
}


class Body final : public ParallelBody {
public:
    CHARR_CXX_HELPER Body(
        const char* locale, shared::CaseMapMode mode,
        const charport::StrViews& values,
        const std::vector<std::size_t>& converted_slots,
        const std::vector<shared::CaseMapInput>& converted_inputs,
        io::ParallelOutputBuilder& builder
    ) noexcept
        : locale_(locale), mode_(mode), values_(values),
          converted_slots_(converted_slots),
          converted_inputs_(converted_inputs), builder_(builder)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        shared::CaseMapper mapper;
        mapper.reset(locale_, mode_);
        while (context.next_chunk()) {
            for (R_xlen_t i = context.begin; i < context.end; ++i) {
                const shared::StringView source =
                    io::as_shared_view(values_[i]);
                if (source.is_na()) {
                    builder_.set_na(context.worker, i);
                    continue;
                }

                const shared::CaseMapInput input = input_at(
                    values_, converted_slots_, converted_inputs_,
                    static_cast<R_len_t>(i), mapper
                );
                if (mapper.has_ascii_fast_path(input)) {
                    char* output = builder_.reserve(
                        context.worker, i,
                        static_cast<std::size_t>(input.length),
                        CETYPE_EXT_ASCII
                    );
                    mapper.map_ascii(input, output);
                    continue;
                }

                UErrorCode status = U_ZERO_ERROR;
                const shared::StringView mapped =
                    mapper.map_icu(input, status);
                require_icu_success(status);
                builder_.set(
                    context.worker, i, io::as_charport_view(mapped)
                );
            }
        }
    }

private:
    const char* locale_;
    shared::CaseMapMode mode_;
    const charport::StrViews& values_;
    const std::vector<std::size_t>& converted_slots_;
    const std::vector<shared::CaseMapInput>& converted_inputs_;
    io::ParallelOutputBuilder& builder_;
};

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
        io::ParallelOutputBuilder parallel_builder;
        shared::CaseMapper mapper;
        shared::SliceArena storage;
        std::vector<std::size_t> converted_slots;
        std::vector<shared::CaseMapInput> converted_inputs;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t str_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                const shared::ParallelPlan plan = shared::parallel_plan(
                    true, str_length
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
                if (plan.workers > 1) {
                    prepare_parallel_inputs(
                        values, mapper, storage,
                        converted_slots, converted_inputs
                    );
                    parallel_builder.reset(str_length, plan.workers);
                    Body body(
                        qloc, shared::CaseMapMode::lower,
                        values, converted_slots, converted_inputs,
                        parallel_builder
                    );
                    shared::run_parallel(plan, str_length, body);
                    result = entry_protections.reprotect_one(
                        parallel_builder.to_sexp(), result_index
                    );
                }
                else {
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
                                CETYPE_EXT_ASCII
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
                }
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
        io::ParallelOutputBuilder parallel_builder;
        shared::CaseMapper mapper;
        shared::SliceArena storage;
        std::vector<std::size_t> converted_slots;
        std::vector<shared::CaseMapInput> converted_inputs;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t str_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                const shared::ParallelPlan plan = shared::parallel_plan(
                    true, str_length
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
                if (plan.workers > 1) {
                    prepare_parallel_inputs(
                        values, mapper, storage,
                        converted_slots, converted_inputs
                    );
                    parallel_builder.reset(str_length, plan.workers);
                    Body body(
                        qloc, shared::CaseMapMode::upper,
                        values, converted_slots, converted_inputs,
                        parallel_builder
                    );
                    shared::run_parallel(plan, str_length, body);
                    result = entry_protections.reprotect_one(
                        parallel_builder.to_sexp(), result_index
                    );
                }
                else {
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
                                CETYPE_EXT_ASCII
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
                }
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}

} } // namespace charr::altrep_backend
