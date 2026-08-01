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
#include "io/string_view.h"
#include "../shared/entrypoint.h"
#include "../shared/line_split.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/string_view.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <cstddef>
#include <exception>
#include <vector>


namespace charr { namespace base_backend {

namespace split_lines {

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

} // namespace split_lines

using namespace split_lines;


/**
 * Split one string into text lines.
 *
 * @param str character vector
 * @return character vector
 */
CHARR_ENTRYPOINT SEXP ci_split_lines1(SEXP str) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(ci__prepare_arg_string_1_r(str, "str"));

    try {
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        shared::line_split::ScanResult scan;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const SEXP* values = STRING_PTR_RO(str);
                const shared::StringView source =
                    io::as_shared_view(values[0]);
                if (source.enc == shared::StringEncoding::bytes)
                    throw StriException(MSG__BYTESENC);
                const shared::StringView value = shared::normalize_utf8(
                    source, converter, storage
                );

                if (value.is_na()) {
                    result = entry_protections.reprotect_one(str, result_index);
                }
                else {
                    shared::line_split::scan_utf8(
                        value.ptr, value.len, false, false, scan
                    );
                    result = entry_protections.reprotect_one(
                        Rf_allocVector(
                            STRSXP,
                            static_cast<R_xlen_t>(scan.lines.size())
                        ),
                        result_index
                    );
                    for (std::size_t i = 0; i < scan.lines.size(); ++i) {
                        const shared::line_split::LineSlice& line =
                            scan.lines[i];
                        SET_STRING_ELT(
                            result, static_cast<R_xlen_t>(i),
                            Rf_mkCharLenCE(
                                value.ptr + line.begin,
                                line.length, CE_UTF8
                            )
                        );
                    }
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}


/**
 * Split strings into text lines.
 *
 * @param str character vector
 * @param omit_empty logical vector
 * @return list of character vectors
 */
CHARR_ENTRYPOINT SEXP ci_split_lines(SEXP str, SEXP omit_empty) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    omit_empty = entry_protections.protect_one(ci__prepare_arg_logical_r(
        omit_empty, "omit_empty"
    ));

    const R_len_t str_length = LENGTH(str);
    const R_len_t omit_empty_length = LENGTH(omit_empty);
    bool recycling_warning = false;
    const R_len_t vectorize_length = recycling_length(
        str_length, omit_empty_length, recycling_warning
    );
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);

    try {
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        std::vector<shared::StringView> normalized;
        shared::line_split::ScanResult scan;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                // String normalization precedes logical-vector access in
                // stringi, including errors from a later bytes-marked record.
                if (vectorize_length > 0) {
                    const SEXP* values = STRING_PTR_RO(str);
                    normalized.resize(
                        static_cast<std::size_t>(str_length)
                    );
                    for (R_len_t i = 0; i < str_length; ++i) {
                        const shared::StringView source =
                            io::as_shared_view(values[i]);
                        if (source.enc == shared::StringEncoding::bytes)
                            throw StriException(MSG__BYTESENC);
                        normalized[static_cast<std::size_t>(i)] =
                            shared::normalize_utf8(
                                source, converter, storage
                            );
                    }
                }

                const int* omit_empty_values = LOGICAL_RO(omit_empty);

                result = entry_protections.reprotect_one(
                    Rf_allocVector(VECSXP, vectorize_length), result_index
                );
                SEXP child = R_NilValue;
                PROTECT_INDEX child_index;
                callback_protections.protect_with_index(child, &child_index);

                // Visit recycled outputs source by source, matching the
                // original container's traversal and allocation order.
                for (R_len_t source_index = 0;
                        vectorize_length > 0 && source_index < str_length;
                        ++source_index) {
                    const shared::StringView& value = normalized[
                        static_cast<std::size_t>(source_index)
                    ];
                    for (R_xlen_t wide_index = source_index;
                            wide_index < vectorize_length;
                            wide_index += str_length) {
                        const R_len_t i = static_cast<R_len_t>(wide_index);
                        if (value.is_na()) {
                            child = callback_protections.reprotect_slot(
                                Rf_allocVector(STRSXP, 1), child_index
                            );
                            SET_STRING_ELT(child, 0, NA_STRING);
                            SET_VECTOR_ELT(result, i, child);
                            continue;
                        }

                        const int omit = omit_empty_values[
                            i % omit_empty_length
                        ];
                        shared::line_split::scan_utf8(
                            value.ptr, value.len, omit != 0, true, scan
                        );
                        child = callback_protections.reprotect_slot(
                            Rf_allocVector(
                                STRSXP,
                                static_cast<R_xlen_t>(scan.lines.size())
                            ),
                            child_index
                        );
                        for (std::size_t j = 0;
                                j < scan.lines.size(); ++j) {
                            const shared::line_split::LineSlice& line =
                                scan.lines[j];
                            SET_STRING_ELT(
                                child, static_cast<R_xlen_t>(j),
                                Rf_mkCharLenCE(
                                    value.ptr + line.begin,
                                    line.length, CE_UTF8
                                )
                            );
                        }
                        SET_VECTOR_ELT(result, i, child);
                    }
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}

} } // namespace charr::base_backend
