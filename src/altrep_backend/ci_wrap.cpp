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
#include "io/string_view.h"
#include "io/utf8_output.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"
#include "../shared/wrap.h"

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <vector>

namespace charr { namespace altrep_backend {

namespace wrap {

CHARR_CXX_HELPER shared::StringView normalize_input(
    const charport::StrView& source,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage
) {
    if (source.enc == cetype_ext_t::CE_BYTES)
        throw StriException(MSG__BYTESENC);
    return shared::normalize_utf8(
        io::as_shared_view(source), converter, storage
    );
}


CHARR_CXX_HELPER void require_icu_success(UErrorCode status)
{
    if (U_FAILURE(status))
        throw StriException(status);
}


CHARR_R_HELPER void emit_locale_warning_r(
    bool root_fallback
) noexcept {
    if (root_fallback) {
        Rf_warning(
            "%s", ICUError::getICUerrorName(U_USING_DEFAULT_WARNING)
        );
    }
}

} // namespace wrap

using namespace wrap;


/** Word wrap text
 *
 * @param str character vector
 * @param width single integer
 * @param cost_exponent single double
 * @param indent single integer
 * @param exdent single integer
 * @param prefix single string
 * @param initial single string
 * @param locale locale identifier or NULL for default locale
 * @param use_length single logical value
 *
 * @return list
 *
 * @version 0.1-?? (Bartek Tartanus)
 *          original implementation
 *
 * @version 0.2-2 (Marek Gagolewski, 2014-04-27)
 *          single function for wrap_greedy and wrap_dynamic
 *          (dispatch inside);
 *          use BreakIterator
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-06)
 *    new args: indent, exdent, prefix, initial
 *
 * @version 0.5-1 (Marek Gagolewski, 2014-12-19)
 *    #133 allow width <= 0
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-02-28)
 *    don't trim so many white spaces at the end of each word (normalize arg does that)
 *    #139: allow a "whitespace" break iterator
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-04-23)
 *    `use_length` arg added
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-06-09)
 *    BIGSKIP: no more CHARSXP on out on "" input
 */
CHARR_ENTRYPOINT SEXP ci_wrap(
    SEXP str, SEXP width, SEXP cost_exponent,
    SEXP indent, SEXP exdent, SEXP prefix, SEXP initial,
    SEXP whitespace_only, SEXP use_length, SEXP locale,
    SEXP normalize, SEXP output_mode
) noexcept {
    CHARR_ENTRYPOINT_BEGIN();

    const int output_mode_value = Rf_asInteger(output_mode);
    const bool flatten = output_mode_value == 1;
    const bool join = output_mode_value == 2;
    const bool normalize_value = ci__prepare_arg_logical_1_notNA_r(
        normalize, "normalize"
    );
    const bool use_length_value = ci__prepare_arg_logical_1_notNA_r(
        use_length, "use_length"
    );
    const double exponent_value = ci__prepare_arg_double_1_notNA_r(
        cost_exponent, "cost_exponent"
    );
    const bool whitespace_only_value = ci__prepare_arg_logical_1_notNA_r(
        whitespace_only, "whitespace_only"
    );

    int width_value = ci__prepare_arg_integer_1_notNA_r(width, "width");
    if (width_value <= 0)
        width_value = 0;

    const int indent_value = ci__prepare_arg_integer_1_notNA_r(
        indent, "indent"
    );
    if (indent_value < 0) {
        Rf_error(
            MSG__INCORRECT_NAMED_ARG "; " MSG__EXPECTED_POSITIVE,
            "indent"
        );
    }

    const int exdent_value = ci__prepare_arg_integer_1_notNA_r(
        exdent, "exdent"
    );
    if (exdent_value < 0) {
        Rf_error(
            MSG__INCORRECT_NAMED_ARG "; " MSG__EXPECTED_POSITIVE,
            "exdent"
        );
    }

    const char* selected_locale = ci__prepare_arg_locale_r(
        locale, "locale"
    );
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    prefix = entry_protections.protect_one(
        ci__prepare_arg_string_1_r(prefix, "prefix")
    );
    initial = entry_protections.protect_one(
        ci__prepare_arg_string_1_r(initial, "initial")
    );


    bool root_fallback_warning = false;
    R_len_t input_length = 0;

    try {
        charport::Reader input_reader;
        charport::Reader prefix_reader;
        charport::Reader initial_reader;
        charport::StrViews input_views;
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        shared::wrap::LineStart initial_indent;
        shared::wrap::LineStart prefix_indent;
        shared::wrap::LineStart prefix_exdent;
        shared::wrap::Engine engine;
        std::vector<io::OutputStore> stores;
        io::OutputBuilder child_output(0);
        io::OutputBuilder joined_output(0);
        io::GrowableOutputBuilder flat_output;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const shared::wrap::Options options{
                    selected_locale, width_value, exponent_value,
                    whitespace_only_value, use_length_value,
                    normalize_value
                };
                const shared::wrap::OpenResult opened = engine.reset(options);
                root_fallback_warning = opened.root_fallback;
                require_icu_success(opened.status);

                input_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                if (input_length > 0) {
                    input_reader.reset(str);
                    if (input_reader.size() != input_length) {
                        throw std::runtime_error(
                            "Reader length changed during word wrapping"
                        );
                    }
                    input_views.resize(input_length);
                    input_reader.views(
                        0, input_length,
                        input_views.ptrs(), input_views.lengths(),
                        input_views.encodings()
                    );
                }

                prefix_reader.reset(prefix);
                if (prefix_reader.size() != 1) {
                    throw std::runtime_error(
                        "prefix length changed during word wrapping"
                    );
                }
                const shared::StringView prefix_value = normalize_input(
                    prefix_reader.view(0), converter, storage
                );

                initial_reader.reset(initial);
                if (initial_reader.size() != 1) {
                    throw std::runtime_error(
                        "initial length changed during word wrapping"
                    );
                }
                const shared::StringView initial_value = normalize_input(
                    initial_reader.view(0), converter, storage
                );

                initial_indent.reset(initial_value, indent_value);
                prefix_indent.reset(prefix_value, indent_value);
                prefix_exdent.reset(prefix_value, exdent_value);

                if (!flatten && !join) {
                    stores.reserve(static_cast<std::size_t>(input_length));
                }
                else if (join) {
                    joined_output.reset(input_length);
                }
                else {
                    flat_output.reset();
                }

                const bool missing_start = initial_indent.is_na() ||
                    prefix_indent.is_na();
                for (R_len_t i = 0; i < input_length; ++i) {
                    const shared::StringView input = normalize_input(
                        input_views[i], converter, storage
                    );
                    if (input.is_na() || missing_start) {
                        if (flatten) {
                            flat_output.append_na();
                        }
                        else if (join) {
                            joined_output.set_na(i);
                        }
                        else {
                            stores.push_back(io::scalar_store(
                                io::missing_output_record()
                            ));
                        }
                        continue;
                    }

                    require_icu_success(engine.plan(
                        input,
                        i == 0 ? initial_indent : prefix_indent,
                        prefix_exdent
                    ));

                    if (join) {
                        const shared::wrap::Joined staged = engine.joined(
                            i == 0 ? initial_indent : prefix_indent,
                            prefix_exdent, true
                        );
                        char* destination = joined_output.reserve(
                            i, staged.size,
                            staged.ascii
                                ? cetype_ext_t::CE_ASCII
                                : cetype_ext_t::CE_UTF8
                        );
                        engine.write_joined(
                            destination,
                            i == 0 ? initial_indent : prefix_indent,
                            prefix_exdent
                        );
                        continue;
                    }

                    const int line_count = engine.line_count();
                    if (!flatten)
                        child_output.reset(line_count);

                    for (int line = 0; line < line_count; ++line) {
                        const shared::wrap::Line staged = engine.line(
                            line,
                            i == 0 ? initial_indent : prefix_indent,
                            prefix_exdent, true
                        );
                        const cetype_ext_t encoding = staged.ascii
                            ? cetype_ext_t::CE_ASCII
                            : cetype_ext_t::CE_UTF8;
                        char* destination = flatten
                            ? flat_output.append_reserve(
                                staged.size, encoding
                            )
                            : child_output.reserve(
                                line, staged.size, encoding
                            );
                        shared::wrap::Engine::write_line(
                            staged, destination
                        );
                    }

                    if (!flatten) {
                        stores.push_back(child_output.release_store());
                    }
                }

                if (flatten) {
                    result = entry_protections.reprotect_one(
                        flat_output.to_sexp(), result_index
                    );
                }
                else if (join) {
                    result = entry_protections.reprotect_one(
                        joined_output.to_sexp(), result_index
                    );
                }
                else {
                    result = entry_protections.reprotect_one(
                        Rf_allocVector(VECSXP, input_length), result_index
                    );
                    SEXP child = R_NilValue;
                    PROTECT_INDEX child_index;
                    callback_protections.protect_with_index(child, &child_index);
                    for (R_len_t i = 0; i < input_length; ++i) {
                        child = callback_protections.reprotect_slot(
                            io::finalize(std::move(stores[
                                static_cast<std::size_t>(i)
                            ])),
                            child_index
                        );
                        SET_VECTOR_ELT(result, i, child);
                    }
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_locale_warning_r(root_fallback_warning);
    );
}

} } // namespace charr::altrep_backend
