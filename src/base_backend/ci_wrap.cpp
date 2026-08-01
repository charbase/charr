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
#include "io/string_view.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"
#include "../shared/wrap.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace charr { namespace base_backend {

namespace wrap {

class CHARR_OWNER_TYPE FlatOutput {
public:
    CHARR_CXX_HELPER FlatOutput() noexcept = default;
    CHARR_CXX_HELPER ~FlatOutput() noexcept = default;

    FlatOutput(const FlatOutput&) = delete;
    FlatOutput& operator=(const FlatOutput&) = delete;
    FlatOutput(FlatOutput&&) = delete;
    FlatOutput& operator=(FlatOutput&&) = delete;

    CHARR_CXX_HELPER void reserve(int size)
    {
        const std::size_t count = static_cast<std::size_t>(size);
        offsets_.reserve(count);
        lengths_.reserve(count);
        missing_.reserve(count);
    }

    CHARR_CXX_HELPER char* append_reserve(std::size_t size)
    {
        const std::size_t maximum = static_cast<std::size_t>(R_LEN_T_MAX);
        if (size > maximum)
            throw std::length_error("wrapped string exceeds R's string length limit");

        const std::size_t offset = data_.size();
        if (size > data_.max_size()-offset)
            throw std::length_error("wrapped output exceeds C++ storage limits");
        data_.resize(offset+size);
        offsets_.push_back(offset);
        lengths_.push_back(static_cast<int>(size));
        missing_.push_back(0);
        return size == 0 ? nullptr : data_.data()+offset;
    }

    CHARR_CXX_HELPER void append_na()
    {
        offsets_.push_back(0);
        lengths_.push_back(0);
        missing_.push_back(1);
    }

    CHARR_NEUTRAL_HELPER R_xlen_t size() const noexcept
    {
        return static_cast<R_xlen_t>(offsets_.size());
    }

    CHARR_NEUTRAL_HELPER bool is_missing(
        R_xlen_t index
    ) const noexcept {
        return missing_[static_cast<std::size_t>(index)] != 0;
    }

    CHARR_NEUTRAL_HELPER int length(R_xlen_t index) const noexcept
    {
        return lengths_[static_cast<std::size_t>(index)];
    }

    CHARR_NEUTRAL_HELPER const char* data(
        R_xlen_t index
    ) const noexcept {
        const std::size_t current = static_cast<std::size_t>(index);
        return lengths_[current] == 0
            ? ""
            : data_.data()+offsets_[current];
    }

private:
    std::vector<char> data_;
    std::vector<std::size_t> offsets_;
    std::vector<int> lengths_;
    std::vector<std::uint8_t> missing_;
};


CHARR_CXX_HELPER shared::StringView normalize_input(
    const shared::StringView& source,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage
) {
    if (source.enc == shared::StringEncoding::bytes)
        throw StriException(MSG__BYTESENC);
    return shared::normalize_utf8(source, converter, storage);
}


CHARR_CXX_HELPER void require_icu_success(UErrorCode status)
{
    if (U_FAILURE(status))
        throw StriException(status);
}


CHARR_R_HELPER void write_flat_output_r(
    SEXP output, const FlatOutput& staged
) noexcept {
    for (R_xlen_t i = 0; i < staged.size(); ++i) {
        if (staged.is_missing(i)) {
            SET_STRING_ELT(output, i, NA_STRING);
            continue;
        }
        SET_STRING_ELT(
            output, i,
            Rf_mkCharLenCE(staged.data(i), staged.length(i), CE_UTF8)
        );
    }
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
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    prefix = entry_protections.protect_one(ci__prepare_arg_string_1_r(prefix, "prefix"));
    initial = entry_protections.protect_one(ci__prepare_arg_string_1_r(initial, "initial"));

    const R_len_t input_length = LENGTH(str);

    bool root_fallback_warning = false;

    try {
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        std::vector<shared::StringView> inputs;
        shared::wrap::LineStart initial_indent;
        shared::wrap::LineStart prefix_indent;
        shared::wrap::LineStart prefix_exdent;
        shared::wrap::Engine engine;
        FlatOutput flat_output;
        std::string line_buffer;

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

                inputs.resize(static_cast<std::size_t>(input_length));
                const SEXP* input_values = input_length > 0
                    ? STRING_PTR_RO(str)
                    : nullptr;
                for (R_len_t i = 0; i < input_length; ++i) {
                    inputs[static_cast<std::size_t>(i)] = normalize_input(
                        io::as_shared_view(input_values[i]), converter, storage
                    );
                }

                const shared::StringView prefix_value = normalize_input(
                    io::as_shared_view(STRING_ELT(prefix, 0)),
                    converter, storage
                );
                const shared::StringView initial_value = normalize_input(
                    io::as_shared_view(STRING_ELT(initial, 0)),
                    converter, storage
                );
                initial_indent.reset(initial_value, indent_value);
                prefix_indent.reset(prefix_value, indent_value);
                prefix_exdent.reset(prefix_value, exdent_value);

                SEXP child = R_NilValue;
                PROTECT_INDEX child_index;
                callback_protections.protect_with_index(child, &child_index);
                if (!flatten && !join) {
                    result = entry_protections.reprotect_one(
                        Rf_allocVector(VECSXP, input_length), result_index
                    );
                }
                else {
                    flat_output.reserve(input_length);
                }

                const bool missing_start = initial_indent.is_na() ||
                    prefix_indent.is_na();
                for (R_len_t i = 0; i < input_length; ++i) {
                    const shared::StringView& input = inputs[
                        static_cast<std::size_t>(i)
                    ];
                    if (input.is_na() || missing_start) {
                        if (flatten || join) {
                            flat_output.append_na();
                        }
                        else {
                            child = callback_protections.reprotect_slot(
                                Rf_allocVector(STRSXP, 1), child_index
                            );
                            SET_STRING_ELT(child, 0, NA_STRING);
                            SET_VECTOR_ELT(result, i, child);
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
                            prefix_exdent, false
                        );
                        char* destination = flat_output.append_reserve(
                            staged.size
                        );
                        engine.write_joined(
                            destination,
                            i == 0 ? initial_indent : prefix_indent,
                            prefix_exdent
                        );
                        continue;
                    }

                    const int line_count = engine.line_count();
                    if (!flatten) {
                        child = callback_protections.reprotect_slot(
                            Rf_allocVector(STRSXP, line_count), child_index
                        );
                    }

                    for (int line = 0; line < line_count; ++line) {
                        const shared::wrap::Line staged = engine.line(
                            line,
                            i == 0 ? initial_indent : prefix_indent,
                            prefix_exdent, false
                        );
                        if (flatten) {
                            char* destination = flat_output.append_reserve(
                                staged.size
                            );
                            shared::wrap::Engine::write_line(
                                staged, destination
                            );
                            continue;
                        }

                        line_buffer.resize(staged.size);
                        char* destination = staged.size == 0
                            ? nullptr
                            : line_buffer.data();
                        shared::wrap::Engine::write_line(
                            staged, destination
                        );
                        SET_STRING_ELT(
                            child, line,
                            Rf_mkCharLenCE(
                                staged.size == 0 ? "" : line_buffer.data(),
                                static_cast<int>(staged.size), CE_UTF8
                            )
                        );
                    }
                    if (!flatten)
                        SET_VECTOR_ELT(result, i, child);
                }

                if (flatten || join) {
                    result = entry_protections.reprotect_one(
                        Rf_allocVector(STRSXP, flat_output.size()),
                        result_index
                    );
                    write_flat_output_r(result, flat_output);
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_locale_warning_r(root_fallback_warning);
    );
}

} } // namespace charr::base_backend
