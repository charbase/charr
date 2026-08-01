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
#include "io/reader_utils.h"
#include "collator/options.h"
#include "../shared/collator.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"
#include "altrep_backend/io/string_view.h"

#include <charport.h>
#include <unicode/ucol.h>

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <vector>

namespace charr { namespace altrep_backend {


namespace compare {

CHARR_NEUTRAL_HELPER R_len_t recycling_length(
    R_len_t first, R_len_t second, bool& warning
) noexcept
{
    warning = false;
    if (first <= 0 || second <= 0)
        return 0;

    const R_len_t result = first > second ? first : second;
    warning = result % first != 0 || result % second != 0;
    return result;
}

} // namespace compare

using namespace compare;


/**
 * Compare elements in two character vectors for collation equivalence.
 *
 * @param e1 character vector
 * @param e2 character vector
 * @param opts_collator collation options
 * @return logical vector
 */
CHARR_ENTRYPOINT SEXP ci_cmp_equiv(
    SEXP e1, SEXP e2, SEXP opts_collator
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    e1 = entry_protections.protect_one(
        ci__prepare_arg_string_r(e1, "e1")
    );
    e2 = entry_protections.protect_one(
        ci__prepare_arg_string_r(e2, "e2")
    );
    const shared::CollatorOptions options =
        collator::prepare_options(opts_collator);


    bool root_fallback_warning = false;
    bool recycling_warning = false;

    try {
        shared::Collator col;
        charport::Reader e1_reader;
        charport::Reader e2_reader;
        charport::StrViews e1_views;
        charport::StrViews e2_views;
        shared::NativeToUtf8 e1_converter;
        shared::NativeToUtf8 e2_converter;
        shared::SliceArena e1_storage;
        shared::SliceArena e2_storage;
        std::vector<shared::StringView> e1_inputs;
        std::vector<shared::StringView> e2_inputs;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t e1_length = io::checked_r_len(
                    XLENGTH(e1), "character vectors"
                );
                const R_len_t e2_length = io::checked_r_len(
                    XLENGTH(e2), "character vectors"
                );
                const R_len_t vectorize_length = recycling_length(
                    e1_length, e2_length, recycling_warning
                );

                const shared::CollatorOpenResult open = col.reset(options);
                if (U_FAILURE(open.status))
                    throw StriException(open.status);
                root_fallback_warning = open.root_fallback;

                if (vectorize_length > 0) {
                    e1_reader.reset(e1);
                    if (e1_reader.size() != e1_length) {
                        throw std::runtime_error(
                            "Reader length changed during comparison"
                        );
                    }
                    e1_views.resize(e1_length);
                    e1_reader.views(
                        0, e1_length,
                        e1_views.ptrs(), e1_views.lengths(),
                        e1_views.encodings()
                    );
                    e1_inputs.resize(static_cast<std::size_t>(e1_length));
                    for (R_len_t i = 0; i < e1_length; ++i) {
                        e1_inputs[static_cast<std::size_t>(i)] =
                            shared::normalize_utf8(
                                io::as_shared_view(e1_views[i]),
                                e1_converter, e1_storage
                            );
                    }

                    e2_reader.reset(e2);
                    if (e2_reader.size() != e2_length) {
                        throw std::runtime_error(
                            "Reader length changed during comparison"
                        );
                    }
                    e2_views.resize(e2_length);
                    e2_reader.views(
                        0, e2_length,
                        e2_views.ptrs(), e2_views.lengths(),
                        e2_views.encodings()
                    );
                    e2_inputs.resize(static_cast<std::size_t>(e2_length));
                    for (R_len_t i = 0; i < e2_length; ++i) {
                        e2_inputs[static_cast<std::size_t>(i)] =
                            shared::normalize_utf8(
                                io::as_shared_view(e2_views[i]),
                                e2_converter, e2_storage
                            );
                    }
                }

                result = entry_protections.reprotect_one(
                    Rf_allocVector(LGLSXP, vectorize_length), result_index
                );
                int* output = LOGICAL(result);

                for (R_len_t i = 0; i < vectorize_length; ++i) {
                    const shared::StringView& first = e1_inputs[
                        static_cast<std::size_t>(i % e1_length)
                    ];
                    const shared::StringView& second = e2_inputs[
                        static_cast<std::size_t>(i % e2_length)
                    ];
                    if (first.is_na() || second.is_na()) {
                        output[i] = NA_LOGICAL;
                        continue;
                    }

                    UErrorCode status = U_ZERO_ERROR;
                    output[i] = ucol_strcollUTF8(
                        col.get(), first.ptr, first.len,
                        second.ptr, second.len, &status
                    ) == UCOL_EQUAL;
                    if (U_FAILURE(status))
                        throw StriException(status);
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        if (root_fallback_warning) {
            Rf_warning(
                "%s", ICUError::getICUerrorName(U_USING_DEFAULT_WARNING)
            );
        }
        if (recycling_warning)
            Rf_warning(MSG__WARN_RECYCLING_RULE);
    );
}


} } // namespace charr::altrep_backend
