// Copied from stringi 19e9586ba39b3320df49355e32bd18d74ed6098f; stri_* renamed to ci_*. See inst/COPYRIGHTS.
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
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
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
#include "io/reader_utils.h"
#include "collator/options.h"
#include "io/string_view.h"
#include "../shared/collation_ordering.h"
#include "../shared/collator.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <charport.h>

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <vector>


namespace charr { namespace altrep_backend {

namespace order_rank {

CHARR_CXX_HELPER void require_icu_success(UErrorCode status)
{
    if (U_FAILURE(status))
        throw StriException(status);
}


CHARR_R_HELPER void emit_warnings(bool root_fallback) noexcept
{
    if (root_fallback) {
        Rf_warning(
            "%s", ICUError::getICUerrorName(U_USING_DEFAULT_WARNING)
        );
    }
}

} // namespace order_rank

using namespace order_rank;


/**
 * Return a stable collation ordering permutation.
 *
 * @param str character vector
 * @param decreasing single logical value
 * @param na_last single logical value, with NA meaning omit
 * @param opts_collator collator options
 * @return integer vector of one-based source indices
 */
CHARR_ENTRYPOINT SEXP ci_order(
    SEXP str, SEXP decreasing, SEXP na_last, SEXP opts_collator
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const bool decreasing_value = ci__prepare_arg_logical_1_notNA_r(
        decreasing, "decreasing"
    );
    na_last = entry_protections.protect_one(
        ci__prepare_arg_logical_1_r(na_last, "na_last")
    );
    const int na_last_value = LOGICAL(na_last)[0];
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    const shared::CollatorOptions options =
        collator::prepare_options(opts_collator);


    bool root_fallback_warning = false;

    try {
        shared::Collator collator_owner;
        charport::Reader reader;
        charport::StrViews views;
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        std::vector<shared::StringView> inputs;
        std::vector<int> order;
        std::vector<int> missing;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const shared::CollatorOpenResult opened =
                    collator_owner.reset(options);
                root_fallback_warning = opened.root_fallback;
                require_icu_success(opened.status);

                const R_len_t size = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                if (size > 0) {
                    reader.reset(str);
                    if (reader.size() != size) {
                        throw std::runtime_error(
                            "Reader length changed during ordering input setup"
                        );
                    }
                    views.resize(size);
                    reader.views(
                        0, size,
                        views.ptrs(), views.lengths(), views.encodings()
                    );

                    inputs.resize(static_cast<std::size_t>(size));
                    for (R_len_t i = 0; i < size; ++i) {
                        const shared::StringView value =
                            io::as_shared_view(views[i]);
                        if (value.enc == shared::StringEncoding::bytes)
                            throw StriException(MSG__BYTESENC);
                        inputs[static_cast<std::size_t>(i)] =
                            shared::normalize_utf8(
                                value, converter, storage
                            );
                    }
                }

                require_icu_success(shared::build_collation_order(
                    inputs.size() == 0 ? nullptr : &inputs[0],
                    inputs.size(), decreasing_value, collator_owner.get(),
                    order, missing
                ));

                const std::size_t output_size = order.size() +
                    (na_last_value == NA_LOGICAL ? 0 : missing.size());
                result = entry_protections.reprotect_one(
                    Rf_allocVector(
                        INTSXP, static_cast<R_xlen_t>(output_size)
                    ),
                    result_index
                );
                int* output = INTEGER(result);
                std::size_t output_index = 0;

                if (na_last_value == FALSE) {
                    for (std::size_t i = 0; i < missing.size(); ++i)
                        output[output_index++] = missing[i]+1;
                }
                for (std::size_t i = 0; i < order.size(); ++i)
                    output[output_index++] = order[i]+1;
                if (na_last_value == TRUE) {
                    for (std::size_t i = 0; i < missing.size(); ++i)
                        output[output_index++] = missing[i]+1;
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_warnings(root_fallback_warning);
    );
}


/**
 * Rank strings with increasing collation and minimum ranks for ties.
 *
 * @param str character vector
 * @param opts_collator collator options
 * @return integer vector aligned with the input
 */
CHARR_ENTRYPOINT SEXP ci_rank(SEXP str, SEXP opts_collator) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    const shared::CollatorOptions options =
        collator::prepare_options(opts_collator);


    bool root_fallback_warning = false;

    try {
        shared::Collator collator_owner;
        charport::Reader reader;
        charport::StrViews views;
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        std::vector<shared::StringView> inputs;
        std::vector<int> order;
        std::vector<int> missing;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const shared::CollatorOpenResult opened =
                    collator_owner.reset(options);
                root_fallback_warning = opened.root_fallback;
                require_icu_success(opened.status);

                const R_len_t size = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                if (size > 0) {
                    reader.reset(str);
                    if (reader.size() != size) {
                        throw std::runtime_error(
                            "Reader length changed during rank input setup"
                        );
                    }
                    views.resize(size);
                    reader.views(
                        0, size,
                        views.ptrs(), views.lengths(), views.encodings()
                    );

                    inputs.resize(static_cast<std::size_t>(size));
                    for (R_len_t i = 0; i < size; ++i) {
                        const shared::StringView value =
                            io::as_shared_view(views[i]);
                        if (value.enc == shared::StringEncoding::bytes)
                            throw StriException(MSG__BYTESENC);
                        inputs[static_cast<std::size_t>(i)] =
                            shared::normalize_utf8(
                                value, converter, storage
                            );
                    }
                }

                require_icu_success(shared::build_collation_order(
                    inputs.size() == 0 ? nullptr : &inputs[0],
                    inputs.size(), false, collator_owner.get(),
                    order, missing
                ));

                result = entry_protections.reprotect_one(
                    Rf_allocVector(INTSXP, size), result_index
                );
                int* output = INTEGER(result);
                for (R_len_t i = 0; i < size; ++i)
                    output[i] = NA_INTEGER;

                require_icu_success(shared::assign_min_collation_ranks(
                    inputs.size() == 0 ? nullptr : &inputs[0],
                    collator_owner.get(),
                    order.size() == 0 ? nullptr : &order[0],
                    order.size(), output
                ));

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_warnings(root_fallback_warning);
    );
}

} } // namespace charr::altrep_backend
