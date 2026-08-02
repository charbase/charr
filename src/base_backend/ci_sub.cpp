
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
#include "ci_string8buf.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/substring.h"
#include "../shared/unwind.h"
#include <exception>
#include <stdexcept>
#include <vector>


namespace charr { namespace base_backend {

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

CHARR_R_HELPER bool ci__sub_plain_integer_scalar(
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


CHARR_R_HELPER bool ci__sub_plain_list_integer_scalar(
    SEXP values, R_len_t values_length, int& output
) noexcept
{
    return TYPEOF(values) == VECSXP && !Rf_isObject(values) &&
        !ALTREP(values) && NO_ATTRIB(values) && values_length == 1 &&
        ci__sub_plain_integer_scalar(VECTOR_ELT(values, 0), output);
}


struct CiSubInput {
    const char* data;
    R_len_t length;
    bool is_na;
    bool is_ascii;
    bool converted;
};


struct CiSubSource {
    const char* data;
    R_len_t length;
    bool is_na;
    bool is_ascii;
    bool is_bytes;
    bool is_utf8;
    bool is_latin1;
};


CHARR_R_HELPER CiSubSource ci__sub_read_source_r(SEXP value) noexcept
{
    if (value == NA_STRING)
        return CiSubSource{nullptr, 0, true, false, false, false, false};
    return CiSubSource{
        CHAR(value), LENGTH(value), false,
        IS_ASCII(value) != 0, IS_BYTES(value) != 0,
        IS_UTF8(value) != 0, IS_LATIN1(value) != 0
    };
}


CHARR_CXX_HELPER CiSubInput ci__sub_normalize_source(
    const CiSubSource& source, shared::NativeToUtf8& converter
)
{
    if (source.is_na)
        return CiSubInput{nullptr, 0, true, false, false};
    if (source.is_ascii) {
        return CiSubInput{
            source.data, source.length, false, true, false
        };
    }
    if (source.is_bytes)
        throw StriException(MSG__BYTESENC);

    const char* data = source.data;
    R_len_t length = source.length;
    bool converted = false;
    if (!source.is_utf8) {
        try {
            const bool native_has_bom = !source.is_latin1 &&
                STRI__ENC_HAS_BOM_UTF8(data, length);
            const shared::ByteView output = source.is_latin1
                ? converter.latin1(data, length)
                : converter.native(data, length);
            data = output.ptr;
            length = output.len;
            converted = true;
            if (native_has_bom && STRI__ENC_HAS_BOM_UTF8(data, length)) {
                data += 3;
                length -= 3;
            }
        }
        catch (const std::exception& error) {
            throw StriException("%s", error.what());
        }
    }
    else if (STRI__ENC_HAS_BOM_UTF8(data, length)) {
        data += 3;
        length -= 3;
    }
    if (length == 0)
        data = "";
    return CiSubInput{data, length, false, false, converted};
}


CHARR_CXX_HELPER CiSubInput ci__sub_stabilize(
    const CiSubInput& input, shared::SliceArena& storage
)
{
    if (!input.converted || input.length <= 0)
        return input;
    char* output = storage.allocate(static_cast<std::size_t>(input.length));
    std::memcpy(output, input.data, static_cast<std::size_t>(input.length));
    return CiSubInput{
        output, input.length, false, input.is_ascii, false
    };
}


CHARR_R_HELPER inline SEXP ci__sub_source_element_r(
    SEXP source, const SEXP* direct, R_len_t index
) noexcept
{
    return direct ? direct[index] : STRING_ELT(source, index);
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
 * @version 0.1-?? (Marek Gagolewski)
 *    Use UTF-8 input and code-point-to-byte indexing
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-01)
 *    Use cached code-point-to-byte indexes
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *    Make StriException-friendly
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-20)
 *    Use indexed UTF-8 input
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

    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    const bool use_matrix_1 = ci__prepare_arg_logical_1_notNA_r(
        use_matrix, "use_matrix"
    );
    const bool ignore_negative_length_1 =
        ci__prepare_arg_logical_1_notNA_r(
            ignore_negative_length, "ignore_negative_length"
        );

    const R_len_t str_len = LENGTH(str);
    R_len_t from_len      = 0;
    R_len_t to_len        = 0;
    R_len_t length_len    = 0;
    int* from_tab         = 0;
    int* to_tab           = 0;
    int* length_tab       = 0;
    R_len_t vectorize_len = 0;
    bool scalar_bounds = false;

    try {
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        shared::substring::Utf8Indexer indexer;
        std::vector<CiSubInput> inputs;

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

                const SEXP* source = ALTREP(str)
                    ? nullptr : STRING_PTR_RO(str);

                if (!scalar_bounds && vectorize_len > 0) {
                    for (R_len_t i = 0; i < str_len; ++i) {
                        const CiSubSource raw = ci__sub_read_source_r(
                            ci__sub_source_element_r(str, source, i)
                        );
                        inputs[static_cast<std::size_t>(i)] =
                            ci__sub_stabilize(
                                ci__sub_normalize_source(raw, converter),
                                storage
                            );
                    }
                }

                result = entry_protections.reprotect_one(
                    Rf_allocVector(STRSXP, vectorize_len), result_index
                );
                R_len_t negative_lengths = 0;

                for (R_len_t i = 0; i < vectorize_len; ++i) {
                    CiSubInput value;
                    if (scalar_bounds) {
                        const CiSubSource raw = ci__sub_read_source_r(
                            ci__sub_source_element_r(str, source, i)
                        );
                        value = ci__sub_normalize_source(raw, converter);
                    }
                    else {
                        value = inputs[static_cast<std::size_t>(i % str_len)];
                    }

                    R_len_t current_from = from_tab[i % from_len];
                    R_len_t current_to = to_tab
                        ? to_tab[i % to_len]
                        : length_tab[i % length_len];
                    if (value.is_na || current_from == NA_INTEGER ||
                            current_to == NA_INTEGER) {
                        SET_STRING_ELT(result, i, NA_STRING);
                        continue;
                    }

                    if (length_tab) {
                        if (current_to == 0) {
                            SET_STRING_ELT(result, i, R_BlankString);
                            continue;
                        }
                        if (current_to < 0) {
                            SET_STRING_ELT(result, i, NA_STRING);
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
                        SET_STRING_ELT(
                            result, i,
                            Rf_mkCharLenCE(
                                value.data+range.begin,
                                range.end-range.begin,
                                scalar_bounds && value.is_ascii
                                    ? CE_NATIVE : CE_UTF8
                            )
                        );
                    }
                    else {
                        SET_STRING_ELT(result, i, R_BlankString);
                    }
                }

                if (negative_lengths > 0 &&
                        ignore_negative_length_1) {
                    const SEXP old_result = result;
                    SEXP filtered = callback_protections.protect_one(
                        Rf_allocVector(
                            STRSXP, vectorize_len-negative_lengths
                        )
                    );
                    R_len_t output = 0;
                    for (R_len_t i =
                            shared::substring::recycled_order_begin(
                                str_len, vectorize_len
                            );
                            i < vectorize_len;
                            i = shared::substring::recycled_order_next(
                                i, str_len, vectorize_len
                            )) {
                        const CiSubInput& value =
                            inputs[static_cast<std::size_t>(i % str_len)];
                        const R_len_t current_from = from_tab[i % from_len];
                        const R_len_t current_length =
                            length_tab[i % length_len];
                        if (!value.is_na && current_from != NA_INTEGER &&
                                current_length != NA_INTEGER &&
                                current_length < 0) {
                            continue;
                        }
                        SET_STRING_ELT(
                            filtered, output++, STRING_ELT(old_result, i)
                        );
                    }
                    result = entry_protections.reprotect_one(
                        filtered, result_index
                    );
                }

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
 * @version 0.1-?? (Marek Gagolewski)
 *          use UTF-8 input and code-point-to-byte indexing
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-01)
 *          use cached code-point-to-byte indexes
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException-friendly
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-20)
 *          Use indexed UTF-8 input
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

    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    value = entry_protections.protect_one(ci__prepare_arg_string_r(value, "value"));
    const bool omit_na_1 = ci__prepare_arg_logical_1_notNA_r(
        omit_na, "omit_na"
    );
    const bool use_matrix_1 = ci__prepare_arg_logical_1_notNA_r(
        use_matrix, "use_matrix"
    );

    const R_len_t value_len = LENGTH(value);
    const R_len_t str_len = LENGTH(str);
    R_len_t from_len = 0;
    R_len_t to_len = 0;
    R_len_t length_len = 0;
    int* from_tab = nullptr;
    int* to_tab = nullptr;
    int* length_tab = nullptr;
    R_len_t vectorize_len = 0;
    bool scalar_bounds = false;

    try {
        shared::NativeToUtf8 source_converter;
        shared::NativeToUtf8 replacement_converter;
        shared::SliceArena source_storage;
        shared::SliceArena replacement_storage;
        shared::substring::Utf8Indexer indexer;
        String8buf buffer(0);
        std::vector<CiSubInput> sources;
        std::vector<CiSubInput> replacements;

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

                const SEXP* source_elements = ALTREP(str)
                    ? nullptr : STRING_PTR_RO(str);
                const SEXP* replacement_elements = ALTREP(value)
                    ? nullptr : STRING_PTR_RO(value);
                CiSubInput scalar_replacement;

                if (scalar_bounds) {
                    scalar_replacement = ci__sub_normalize_source(
                        ci__sub_read_source_r(
                            ci__sub_source_element_r(
                                value, replacement_elements, 0
                            )
                        ),
                        replacement_converter
                    );
                }
                else if (vectorize_len > 0) {
                    for (R_len_t i = 0; i < str_len; ++i) {
                        sources[static_cast<std::size_t>(i)] =
                            ci__sub_stabilize(
                                ci__sub_normalize_source(
                                    ci__sub_read_source_r(
                                        ci__sub_source_element_r(
                                            str, source_elements, i
                                        )
                                    ),
                                    source_converter
                                ),
                                source_storage
                            );
                    }
                    for (R_len_t i = 0; i < value_len; ++i) {
                        replacements[static_cast<std::size_t>(i)] =
                            ci__sub_stabilize(
                                ci__sub_normalize_source(
                                    ci__sub_read_source_r(
                                        ci__sub_source_element_r(
                                            value, replacement_elements, i
                                        )
                                    ),
                                    replacement_converter
                                ),
                                replacement_storage
                            );
                    }
                }

                result = entry_protections.reprotect_one(
                    Rf_allocVector(STRSXP, vectorize_len), result_index
                );

                for (R_len_t i = 0; i < vectorize_len; ++i) {
                    CiSubInput source;
                    CiSubInput replacement;
                    if (scalar_bounds) {
                        source = ci__sub_normalize_source(
                            ci__sub_read_source_r(
                                ci__sub_source_element_r(
                                    str, source_elements, i
                                )
                            ),
                            source_converter
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
                        SET_STRING_ELT(result, i, NA_STRING);
                        continue;
                    }
                    if (current_from == NA_INTEGER ||
                            current_to == NA_INTEGER ||
                            replacement.is_na) {
                        if (omit_na_1) {
                            SET_STRING_ELT(
                                result, i,
                                Rf_mkCharLenCE(
                                    source.data, source.length,
                                    source.is_ascii ? CE_NATIVE : CE_UTF8
                                )
                            );
                        }
                        else {
                            SET_STRING_ELT(result, i, NA_STRING);
                        }
                        continue;
                    }
                    if (!to_tab && current_to < 0) {
                        SET_STRING_ELT(
                            result, i,
                            Rf_mkCharLenCE(
                                source.data, source.length,
                                source.is_ascii ? CE_NATIVE : CE_UTF8
                            )
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
                    buffer.resize(output_size, false);
                    if (prefix > 0)
                        std::memcpy(buffer.data(), source.data, prefix);
                    if (replacement_length > 0) {
                        std::memcpy(
                            buffer.data()+prefix, replacement.data,
                            replacement_length
                        );
                    }
                    if (suffix > 0) {
                        std::memcpy(
                            buffer.data()+prefix+replacement_length,
                            source.data+range.end, suffix
                        );
                    }
                    SET_STRING_ELT(
                        result, i,
                        Rf_mkCharLenCE(
                            buffer.data(),
                            static_cast<R_len_t>(output_size),
                            scalar_bounds && source.is_ascii &&
                                    replacement.is_ascii
                                ? CE_NATIVE : CE_UTF8
                        )
                    );
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
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

    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    from = entry_protections.protect_one(ci__prepare_arg_list_r(from, "from"));
    to = entry_protections.protect_one(ci__prepare_arg_list_r(to, "to"));
    length = entry_protections.protect_one(ci__prepare_arg_list_r(length, "length"));
    const bool use_matrix_1 = ci__prepare_arg_logical_1_notNA_r(
        use_matrix, "use_matrix"
    );
    const bool ignore_negative_length_1 =
        ci__prepare_arg_logical_1_notNA_r(
            ignore_negative_length, "ignore_negative_length"
        );

    const R_len_t str_len = LENGTH(str);
    const R_len_t from_list_len = LENGTH(from);
    const R_len_t to_list_len = LENGTH(to);
    const R_len_t length_list_len = LENGTH(length);
    const bool has_to = !Rf_isNull(to);
    const bool has_length = !Rf_isNull(length);
    const R_len_t vectorize_len = has_to
        ? recycling_length_r(
            str_len, from_list_len, to_list_len
        )
        : has_length
            ? recycling_length_r(
                str_len, from_list_len, length_list_len
            )
            : recycling_length_r(str_len, from_list_len);

    int scalar_from = 0;
    int scalar_to = 0;
    const bool scalar_bounds = has_to && !has_length &&
        ci__sub_plain_list_integer_scalar(
            from, from_list_len, scalar_from
        ) &&
        ci__sub_plain_list_integer_scalar(
            to, to_list_len, scalar_to
        ) && scalar_from > 0 && scalar_to > 0;

    try {
        shared::NativeToUtf8 converter;
        shared::SliceArena storage;
        shared::substring::Utf8Indexer indexer;
        std::vector<CiSubInput> sources(
            static_cast<std::size_t>(str_len)
        );
        std::vector<unsigned char> source_ready(
            static_cast<std::size_t>(str_len), 0
        );

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const SEXP* source_elements = ALTREP(str)
                    ? nullptr : STRING_PTR_RO(str);
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
                        SEXP inner = callback_protections.protect_one(
                            Rf_allocVector(STRSXP, 0)
                        );
                        SET_VECTOR_ELT(result, outer, inner);
                        callback_protections.release(1);
                        callback_protections.release(inner_protected);
                        continue;
                    }
                    const R_len_t source_index = outer % str_len;
                    if (!source_ready[
                            static_cast<std::size_t>(source_index)
                        ]) {
                        const CiSubSource raw = ci__sub_read_source_r(
                            ci__sub_source_element_r(
                                str, source_elements, source_index
                            )
                        );
                        sources[static_cast<std::size_t>(source_index)] =
                            ci__sub_stabilize(
                                ci__sub_normalize_source(raw, converter),
                                storage
                            );
                        source_ready[
                            static_cast<std::size_t>(source_index)
                        ] = 1;
                    }
                    const CiSubInput& source = sources[
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
                    SEXP inner = callback_protections.protect_one(
                        Rf_allocVector(STRSXP, output_len)
                    );
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
                            SET_STRING_ELT(inner, output++, NA_STRING);
                            continue;
                        }
                        if (inner_length_tab) {
                            if (current_to == 0) {
                                SET_STRING_ELT(
                                    inner, output++, R_BlankString
                                );
                                continue;
                            }
                            if (current_to < 0) {
                                SET_STRING_ELT(inner, output++, NA_STRING);
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
                            SET_STRING_ELT(
                                inner, output++,
                                Rf_mkCharLenCE(
                                    source.data+range.begin,
                                    range.end-range.begin,
                                    source.is_ascii
                                        ? CE_NATIVE : CE_UTF8
                                )
                            );
                        }
                        else {
                            SET_STRING_ELT(
                                inner, output++, R_BlankString
                            );
                        }
                    }
                    SET_VECTOR_ELT(result, outer, inner);
                    callback_protections.release(1);
                    callback_protections.release(inner_protected);
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}


/** internal function - replace multiple substrings in a single string
 * can raise Rf_error
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

    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    from = entry_protections.protect_one(ci__prepare_arg_list_r(from, "from"));
    to = entry_protections.protect_one(ci__prepare_arg_list_r(to, "to"));
    length = entry_protections.protect_one(ci__prepare_arg_list_r(length, "length"));
    value = entry_protections.protect_one(ci__prepare_arg_list_r(value, "value"));
    const bool omit_na_1 = ci__prepare_arg_logical_1_notNA_r(
        omit_na, "omit_na"
    );
    const bool use_matrix_1 = ci__prepare_arg_logical_1_notNA_r(
        use_matrix, "use_matrix"
    );

    const R_len_t str_len = LENGTH(str);
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
            outer_lengths, has_to || has_length ? 4 : 3,
            outer_recycling_warning
        );

    int scalar_from = 0;
    int scalar_to = 0;
    SEXP scalar_value = R_NilValue;
    const bool scalar_bounds = vectorize_len > 0 && has_to &&
        !has_length &&
        ci__sub_plain_list_integer_scalar(
            from, from_list_len, scalar_from
        ) &&
        ci__sub_plain_list_integer_scalar(
            to, to_list_len, scalar_to
        ) && scalar_from > 0 && scalar_to > 0;
    const bool scalar_replacement = value_list_len == 1 &&
        TYPEOF(value) == VECSXP && !Rf_isObject(value) &&
        !ALTREP(value) && NO_ATTRIB(value) &&
        ((scalar_value = VECTOR_ELT(value, 0)),
         TYPEOF(scalar_value) == STRSXP &&
         !Rf_isObject(scalar_value) && !ALTREP(scalar_value) &&
         NO_ATTRIB(scalar_value) && XLENGTH(scalar_value) == 1);
    const bool scalar_fast_path = scalar_bounds && scalar_replacement;

    try {
        shared::NativeToUtf8 source_converter;
        shared::NativeToUtf8 replacement_converter;
        shared::SliceArena source_storage;
        shared::SliceArena replacement_storage;
        shared::substring::ReplacementAssembler assembler;
        shared::substring::Utf8Indexer scalar_indexer;
        std::vector<char> scalar_buffer;
        CiSubInput scalar_replacement_input{
            nullptr, 0, true, false, false
        };
        std::vector<CiSubInput> sources(
            static_cast<std::size_t>(str_len)
        );
        std::vector<unsigned char> source_ready(
            static_cast<std::size_t>(str_len), 0
        );
        std::vector<shared::StringView> replacements;
        std::vector<shared::substring::ReplacementWarning>
            pending_warnings;
        if (outer_recycling_warning) {
            pending_warnings.push_back(
                shared::substring::ReplacementWarning::recycling_rule
            );
        }

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const SEXP* source_elements = ALTREP(str)
                    ? nullptr : STRING_PTR_RO(str);
                if (scalar_fast_path) {
                    scalar_replacement_input = ci__sub_normalize_source(
                        ci__sub_read_source_r(STRING_ELT(scalar_value, 0)),
                        replacement_converter
                    );
                }
                result = entry_protections.reprotect_one(
                    Rf_allocVector(STRSXP, vectorize_len), result_index
                );

                try {
                  for (R_len_t outer = 0;
                          outer < vectorize_len; ++outer) {
                    const R_len_t source_index = outer % str_len;
                    if (!source_ready[
                            static_cast<std::size_t>(source_index)
                        ]) {
                        const CiSubSource raw = ci__sub_read_source_r(
                            ci__sub_source_element_r(
                                str, source_elements, source_index
                            )
                        );
                        sources[static_cast<std::size_t>(source_index)] =
                            ci__sub_stabilize(
                                ci__sub_normalize_source(
                                    raw, source_converter
                                ),
                                source_storage
                            );
                        source_ready[
                            static_cast<std::size_t>(source_index)
                        ] = 1;
                    }
                    const CiSubInput& source = sources[
                        static_cast<std::size_t>(source_index)
                    ];
                    if (source.is_na) {
                        SET_STRING_ELT(result, outer, NA_STRING);
                        continue;
                    }

                    if (scalar_fast_path) {
                        if (scalar_replacement_input.is_na) {
                            if (omit_na_1) {
                                SET_STRING_ELT(
                                    result, outer,
                                    Rf_mkCharLenCE(
                                        source.length == 0
                                            ? "" : source.data,
                                        source.length,
                                        source.is_ascii
                                            ? CE_NATIVE : CE_UTF8
                                    )
                                );
                            }
                            else {
                                SET_STRING_ELT(
                                    result, outer, NA_STRING
                                );
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
                        scalar_buffer.resize(output_size);
                        if (prefix > 0) {
                            std::memcpy(
                                scalar_buffer.data(), source.data,
                                prefix
                            );
                        }
                        if (replacement_length > 0) {
                            std::memcpy(
                                scalar_buffer.data()+prefix,
                                scalar_replacement_input.data,
                                replacement_length
                            );
                        }
                        if (suffix > 0) {
                            std::memcpy(
                                scalar_buffer.data()+prefix+
                                    replacement_length,
                                source.data+range.end, suffix
                            );
                        }
                        SET_STRING_ELT(
                            result, outer,
                            Rf_mkCharLenCE(
                                output_size == 0
                                    ? "" : scalar_buffer.data(),
                                static_cast<R_len_t>(output_size),
                                source.is_ascii &&
                                        scalar_replacement_input.is_ascii
                                    ? CE_NATIVE : CE_UTF8
                            )
                        );
                        continue;
                    }

                    SEXP inner_value = callback_protections.protect_one(
                        ci__prepare_arg_string_r(
                            VECTOR_ELT(
                                value, outer % value_list_len
                            ),
                            "str"
                        )
                    );
                    const R_len_t replacement_len = LENGTH(inner_value);
                    replacements.resize(
                        static_cast<std::size_t>(replacement_len)
                    );
                    const SEXP* replacement_elements = ALTREP(inner_value)
                        ? nullptr : STRING_PTR_RO(inner_value);
                    for (R_len_t i = 0; i < replacement_len; ++i) {
                        const CiSubInput replacement =
                            ci__sub_stabilize(
                                ci__sub_normalize_source(
                                    ci__sub_read_source_r(
                                        ci__sub_source_element_r(
                                            inner_value,
                                            replacement_elements, i
                                        )
                                    ),
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
                        SET_STRING_ELT(result, outer, NA_STRING);
                    }
                    else {
                        SET_STRING_ELT(
                            result, outer,
                            Rf_mkCharLenCE(
                                output.value.len == 0
                                    ? "" : output.value.ptr,
                                output.value.len,
                                output.value.enc ==
                                        shared::StringEncoding::ascii
                                    ? CE_NATIVE : CE_UTF8
                            )
                        );
                    }
                    callback_protections.release(inner_protected);
                    callback_protections.release(1);
                  }
                  ci__sub_emit_replacement_warnings_r(pending_warnings);
                }
                catch (...) {
                    ci__sub_emit_replacement_warnings_r(
                        pending_warnings
                    );
                    throw;
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}
} } // namespace charr::base_backend
