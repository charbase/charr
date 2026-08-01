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
#include "ci_builder.h"
#include "io/reader_utils.h"
#include "io/utf8_output.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/substring.h"
#include "../shared/unwind.h"
#include <cstring>
#include <exception>
#include <stdexcept>
#include <vector>

namespace charr { namespace altrep_backend {


namespace sub {

CHARR_R_HELPER R_len_t recycling_length_r(
    const int* lengths, int count
) noexcept
{
    bool needs_warning = false;
    const R_len_t result = shared::substring::recycling_length(
        lengths, count, needs_warning
    );
    if (needs_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);
    return result;
}


CHARR_R_HELPER R_len_t recycling_length_r(
    R_len_t first, R_len_t second
) noexcept
{
    const int lengths[] = {first, second};
    return recycling_length_r(lengths, 2);
}


CHARR_R_HELPER R_len_t recycling_length_r(
    R_len_t first, R_len_t second, R_len_t third
) noexcept
{
    const int lengths[] = {first, second, third};
    return recycling_length_r(lengths, 3);
}


CHARR_R_HELPER R_len_t recycling_length_r(
    R_len_t first, R_len_t second, R_len_t third, R_len_t fourth
) noexcept
{
    const int lengths[] = {first, second, third, fourth};
    return recycling_length_r(lengths, 4);
}

struct CiSubFrameInput {
    const char* data;
    R_len_t length;
    bool is_na;
    bool is_ascii;
    bool converted;
};


CHARR_CXX_HELPER CiSubFrameInput ci__sub_normalize_frame_input(
    const charport::StrView& value,
    shared::NativeToUtf8& converter
)
{
    if (value.is_na())
        return CiSubFrameInput{nullptr, 0, true, false, false};
    if (value.ptr == nullptr || value.len < 0)
        throw std::runtime_error("Reader returned an invalid string view");

    const char* data = value.ptr;
    R_len_t length = value.len;
    if (value.enc == cetype_ext_t::CE_ASCII) {
        return CiSubFrameInput{data, length, false, true, false};
    }
    if (value.enc == cetype_ext_t::CE_BYTES)
        throw StriException(MSG__BYTESENC);

    if (value.enc == cetype_ext_t::CE_UTF8 ||
            value.enc == cetype_ext_t::CE_ASCII_OR_UTF8) {
        const bool ascii = value.enc == cetype_ext_t::CE_ASCII_OR_UTF8 &&
            io::is_ascii(data, static_cast<std::size_t>(length));
        if (!ascii && STRI__ENC_HAS_BOM_UTF8(data, length)) {
            data += 3;
            length -= 3;
        }
        return CiSubFrameInput{data, length, false, ascii, false};
    }

    shared::ByteView output;
    if (value.enc == cetype_ext_t::CE_LATIN1) {
        output = converter.latin1(data, length);
    }
    else if (value.enc == cetype_ext_t::CE_NATIVE) {
        const bool native_has_bom = STRI__ENC_HAS_BOM_UTF8(data, length);
        output = converter.native(data, length);
        if (native_has_bom &&
                STRI__ENC_HAS_BOM_UTF8(output.ptr, output.len)) {
            return CiSubFrameInput{
                output.ptr+3, output.len-3, false, false, true
            };
        }
    }
    else {
        throw std::runtime_error(
            "Reader returned an unknown string encoding"
        );
    }
    if (output.len == 0) {
        return CiSubFrameInput{"", 0, false, false, true};
    }
    return CiSubFrameInput{
        output.ptr, output.len, false, false, true
    };
}


CHARR_CXX_HELPER CiSubFrameInput ci__sub_stabilize_frame_input(
    const CiSubFrameInput& input, shared::SliceArena& storage
)
{
    if (!input.converted || input.length <= 0)
        return input;
    char* output = storage.allocate(static_cast<std::size_t>(input.length));
    std::memcpy(output, input.data, static_cast<std::size_t>(input.length));
    return CiSubFrameInput{
        output, input.length, false, input.is_ascii, false
    };
}


} // namespace sub

using namespace sub;

CHARR_R_HELPER bool ci__sub_matrix_has_too_many_columns_r(
    SEXP value, bool use_matrix
) noexcept
{
    if (!use_matrix || !Rf_isMatrix(value))
        return false;

    SEXP dimensions = PROTECT(Rf_getAttrib(value, R_DimSymbol));
    const bool output = INTEGER(dimensions)[1] > 2;
    UNPROTECT(1);
    return output;
}


CHARR_R_HELPER void ci__sub_emit_replacement_warnings_r(
    const std::vector<shared::substring::ReplacementWarning>& warnings
) noexcept
{
    for (std::size_t i = 0; i < warnings.size(); ++i) {
        if (warnings[i] ==
                shared::substring::ReplacementWarning::replacement_zero) {
            Rf_warning("%s", MSG__REPLACEMENT_ZERO);
        }
        else if (warnings[i] ==
                shared::substring::ReplacementWarning::recycling) {
            Rf_warning("%s", MSG__WARN_RECYCLING_RULE2);
        }
        else {
            Rf_warning("%s", MSG__WARN_RECYCLING_RULE);
        }
    }
}


CHARR_R_HELPER R_len_t ci__sub_prepare_from_to_length_r(
    SEXP& from, SEXP& to, SEXP& length,
    R_len_t& from_len, R_len_t& to_len, R_len_t& length_len,
    int*& from_tab, int*& to_tab, int*& length_tab,
    bool use_matrix_1
) noexcept
{
    R_len_t protected_count = 0;
    bool from_is_matrix = use_matrix_1 && Rf_isMatrix(from);
    if (from_is_matrix) {
        SEXP dimensions = PROTECT(Rf_getAttrib(from, R_DimSymbol));
        if (INTEGER(dimensions)[1] == 1) {
            from_is_matrix = false;
        }
        else if (INTEGER(dimensions)[1] > 2) {
            UNPROTECT(1);
            Rf_error(
                MSG__ARG_EXPECTED_MATRIX_WITH_GIVEN_COLUMNS, "from", 2
            );
        }
        UNPROTECT(1);
    }

    PROTECT(from = ci__prepare_arg_integer_r(from, "from"));
    ++protected_count;

    if (from_is_matrix) {
        bool from_length_matrix = false;
        SEXP dimnames = PROTECT(Rf_getAttrib(from, R_DimNamesSymbol));
        if (!Rf_isNull(dimnames)) {
            SEXP column_names = PROTECT(VECTOR_ELT(dimnames, 1));
            if (Rf_isString(column_names) && LENGTH(column_names) == 2 &&
                    std::strcmp(
                        "length", CHAR(STRING_ELT(column_names, 1))
                    ) == 0) {
                from_length_matrix = true;
            }
            UNPROTECT(1);
        }
        UNPROTECT(1);

        from_len = LENGTH(from)/2;
        from_tab = INTEGER(from);
        if (from_length_matrix) {
            length_len = from_len;
            length_tab = from_tab+from_len;
        }
        else {
            to_len = from_len;
            to_tab = from_tab+from_len;
        }
    }
    else if (Rf_isNull(length)) {
        PROTECT(to = ci__prepare_arg_integer_r(to, "to"));
        ++protected_count;
        from_len = LENGTH(from);
        from_tab = INTEGER(from);
        to_len = LENGTH(to);
        to_tab = INTEGER(to);
    }
    else {
        PROTECT(length = ci__prepare_arg_integer_r(length, "length"));
        ++protected_count;
        from_len = LENGTH(from);
        from_tab = INTEGER(from);
        length_len = LENGTH(length);
        length_tab = INTEGER(length);
    }
    return protected_count;
}


/**
 * Get substring
 *
 *
 * @param str character vector
 * @param from integer vector (possibly with negative indices)
 * @param to integer vector (possibly with negative indices) or NULL
 * @param length integer vector or NULL
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *    ci_sub
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *    Make StriException-friendly
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-03)
 *    Use ci__sub_prepare_from_to_length()
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.5-9003 (Marek Gagolewski, 2015-08-05)
 *    Bugfix #183: floating point exception when to or length is an empty vector
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-28)
 *    Negative length yields NA
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-07-08)
 *    use_matrix, ignore_negative_length
 */
CHARR_ENTRYPOINT SEXP ci_sub(
    SEXP str, SEXP from, SEXP to, SEXP length,
    SEXP use_matrix, SEXP ignore_negative_length
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    const bool use_matrix_1 = ci__prepare_arg_logical_1_notNA_r(
        use_matrix, "use_matrix"
    );
    const bool ignore_negative_length_1 =
        ci__prepare_arg_logical_1_notNA_r(
            ignore_negative_length, "ignore_negative_length"
        );

    R_len_t from_len      = 0;
    R_len_t to_len        = 0;
    R_len_t length_len    = 0;
    int* from_tab         = 0;
    int* to_tab           = 0;
    int* length_tab       = 0;
    const R_xlen_t source_size = XLENGTH(str);
    R_len_t str_len = 0;
    R_len_t vectorize_len = 0;
    bool scalar_bounds = false;

    try {
        charport::Reader reader;
        charport::StrViews views;
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        shared::substring::Utf8Indexer indexer;
        std::vector<CiSubFrameInput> inputs;
        io::OutputBuilder builder(0);
        io::OutputBuilder filtered(0);
        io::OutputStore output_store(0, 0);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t bounds_protected =
                    ci__sub_prepare_from_to_length_r(
                        from, to, length,
                        from_len, to_len, length_len,
                        from_tab, to_tab, length_tab, use_matrix_1
                    );
                callback_protections.adopt(bounds_protected);
                if (source_size < 0 || source_size > R_LEN_T_MAX) {
                    Rf_error("long character vectors are not supported");
                }
                str_len = static_cast<R_len_t>(source_size);
                const R_len_t endpoint_len = to_len > length_len
                    ? to_len : length_len;
                vectorize_len = recycling_length_r(
                    str_len, from_len, endpoint_len
                );
                scalar_bounds = vectorize_len > 0 && !length_tab &&
                    to_tab && from_len == 1 && to_len == 1 &&
                    from_tab[0] > 0 && to_tab[0] > 0;
                if (!scalar_bounds && vectorize_len > 0) {
                    inputs.resize(static_cast<std::size_t>(str_len));
                }

                if (vectorize_len > 0) {
                    reader.reset(str);
                    if (reader.size() != source_size) {
                        throw std::runtime_error(
                            "character vector length changed during an operation"
                        );
                    }
                    views.resize(source_size);
                    reader.views(
                        0, source_size,
                        views.ptrs(), views.lengths(), views.encodings()
                    );
                }

                if (!scalar_bounds && vectorize_len > 0) {
                    for (R_len_t i = 0; i < str_len; ++i) {
                        inputs[static_cast<std::size_t>(i)] =
                            ci__sub_stabilize_frame_input(
                                ci__sub_normalize_frame_input(
                                    views[i], converter
                                ),
                                storage
                            );
                    }
                }

                builder.reset(vectorize_len);
                R_len_t negative_lengths = 0;
                for (R_len_t i = 0; i < vectorize_len; ++i) {
                    CiSubFrameInput value;
                    if (scalar_bounds) {
                        value = ci__sub_normalize_frame_input(
                            views[i], converter
                        );
                    }
                    else {
                        value = inputs[
                            static_cast<std::size_t>(i % str_len)
                        ];
                    }

                    R_len_t current_from = from_tab[i % from_len];
                    R_len_t current_to = to_tab
                        ? to_tab[i % to_len]
                        : length_tab[i % length_len];
                    if (value.is_na || current_from == NA_INTEGER ||
                            current_to == NA_INTEGER) {
                        builder.set_na(i);
                        continue;
                    }
                    if (length_tab) {
                        if (current_to == 0) {
                            builder.set(
                                i, "", 0, cetype_ext_t::CE_ASCII
                            );
                            continue;
                        }
                        if (current_to < 0) {
                            builder.set_na(i);
                            ++negative_lengths;
                            continue;
                        }
                        current_to = shared::substring::length_endpoint(
                            current_from, current_to
                        );
                    }

                    indexer.reset(
                        value.data, value.length, value.is_ascii
                    );
                    const shared::substring::ByteRange range =
                        indexer.range(current_from, current_to);
                    if (range.end > range.begin) {
                        builder.set_validated(
                            i,
                            charport::StrView{
                                value.data+range.begin,
                                range.end-range.begin,
                                value.is_ascii
                                    ? cetype_ext_t::CE_ASCII
                                    : cetype_ext_t::CE_ASCII_OR_UTF8
                            }
                        );
                    }
                    else {
                        builder.set(
                            i, "", 0, cetype_ext_t::CE_ASCII
                        );
                    }
                }

                output_store = builder.release_store();
                if (negative_lengths > 0 &&
                        ignore_negative_length_1) {
                    filtered.reset(vectorize_len-negative_lengths);
                    R_len_t output = 0;
                    for (R_len_t i =
                            shared::substring::recycled_order_begin(
                                str_len, vectorize_len
                            );
                            i < vectorize_len;
                            i = shared::substring::recycled_order_next(
                                i, str_len, vectorize_len
                            )) {
                        const CiSubFrameInput& value = inputs[
                            static_cast<std::size_t>(i % str_len)
                        ];
                        const R_len_t current_from = from_tab[i % from_len];
                        const R_len_t current_length =
                            length_tab[i % length_len];
                        if (!value.is_na && current_from != NA_INTEGER &&
                                current_length != NA_INTEGER &&
                                current_length < 0) {
                            continue;
                        }
                        filtered.set_validated(
                            output++, output_store.view(
                                static_cast<std::size_t>(i)
                            )
                        );
                    }
                    output_store = filtered.release_store();
                }

                result = entry_protections.reprotect_one(
                    io::finalize(std::move(output_store)), result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}


/**
 * Substring replacement function
 *
 *
 * @param str character vector
 * @param from integer vector (possibly with negative indices)
 * @param to integer vector (possibly with negative indices) or NULL
 * @param length integer vector or NULL
 * @param omit_na logical scalar
 * @param value character vector replacement
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException-friendly
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-03)
 *          Use ci__sub_prepare_from_to_length()
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.5-9003 (Marek Gagolewski, 2015-08-05)
 *    Bugfix #183: floating point exception when to or length is an empty vector
 *
 * @version 1.0-2 (Marek Gagolewski, 2016-01-31)
 *    FR #199: new arg: `omit_na`
 *    FR #207: allow insertions
 *
 *
 * @version 1.4.3 (Marek Gagolewski, 2019-03-12)
 *    #346: na_omit for `value`
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-28)
 *    negative length does not alter input
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-07-08)
 *    use_matrix
 */
CHARR_ENTRYPOINT SEXP ci_sub_replacement(
    SEXP str, SEXP from, SEXP to, SEXP length,
    SEXP omit_na, SEXP value, SEXP use_matrix
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    value = entry_protections.protect_one(
        ci__prepare_arg_string_r(value, "value")
    );
    const bool omit_na_1 = ci__prepare_arg_logical_1_notNA_r(
        omit_na, "omit_na"
    );
    const bool use_matrix_1 = ci__prepare_arg_logical_1_notNA_r(
        use_matrix, "use_matrix"
    );

    R_len_t from_len      = 0; // see below
    R_len_t to_len        = 0; // see below
    R_len_t length_len    = 0; // see below
    int* from_tab         = 0; // see below
    int* to_tab           = 0; // see below
    int* length_tab       = 0; // see below
    const R_xlen_t replacement_size = XLENGTH(value);
    const R_xlen_t source_size = XLENGTH(str);
    R_len_t value_len = 0;
    R_len_t str_len = 0;
    R_len_t vectorize_len = 0;
    bool scalar_bounds = false;

    try {
        charport::Reader source_reader;
        charport::Reader replacement_reader;
        charport::StrViews source_views;
        charport::StrViews replacement_views;
        shared::NativeToUtf8 source_converter;
        shared::NativeToUtf8 replacement_converter;
        shared::SliceArena source_storage;
        shared::SliceArena replacement_storage;
        shared::substring::Utf8Indexer indexer;
        std::vector<CiSubFrameInput> sources;
        std::vector<CiSubFrameInput> replacements;
        io::OutputBuilder builder(0);
        io::OutputStore output_store(0, 0);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t bounds_protected =
                    ci__sub_prepare_from_to_length_r(
                        from, to, length,
                        from_len, to_len, length_len,
                        from_tab, to_tab, length_tab, use_matrix_1
                    );
                callback_protections.adopt(bounds_protected);
                if (replacement_size < 0 ||
                        replacement_size > R_LEN_T_MAX) {
                    Rf_error("long character vectors are not supported");
                }
                if (source_size < 0 || source_size > R_LEN_T_MAX) {
                    Rf_error("long character vectors are not supported");
                }
                value_len = static_cast<R_len_t>(replacement_size);
                str_len = static_cast<R_len_t>(source_size);
                const R_len_t endpoint_len = to_len > length_len
                    ? to_len : length_len;
                vectorize_len = recycling_length_r(
                    str_len, value_len, from_len, endpoint_len
                );
                scalar_bounds = vectorize_len > 0 && !length_tab &&
                    to_tab && from_len == 1 && to_len == 1 &&
                    value_len == 1 && from_tab[0] > 0 && to_tab[0] > 0;
                if (!scalar_bounds && vectorize_len > 0) {
                    sources.resize(static_cast<std::size_t>(str_len));
                    replacements.resize(
                        static_cast<std::size_t>(value_len)
                    );
                }

                if (vectorize_len > 0) {
                    source_reader.reset(str);
                    if (source_reader.size() != source_size) {
                        throw std::runtime_error(
                            "character vector length changed during an operation"
                        );
                    }
                    source_views.resize(source_size);
                    source_reader.views(
                        0, source_size,
                        source_views.ptrs(), source_views.lengths(),
                        source_views.encodings()
                    );

                    replacement_reader.reset(value);
                    if (replacement_reader.size() != replacement_size) {
                        throw std::runtime_error(
                            "character vector length changed during an operation"
                        );
                    }
                    replacement_views.resize(replacement_size);
                    replacement_reader.views(
                        0, replacement_size,
                        replacement_views.ptrs(),
                        replacement_views.lengths(),
                        replacement_views.encodings()
                    );
                }

                CiSubFrameInput scalar_replacement;
                if (scalar_bounds) {
                    scalar_replacement = ci__sub_normalize_frame_input(
                        replacement_views[0], replacement_converter
                    );
                }
                else if (vectorize_len > 0) {
                    for (R_len_t i = 0; i < str_len; ++i) {
                        sources[static_cast<std::size_t>(i)] =
                            ci__sub_stabilize_frame_input(
                                ci__sub_normalize_frame_input(
                                    source_views[i], source_converter
                                ),
                                source_storage
                            );
                    }
                    for (R_len_t i = 0; i < value_len; ++i) {
                        replacements[static_cast<std::size_t>(i)] =
                            ci__sub_stabilize_frame_input(
                                ci__sub_normalize_frame_input(
                                    replacement_views[i],
                                    replacement_converter
                                ),
                                replacement_storage
                            );
                    }
                }

                builder.reset(vectorize_len);
                for (R_len_t i = 0; i < vectorize_len; ++i) {
                    CiSubFrameInput source;
                    CiSubFrameInput replacement;
                    if (scalar_bounds) {
                        source = ci__sub_normalize_frame_input(
                            source_views[i], source_converter
                        );
                        replacement = scalar_replacement;
                    }
                    else {
                        source = sources[
                            static_cast<std::size_t>(i % str_len)
                        ];
                        replacement = replacements[
                            static_cast<std::size_t>(i % value_len)
                        ];
                    }

                    R_len_t current_from = from_tab[i % from_len];
                    R_len_t current_to = to_tab
                        ? to_tab[i % to_len]
                        : length_tab[i % length_len];
                    if (source.is_na) {
                        builder.set_na(i);
                        continue;
                    }
                    if (current_from == NA_INTEGER ||
                            current_to == NA_INTEGER ||
                            replacement.is_na) {
                        if (omit_na_1) {
                            builder.set_validated(
                                i,
                                charport::StrView{
                                    source.length == 0 ? "" : source.data,
                                    source.length,
                                    source.is_ascii
                                        ? cetype_ext_t::CE_ASCII
                                        : cetype_ext_t::CE_UTF8
                                }
                            );
                        }
                        else {
                            builder.set_na(i);
                        }
                        continue;
                    }
                    if (!to_tab && current_to < 0) {
                        builder.set_validated(
                            i,
                            charport::StrView{
                                source.length == 0 ? "" : source.data,
                                source.length,
                                source.is_ascii
                                    ? cetype_ext_t::CE_ASCII
                                    : cetype_ext_t::CE_UTF8
                            }
                        );
                        continue;
                    }
                    if (length_tab) {
                        current_to = current_to <= 0
                            ? 0
                            : shared::substring::length_endpoint(
                                current_from, current_to
                            );
                    }

                    indexer.reset(
                        source.data, source.length, source.is_ascii
                    );
                    shared::substring::ByteRange range =
                        indexer.range(current_from, current_to);
                    if (range.end < range.begin)
                        range.end = range.begin;

                    const std::size_t prefix =
                        static_cast<std::size_t>(range.begin);
                    const std::size_t replacement_length =
                        static_cast<std::size_t>(replacement.length);
                    const std::size_t suffix = static_cast<std::size_t>(
                        source.length-range.end
                    );
                    std::size_t output_size =
                        shared::substring::checked_output_size(
                            prefix, replacement_length
                        );
                    output_size = shared::substring::checked_output_size(
                        output_size, suffix
                    );
                    const bool output_ascii =
                        (source.is_ascii ||
                         io::is_ascii(source.data, prefix)) &&
                        (replacement.is_ascii ||
                         io::is_ascii(
                             replacement.data, replacement_length
                         )) &&
                        (source.is_ascii ||
                         io::is_ascii(source.data+range.end, suffix));
                    char* output = builder.reserve(
                        i, output_size,
                        output_ascii
                            ? cetype_ext_t::CE_ASCII
                            : cetype_ext_t::CE_UTF8
                    );
                    if (prefix > 0)
                        std::memcpy(output, source.data, prefix);
                    if (replacement_length > 0) {
                        std::memcpy(
                            output+prefix, replacement.data,
                            replacement_length
                        );
                    }
                    if (suffix > 0) {
                        std::memcpy(
                            output+prefix+replacement_length,
                            source.data+range.end, suffix
                        );
                    }
                }

                output_store = builder.release_store();
                result = entry_protections.reprotect_one(
                    io::finalize(std::move(output_store)), result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}



CHARR_R_HELPER bool ci__sub_all_plain_integer_scalar(
    SEXP value, int& output
) noexcept
{
    if (TYPEOF(value) != INTSXP || Rf_isObject(value) || ALTREP(value) ||
            !NO_ATTRIB(value) || XLENGTH(value) != 1) {
        return false;
    }
    output = INTEGER_RO(value)[0];
    return true;
}


CHARR_R_HELPER bool ci__sub_all_plain_list_scalar(
    SEXP values, R_len_t values_len, int& output
) noexcept
{
    return TYPEOF(values) == VECSXP && !Rf_isObject(values) &&
        !ALTREP(values) && NO_ATTRIB(values) && values_len == 1 &&
        ci__sub_all_plain_integer_scalar(VECTOR_ELT(values, 0), output);
}



/**
 * Extract multiple substrings
 *
 *
 * @param str character vector
 * @param from list
 * @param to list
 * @param length list
 * @return list of character vectors
 *
 * @version 1.3.2 (Marek Gagolewski, 2019-02-21)
 *    #30: new function
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-28)
 *    negative length yields NA
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-07-08)
 *    use_matrix, ignore_negative_length
 */
CHARR_ENTRYPOINT SEXP ci_sub_all(
    SEXP str, SEXP from, SEXP to, SEXP length,
    SEXP use_matrix, SEXP ignore_negative_length
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    from = entry_protections.protect_one(
        ci__prepare_arg_list_r(from, "from")
    );
    to = entry_protections.protect_one(
        ci__prepare_arg_list_r(to, "to")
    );
    length = entry_protections.protect_one(
        ci__prepare_arg_list_r(length, "length")
    );
    const bool use_matrix_1 = ci__prepare_arg_logical_1_notNA_r(
        use_matrix, "use_matrix"
    );
    const bool ignore_negative_length_1 =
        ci__prepare_arg_logical_1_notNA_r(
            ignore_negative_length, "ignore_negative_length"
    );

    const R_xlen_t source_size = XLENGTH(str);
    R_len_t str_len = 0;
    const R_len_t from_list_len = LENGTH(from);
    const R_len_t to_list_len = LENGTH(to);
    const R_len_t length_list_len = LENGTH(length);
    const bool has_to = !Rf_isNull(to);
    const bool has_length = !Rf_isNull(length);
    R_len_t vectorize_len = 0;
    int scalar_from = 0;
    int scalar_to = 0;
    bool scalar_bounds = false;

    try {
        charport::Reader source_reader;
        charport::StrViews source_views;
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        shared::substring::Utf8Indexer indexer;
        std::vector<CiSubFrameInput> sources;
        std::vector<unsigned char> source_ready;
        io::OutputBuilder builder(0);
        std::vector<io::OutputStore> stores;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                if (source_size < 0 || source_size > R_LEN_T_MAX) {
                    Rf_error("long character vectors are not supported");
                }
                str_len = static_cast<R_len_t>(source_size);
                vectorize_len = has_to
                    ? recycling_length_r(
                        str_len, from_list_len, to_list_len
                    )
                    : has_length
                        ? recycling_length_r(
                            str_len, from_list_len,
                            length_list_len
                        )
                        : recycling_length_r(
                            str_len, from_list_len
                        );
                scalar_bounds = has_to && !has_length &&
                    ci__sub_all_plain_list_scalar(
                        from, from_list_len, scalar_from
                    ) &&
                    ci__sub_all_plain_list_scalar(
                        to, to_list_len, scalar_to
                    ) && scalar_from > 0 && scalar_to > 0;
                if (vectorize_len > 0) {
                    sources.resize(static_cast<std::size_t>(str_len));
                    source_ready.assign(
                        static_cast<std::size_t>(str_len), 0
                    );
                    stores.reserve(
                        static_cast<std::size_t>(vectorize_len)
                    );
                    source_reader.reset(str);
                    if (source_reader.size() != source_size) {
                        throw std::runtime_error(
                            "character vector length changed during an operation"
                        );
                    }
                    source_views.resize(source_size);
                    source_reader.views(
                        0, source_size,
                        source_views.ptrs(), source_views.lengths(),
                        source_views.encodings()
                    );
                }
                result = entry_protections.reprotect_one(
                    Rf_allocVector(VECSXP, vectorize_len), result_index
                );

                for (R_len_t outer = 0; outer < vectorize_len; ++outer) {
                    SEXP inner_from = R_NilValue;
                    SEXP inner_to = R_NilValue;
                    SEXP inner_length = R_NilValue;
                    R_len_t inner_from_len = 0;
                    R_len_t inner_to_len = 0;
                    R_len_t inner_length_len = 0;
                    int* inner_from_tab = nullptr;
                    int* inner_to_tab = nullptr;
                    int* inner_length_tab = nullptr;
                    R_len_t inner_protected = 0;

                    if (scalar_bounds) {
                        inner_from_len = 1;
                        inner_to_len = 1;
                        inner_from_tab = &scalar_from;
                        inner_to_tab = &scalar_to;
                    }
                    else {
                        inner_from = VECTOR_ELT(
                            from, outer % from_list_len
                        );
                        if (has_to) {
                            inner_to = VECTOR_ELT(
                                to, outer % to_list_len
                            );
                        }
                        else if (has_length) {
                            inner_length = VECTOR_ELT(
                                length, outer % length_list_len
                            );
                        }
                        inner_protected =
                            ci__sub_prepare_from_to_length_r(
                                inner_from, inner_to, inner_length,
                                inner_from_len, inner_to_len,
                                inner_length_len,
                                inner_from_tab, inner_to_tab,
                                inner_length_tab, use_matrix_1
                            );
                        callback_protections.adopt(inner_protected);
                    }

                    const R_len_t inner_endpoint_len =
                        inner_to_len > inner_length_len
                            ? inner_to_len : inner_length_len;
                    const R_len_t inner_vectorize_len =
                        recycling_length_r(
                            1, inner_from_len,
                            inner_endpoint_len
                        );
                    if (inner_vectorize_len <= 0) {
                        builder.reset(0);
                        stores.push_back(builder.release_store());
                        callback_protections.release(inner_protected);
                        continue;
                    }
                    const R_len_t source_index = outer % str_len;
                    if (!source_ready[
                            static_cast<std::size_t>(source_index)
                        ]) {
                        sources[static_cast<std::size_t>(source_index)] =
                            ci__sub_stabilize_frame_input(
                                ci__sub_normalize_frame_input(
                                    source_views[source_index], converter
                                ),
                                storage
                            );
                        source_ready[
                            static_cast<std::size_t>(source_index)
                        ] = 1;
                    }
                    const CiSubFrameInput& source = sources[
                        static_cast<std::size_t>(source_index)
                    ];

                    R_len_t negative_lengths = 0;
                    if (inner_length_tab && !source.is_na) {
                        for (R_len_t i = 0;
                                i < inner_vectorize_len; ++i) {
                            const R_len_t current_from =
                                inner_from_tab[i % inner_from_len];
                            const R_len_t current_length =
                                inner_length_tab[i % inner_length_len];
                            if (current_from != NA_INTEGER &&
                                    current_length != NA_INTEGER &&
                                    current_length < 0) {
                                ++negative_lengths;
                            }
                        }
                    }
                    const R_len_t output_len =
                        ignore_negative_length_1
                            ? inner_vectorize_len-negative_lengths
                            : inner_vectorize_len;
                    builder.reset(output_len);
                    R_len_t output = 0;
                    for (R_len_t i = 0;
                            i < inner_vectorize_len; ++i) {
                        R_len_t current_from =
                            inner_from_tab[i % inner_from_len];
                        R_len_t current_to = inner_to_tab
                            ? inner_to_tab[i % inner_to_len]
                            : inner_length_tab[i % inner_length_len];
                        if (ignore_negative_length_1 && !source.is_na &&
                                current_from != NA_INTEGER &&
                                current_to != NA_INTEGER &&
                                inner_length_tab && current_to < 0) {
                            continue;
                        }
                        if (source.is_na ||
                                current_from == NA_INTEGER ||
                                current_to == NA_INTEGER) {
                            builder.set_na(output++);
                            continue;
                        }
                        if (inner_length_tab) {
                            if (current_to == 0) {
                                builder.set(
                                    output++, "", 0,
                                    cetype_ext_t::CE_ASCII
                                );
                                continue;
                            }
                            if (current_to < 0) {
                                builder.set_na(output++);
                                continue;
                            }
                            current_to =
                                shared::substring::length_endpoint(
                                    current_from, current_to
                                );
                        }
                        indexer.reset(
                            source.data, source.length, source.is_ascii
                        );
                        const shared::substring::ByteRange range =
                            indexer.range(current_from, current_to);
                        if (range.end > range.begin) {
                            builder.set_validated(
                                output++,
                                charport::StrView{
                                    source.data+range.begin,
                                    range.end-range.begin,
                                    source.is_ascii
                                        ? cetype_ext_t::CE_ASCII
                                        : cetype_ext_t::CE_ASCII_OR_UTF8
                                }
                            );
                        }
                        else {
                            builder.set(
                                output++, "", 0,
                                cetype_ext_t::CE_ASCII
                            );
                        }
                    }
                    stores.push_back(builder.release_store());
                    callback_protections.release(inner_protected);
                }

                for (R_len_t outer = 0; outer < vectorize_len; ++outer) {
                    SEXP inner = callback_protections.protect_one(
                        io::finalize(std::move(
                            stores[static_cast<std::size_t>(outer)]
                        ))
                    );
                    SET_VECTOR_ELT(result, outer, inner);
                    callback_protections.release(1);
                }
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}
/** internal function - replace multiple substrings in a single string
 *
 *  @version 1.3.2 (Marek Gagolewski, 2019-02-23)
 *
 * @version 1.4.3 (Marek Gagolewski, 2019-03-12)
 *    #346: na_omit for `value`
 *
 * @version 1.4.4 (Marek Gagolewski, 2019-03-13)-
 *    #348: UBSAN runtime error: null pointer passed as argument 1,
 *     which is declared to never be null
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-28)
 *    negative length does not alter input
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-07-08)
 *    use_matrix
 */
/**
 * Replace multiple substrings
 *
 *
 * @param str character vector
 * @param from integer vector (possibly with negative indices)
 * @param to integer vector (possibly with negative indices) or NULL
 * @param length integer vector or NULL
 * @param omit_na logical scalar
 * @param value character vector replacement
 * @return character vector
 *
 * @version 1.3.2 (Marek Gagolewski, 2019-02-22)
 *    #30: new function
 *
 *
 * @version 1.4.3 (Marek Gagolewski, 2019-03-12)
 *    #346: na_omit for `value`
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-28)
 *    negative length does not alter input
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-07-08)
 *    use_matrix
 */
CHARR_ENTRYPOINT SEXP ci_sub_replacement_all(
    SEXP str, SEXP from, SEXP to, SEXP length,
    SEXP omit_na, SEXP value, SEXP use_matrix
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    const R_xlen_t source_size = XLENGTH(str);
    R_len_t str_len = 0;
    int scalar_from = 0;
    int scalar_to = 0;
    SEXP scalar_value = R_NilValue;
    bool scalar_fast_path = false;
    bool scalar_replacement_ready = false;

    try {
        charport::Reader source_reader;
        charport::Reader replacement_reader;
        charport::StrViews source_views;
        charport::StrViews replacement_views;
        shared::NativeToUtf8 source_converter;
        shared::NativeToUtf8 replacement_converter;
        shared::SliceArena source_storage;
        shared::SliceArena replacement_storage;
        shared::substring::ReplacementAssembler assembler;
        shared::substring::Utf8Indexer scalar_indexer;
        CiSubFrameInput scalar_replacement_input{
            nullptr, 0, true, false
        };
        std::vector<CiSubFrameInput> sources;
        std::vector<shared::StringView> replacements;
        std::vector<shared::substring::ReplacementWarning>
            pending_warnings;
        io::OutputBuilder builder(0);
        io::OutputStore output_store(0, 0);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                if (source_size < 0 || source_size > R_LEN_T_MAX) {
                    Rf_error("long character vectors are not supported");
                }
                str_len = static_cast<R_len_t>(source_size);
                sources.resize(static_cast<std::size_t>(str_len));
                source_reader.reset(str);
                if (source_reader.size() != source_size) {
                    throw std::runtime_error(
                        "character vector length changed during an operation"
                    );
                }
                source_views.resize(source_size);
                source_reader.views(
                    0, source_size,
                    source_views.ptrs(), source_views.lengths(),
                    source_views.encodings()
                );
                for (R_len_t i = 0; i < str_len; ++i) {
                    sources[static_cast<std::size_t>(i)] =
                        ci__sub_stabilize_frame_input(
                            ci__sub_normalize_frame_input(
                                source_views[i], source_converter
                            ),
                            source_storage
                        );
                }

                from = callback_protections.protect_one(
                    ci__prepare_arg_list_r(from, "from")
                );
                to = callback_protections.protect_one(
                    ci__prepare_arg_list_r(to, "to")
                );
                length = callback_protections.protect_one(
                    ci__prepare_arg_list_r(length, "length")
                );
                value = callback_protections.protect_one(
                    ci__prepare_arg_list_r(value, "value")
                );
                const bool omit_na_1 =
                    ci__prepare_arg_logical_1_notNA_r(
                        omit_na, "omit_na"
                    );
                const bool use_matrix_1 =
                    ci__prepare_arg_logical_1_notNA_r(
                        use_matrix, "use_matrix"
                    );

                const R_len_t from_list_len = LENGTH(from);
                const R_len_t to_list_len = LENGTH(to);
                const R_len_t length_list_len = LENGTH(length);
                const R_len_t value_list_len = LENGTH(value);
                const bool has_to = !Rf_isNull(to);
                const bool has_length = !Rf_isNull(length);
                const int outer_lengths[4] = {
                    str_len, from_list_len, value_list_len,
                    has_to ? to_list_len
                        : has_length ? length_list_len : 1
                };
                bool outer_recycling_warning = false;
                const R_len_t vectorize_len =
                    shared::substring::recycling_length(
                        outer_lengths,
                        has_to || has_length ? 4 : 3,
                        outer_recycling_warning
                    );
                const bool scalar_bounds = vectorize_len > 0 &&
                    has_to && !has_length &&
                    ci__sub_all_plain_list_scalar(
                        from, from_list_len, scalar_from
                    ) &&
                    ci__sub_all_plain_list_scalar(
                        to, to_list_len, scalar_to
                    ) && scalar_from > 0 && scalar_to > 0;
                const bool scalar_replacement = value_list_len == 1 &&
                    TYPEOF(value) == VECSXP && !Rf_isObject(value) &&
                    !ALTREP(value) && NO_ATTRIB(value) &&
                    ((scalar_value = VECTOR_ELT(value, 0)),
                     TYPEOF(scalar_value) == STRSXP &&
                     !Rf_isObject(scalar_value) &&
                     !ALTREP(scalar_value) && NO_ATTRIB(scalar_value) &&
                     XLENGTH(scalar_value) == 1);
                scalar_fast_path = scalar_bounds && scalar_replacement;
                if (outer_recycling_warning) {
                    pending_warnings.push_back(
                        shared::substring::ReplacementWarning::
                            recycling_rule
                    );
                }

                try {
                  builder.reset(vectorize_len);
                  for (R_len_t outer = 0;
                          outer < vectorize_len; ++outer) {
                    const CiSubFrameInput& source = sources[
                        static_cast<std::size_t>(outer % str_len)
                    ];
                    if (source.is_na) {
                        builder.set_na(outer);
                        continue;
                    }

                    R_len_t replacement_len = 0;
                    R_len_t replacement_protected = 0;
                    if (!scalar_fast_path ||
                            !scalar_replacement_ready) {
                        SEXP inner_value = callback_protections.protect_one(
                            ci__prepare_arg_string_r(
                                VECTOR_ELT(
                                    value, outer % value_list_len
                                ),
                                "str"
                            )
                        );
                        replacement_protected = 1;
                        const R_xlen_t replacement_size =
                            XLENGTH(inner_value);
                        if (replacement_size < 0 ||
                                replacement_size > R_LEN_T_MAX) {
                            Rf_error(
                                "long character vectors are not supported"
                            );
                        }
                        replacement_len =
                            static_cast<R_len_t>(replacement_size);
                        replacement_reader.reset(inner_value);
                        if (replacement_reader.size() !=
                                replacement_size) {
                            throw std::runtime_error(
                                "character vector length changed during an operation"
                            );
                        }
                        replacement_views.resize(replacement_size);
                        replacement_reader.views(
                            0, replacement_size,
                            replacement_views.ptrs(),
                            replacement_views.lengths(),
                            replacement_views.encodings()
                        );
                        replacements.resize(
                            static_cast<std::size_t>(replacement_len)
                        );
                        for (R_len_t i = 0;
                                i < replacement_len; ++i) {
                            const CiSubFrameInput replacement =
                                ci__sub_stabilize_frame_input(
                                    ci__sub_normalize_frame_input(
                                        replacement_views[i],
                                        replacement_converter
                                    ),
                                    replacement_storage
                                );
                            replacements[static_cast<std::size_t>(i)] =
                                shared::StringView{
                                    replacement.length == 0
                                        ? "" : replacement.data,
                                    replacement.is_na
                                        ? shared::missing_string_length
                                        : replacement.length,
                                    replacement.is_na
                                        ? shared::StringEncoding::missing
                                        : replacement.is_ascii
                                            ? shared::StringEncoding::ascii
                                            : shared::StringEncoding::utf8
                                };
                            if (scalar_fast_path) {
                                scalar_replacement_input = replacement;
                                scalar_replacement_ready = true;
                            }
                        }
                    }

                    if (scalar_fast_path) {
                        callback_protections.release(replacement_protected);
                        replacement_protected = 0;
                        if (scalar_replacement_input.is_na) {
                            if (omit_na_1) {
                                builder.set_validated(
                                    outer,
                                    charport::StrView{
                                        source.length == 0
                                            ? "" : source.data,
                                        source.length,
                                        source.is_ascii
                                            ? cetype_ext_t::CE_ASCII
                                            : cetype_ext_t::CE_UTF8
                                    }
                                );
                            }
                            else {
                                builder.set_na(outer);
                            }
                            continue;
                        }

                        scalar_indexer.reset(
                            source.data, source.length, source.is_ascii
                        );
                        shared::substring::ByteRange range =
                            scalar_indexer.range(scalar_from, scalar_to);
                        if (range.end < range.begin)
                            range.end = range.begin;

                        const std::size_t prefix =
                            static_cast<std::size_t>(range.begin);
                        const std::size_t replacement_length =
                            static_cast<std::size_t>(
                                scalar_replacement_input.length
                            );
                        const std::size_t suffix =
                            static_cast<std::size_t>(
                                source.length-range.end
                            );
                        std::size_t output_size =
                            shared::substring::checked_output_size(
                                prefix, replacement_length
                            );
                        output_size =
                            shared::substring::checked_output_size(
                                output_size, suffix
                            );
                        const bool output_ascii =
                            (source.is_ascii ||
                             io::is_ascii(source.data, prefix)) &&
                            (scalar_replacement_input.is_ascii ||
                             io::is_ascii(
                                 scalar_replacement_input.data,
                                 replacement_length
                             )) &&
                            (source.is_ascii ||
                             io::is_ascii(
                                 source.data+range.end, suffix
                             ));
                        char* output = builder.reserve(
                            outer, output_size,
                            output_ascii
                                ? cetype_ext_t::CE_ASCII
                                : cetype_ext_t::CE_UTF8
                        );
                        if (prefix > 0) {
                            std::memcpy(
                                output, source.data, prefix
                            );
                        }
                        if (replacement_length > 0) {
                            std::memcpy(
                                output+prefix,
                                scalar_replacement_input.data,
                                replacement_length
                            );
                        }
                        if (suffix > 0) {
                            std::memcpy(
                                output+prefix+replacement_length,
                                source.data+range.end, suffix
                            );
                        }
                        continue;
                    }

                    SEXP inner_from = VECTOR_ELT(
                        from, outer % from_list_len
                    );
                    SEXP inner_to = R_NilValue;
                    SEXP inner_length = R_NilValue;
                    if (has_to) {
                        inner_to = VECTOR_ELT(
                            to, outer % to_list_len
                        );
                    }
                    else if (has_length) {
                        inner_length = VECTOR_ELT(
                            length, outer % length_list_len
                        );
                    }
                    R_len_t inner_from_len = 0;
                    R_len_t inner_to_len = 0;
                    R_len_t inner_length_len = 0;
                    int* inner_from_tab = nullptr;
                    int* inner_to_tab = nullptr;
                    int* inner_length_tab = nullptr;
                    if (ci__sub_matrix_has_too_many_columns_r(
                            inner_from, use_matrix_1)) {
                        ci__sub_emit_replacement_warnings_r(
                            pending_warnings
                        );
                    }
                    const R_len_t inner_protected =
                        ci__sub_prepare_from_to_length_r(
                            inner_from, inner_to, inner_length,
                            inner_from_len, inner_to_len,
                            inner_length_len,
                            inner_from_tab, inner_to_tab,
                            inner_length_tab, use_matrix_1
                    );
                    callback_protections.adopt(inner_protected);
                    const int inner_endpoint_len =
                        inner_to_len > inner_length_len
                            ? inner_to_len : inner_length_len;
                    const int inner_lengths[2] = {
                        inner_from_len, inner_endpoint_len
                    };
                    bool inner_recycling_warning = false;
                    const R_len_t inner_vectorize_len =
                        shared::substring::recycling_length(
                            inner_lengths, 2,
                            inner_recycling_warning
                        );
                    if (inner_recycling_warning) {
                        pending_warnings.push_back(
                            shared::substring::ReplacementWarning::
                                recycling_rule
                        );
                    }

                    const shared::StringView source_view{
                        source.length == 0 ? "" : source.data,
                        source.length,
                        source.is_ascii
                            ? shared::StringEncoding::ascii
                            : shared::StringEncoding::utf8
                    };
                    const shared::substring::ReplacementResult output =
                        assembler.build(
                            source_view,
                            replacements.empty()
                                ? nullptr : replacements.data(),
                            replacement_len,
                            inner_from_tab, inner_from_len,
                            inner_to_tab, inner_to_len,
                            inner_length_tab, inner_length_len,
                            inner_vectorize_len, omit_na_1
                        );
                    if (output.warning ==
                            shared::substring::ReplacementWarning::
                                replacement_zero) {
                        pending_warnings.push_back(output.warning);
                    }
                    else if (output.warning ==
                            shared::substring::ReplacementWarning::
                                recycling) {
                        pending_warnings.push_back(output.warning);
                    }

                    if (output.value.is_na()) {
                        builder.set_na(outer);
                    }
                    else {
                        builder.set_validated(
                            outer,
                            charport::StrView{
                                output.value.len == 0
                                    ? "" : output.value.ptr,
                                output.value.len,
                                output.value.enc ==
                                        shared::StringEncoding::ascii
                                    ? cetype_ext_t::CE_ASCII
                                    : cetype_ext_t::CE_UTF8
                            }
                        );
                    }
                    callback_protections.release(inner_protected);
                    callback_protections.release(replacement_protected);
                  }
                  output_store = builder.release_store();
                }
                catch (...) {
                    ci__sub_emit_replacement_warnings_r(
                        pending_warnings
                    );
                    throw;
                }

                result = entry_protections.reprotect_one(
                    io::finalize(std::move(output_store)), result_index
                );
                ci__sub_emit_replacement_warnings_r(pending_warnings);
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}
} } // namespace charr::altrep_backend
