
/* This file is part of the 'stringi' project.
 * Copyright (c) 2013-2025, Marek Gagolewski <https://www.gagolewski.com/>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "ci_stringi.h"
#include "ci_parallel.h"
#include "io/reader_utils.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/nfc_normalizer.h"
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


namespace trans_normalization {

CHARR_CXX_HELPER void require_icu_success(UErrorCode status)
{
    if (U_FAILURE(status))
        throw StriException(status);
}


CHARR_NEUTRAL_HELPER std::size_t no_slot() noexcept
{
    return static_cast<std::size_t>(-1);
}


CHARR_NEUTRAL_HELPER const char* empty_input() noexcept
{
    static const char value = '\0';
    return &value;
}


CHARR_CXX_HELPER void prepare_parallel_inputs(
    const charport::StrViews& values,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    std::vector<std::size_t>& converted_slots,
    std::vector<shared::StringView>& converted_values
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
        case shared::StringEncoding::native: {
            if (converted_slots.empty()) {
                converted_slots.assign(
                    static_cast<std::size_t>(size), no_slot()
                );
            }
            converted_slots[static_cast<std::size_t>(i)] =
                converted_values.size();
            const shared::ByteView converted =
                value.enc == shared::StringEncoding::native
                    ? converter.native(value.ptr, value.len)
                    : converter.latin1(value.ptr, value.len);
            if (converted.len < 0 ||
                    (converted.ptr == nullptr && converted.len > 0)) {
                throw std::runtime_error(
                    "encoding conversion returned invalid bytes"
                );
            }
            const char* data = empty_input();
            if (converted.len > 0) {
                char* stable = storage.allocate(
                    static_cast<std::size_t>(converted.len)
                );
                std::memcpy(
                    stable, converted.ptr,
                    static_cast<std::size_t>(converted.len)
                );
                data = stable;
            }
            converted_values.push_back(shared::StringView{
                data, converted.len, shared::StringEncoding::utf8
            });
            break;
        }
        case shared::StringEncoding::bytes:
            throw std::runtime_error(
                "bytes encoding is not supported by this function"
            );
        case shared::StringEncoding::missing:
            throw std::invalid_argument(
                "non-missing NFC input has NA encoding"
            );
        case shared::StringEncoding::unknown:
            throw std::invalid_argument("unknown NFC input encoding");
        }
    }
}


CHARR_NEUTRAL_HELPER shared::StringView input_at(
    const charport::StrViews& values,
    const std::vector<std::size_t>& converted_slots,
    const std::vector<shared::StringView>& converted_values,
    R_len_t index
) noexcept
{
    if (!converted_slots.empty()) {
        const std::size_t slot =
            converted_slots[static_cast<std::size_t>(index)];
        if (slot != no_slot())
            return converted_values[slot];
    }
    return io::as_shared_view(values[index]);
}


class Body final : public ParallelBody {
public:
    CHARR_CXX_HELPER Body(
        const charport::StrViews& values,
        const std::vector<std::size_t>& converted_slots,
        const std::vector<shared::StringView>& converted_values,
        io::ParallelOutputBuilder& builder
    ) noexcept
        : values_(values), converted_slots_(converted_slots),
          converted_values_(converted_values), builder_(builder)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        shared::NfcNormalizer normalizer;
        require_icu_success(normalizer.reset());
        while (context.next_chunk()) {
            for (R_xlen_t i = context.begin; i < context.end; ++i) {
                UErrorCode status = U_ZERO_ERROR;
                const shared::StringView normalized =
                    normalizer.normalize_utf8(
                        input_at(
                            values_, converted_slots_, converted_values_,
                            static_cast<R_len_t>(i)
                        ),
                        status
                    );
                require_icu_success(status);

                if (normalized.is_na())
                    builder_.set_na(context.worker, i);
                else
                    builder_.set(
                        context.worker, i, io::as_charport_view(normalized)
                    );
            }
        }
    }

private:
    const charport::StrViews& values_;
    const std::vector<std::size_t>& converted_slots_;
    const std::vector<shared::StringView>& converted_values_;
    io::ParallelOutputBuilder& builder_;
};

} // namespace trans_normalization

using namespace trans_normalization;


/**
 * Perform Unicode NFC normalization
 *
 * @param str character vector
 * @return character vector
 */
CHARR_ENTRYPOINT SEXP ci_trans_nfc(SEXP str) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );

    try {
        shared::NfcNormalizer normalizer;
        charport::Reader reader;
        charport::StrViews values;
        io::OutputBuilder builder(0);
        io::ParallelOutputBuilder parallel_builder;
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        std::vector<std::size_t> converted_slots;
        std::vector<shared::StringView> converted_values;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t str_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                const shared::ParallelPlan plan = shared::parallel_plan(
                    true, str_length
                );

                UErrorCode status = U_ZERO_ERROR;
                if (plan.workers == 1) {
                    status = normalizer.reset();
                    if (U_FAILURE(status))
                        throw StriException(status);
                }

                if (str_length > 0) {
                    reader.reset(str);
                    if (reader.size() != str_length) {
                        throw std::runtime_error(
                            "Reader length changed during NFC normalization"
                        );
                    }
                    values.resize(str_length);
                    reader.views(
                        0, str_length,
                        values.ptrs(), values.lengths(), values.encodings()
                    );
                }
                if (plan.workers > 1) {
                    prepare_parallel_inputs(
                        values, converter, storage,
                        converted_slots, converted_values
                    );
                    parallel_builder.reset(str_length, plan.workers);
                    Body body(
                        values, converted_slots, converted_values,
                        parallel_builder
                    );
                    shared::run_parallel(plan, str_length, body);
                    result = entry_protections.reprotect_one(
                        parallel_builder.to_sexp(), result_index
                    );
                }
                else {
                    builder.reset(str_length);

                    for (R_len_t i = 0; i < str_length; ++i) {
                        status = U_ZERO_ERROR;
                        const shared::StringView normalized =
                            normalizer.normalize(
                                io::as_shared_view(values[i]), status
                            );
                        if (U_FAILURE(status))
                            throw StriException(status);

                        if (normalized.is_na())
                            builder.set_na(i);
                        else
                            builder.set(i, io::as_charport_view(normalized));
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
