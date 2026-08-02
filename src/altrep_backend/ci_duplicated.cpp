
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

namespace duplicated {

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

} // namespace duplicated

using namespace duplicated;


/**
 * Determine which strings duplicate an earlier collation-equivalent value.
 *
 * @param str character vector
 * @param fromLast logical value
 * @param opts_collator collator options
 * @return logical vector
 */
CHARR_ENTRYPOINT SEXP ci_duplicated(
    SEXP str, SEXP fromLast, SEXP opts_collator
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    const bool from_last = ci__prepare_arg_logical_1_notNA_r(
        fromLast, "fromLast"
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
                            "Reader length changed during duplicated input setup"
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

                result = entry_protections.reprotect_one(
                    Rf_allocVector(LGLSXP, size), result_index
                );
                require_icu_success(shared::mark_collation_duplicates(
                    inputs.size() == 0 ? nullptr : &inputs[0],
                    inputs.size(), from_last, collator_owner.get(),
                    LOGICAL(result)
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
