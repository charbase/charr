// Derived from stringi 19e9586ba39b3320df49355e32bd18d74ed6098f.
// Copyright (c) 2013-2025, Marek Gagolewski. See inst/COPYRIGHTS.

#include "ci_stringi.h"
#include "io/reader_utils.h"
#include "fixed/options.h"
#include "io/string_view.h"
#include "io/utf8_output.h"
#include "../shared/entrypoint.h"
#include "../shared/fixed_search.h"
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

namespace search_fixed_extract_first {

CHARR_NEUTRAL_HELPER R_len_t recycling_length(
    R_len_t subject_length, R_len_t pattern_length,
    bool& warning
) noexcept
{
    warning = false;
    if (subject_length <= 0 || pattern_length <= 0)
        return 0;

    const R_len_t result = subject_length > pattern_length
        ? subject_length
        : pattern_length;
    warning = result % subject_length != 0 ||
        result % pattern_length != 0;
    return result;
}


CHARR_NEUTRAL_HELPER inline bool direct_view(
    const charport::StrView& value,
    charport::StrView& output, bool& modified
) noexcept
{
    modified = false;
    if (value.is_na()) {
        output = value;
        return true;
    }

    if (value.enc != cetype_ext_t::CE_ASCII &&
            value.enc != cetype_ext_t::CE_UTF8 &&
            value.enc != cetype_ext_t::CE_ASCII_OR_UTF8) {
        return false;
    }
    if (value.len < 0 || value.ptr == nullptr)
        return false;

    output = value;
    if (output.enc != cetype_ext_t::CE_ASCII &&
            output.len >= 3 &&
            static_cast<unsigned char>(output.ptr[0]) == 0xefU &&
            static_cast<unsigned char>(output.ptr[1]) == 0xbbU &&
            static_cast<unsigned char>(output.ptr[2]) == 0xbfU) {
        output.ptr += 3;
        output.len -= 3;
        modified = true;
    }
    return true;
}


CHARR_NEUTRAL_HELPER inline charport::StrView matched_output_view(
    const char* data, int length
) noexcept
{
    cetype_ext_t encoding = cetype_ext_t::CE_ASCII;
    for (int i = 0; i < length; ++i) {
        if (static_cast<unsigned char>(data[i]) > 0x7fU) {
            encoding = cetype_ext_t::CE_UTF8;
            break;
        }
    }
    return charport::StrView{data, length, encoding};
}


CHARR_CXX_HELPER bool extract_direct(
    const charport::StrViews& subjects,
    const charport::StrViews& patterns,
    shared::FixedSearchOptions options,
    R_len_t vectorize_length,
    io::OutputBuilder& output
)
{
    if (options.case_insensitive || options.overlap ||
            vectorize_length <= 0 ||
            subjects.size() <= 0 || patterns.size() <= 0) {
        return false;
    }

    const bool direct_subject_length =
        subjects.size() == vectorize_length;
    const bool direct_pattern_length =
        patterns.size() == vectorize_length;

    for (R_len_t i = 0; i < vectorize_length; ++i) {
        charport::StrView pattern;
        bool pattern_modified = false;
        if (!direct_view(
                patterns[direct_pattern_length
                    ? static_cast<R_xlen_t>(i)
                    : static_cast<R_xlen_t>(i) % patterns.size()],
                pattern, pattern_modified
            ) || pattern_modified ||
                (!pattern.is_na() && pattern.len <= 0)) {
            return false;
        }

        charport::StrView subject;
        bool subject_modified = false;
        if (!direct_view(
                subjects[direct_subject_length
                    ? static_cast<R_xlen_t>(i)
                    : static_cast<R_xlen_t>(i) % subjects.size()],
                subject, subject_modified
            )) {
            return false;
        }

        if (subject.is_na() || pattern.is_na()) {
            output.set_na(i);
            continue;
        }

        const int match = shared::find_first_exact_bytes(
            subject.ptr, subject.len, pattern.ptr, pattern.len
        );
        if (match < 0) {
            output.set_na(i);
        }
        else {
            output.set_validated(
                i, matched_output_view(pattern.ptr, pattern.len)
            );
        }
    }
    return true;
}


CHARR_CXX_HELPER void normalize_views(
    const charport::StrViews& input,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    std::vector<shared::StringView>& output
)
{
    output.resize(static_cast<std::size_t>(input.size()));
    for (R_xlen_t i = 0; i < input.size(); ++i) {
        output[static_cast<std::size_t>(i)] = shared::normalize_utf8(
            io::as_shared_view(input[i]), converter, storage
        );
    }
}


CHARR_NEUTRAL_HELPER R_len_t count_empty_patterns(
    const std::vector<shared::StringView>& patterns
) noexcept
{
    R_len_t result = 0;
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        const shared::StringView& pattern = patterns[i];
        if (!pattern.is_na() && pattern.len <= 0)
            ++result;
    }
    return result;
}


CHARR_R_HELPER void emit_empty_pattern_warnings(
    R_len_t count
) noexcept
{
    for (R_len_t i = 0; i < count; ++i)
        Rf_warning(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
}

} // namespace search_fixed_extract_first

using namespace search_fixed_extract_first;


CHARR_ENTRYPOINT SEXP ci_extract_first_fixed(
    SEXP str, SEXP pattern, SEXP opts_fixed
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    const shared::FixedSearchOptions options = fixed::prepare_options(
        opts_fixed, false
    );
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    pattern = entry_protections.protect_one(
        ci__prepare_arg_string_r(pattern, "pattern")
    );

    const R_xlen_t subject_xlength = XLENGTH(str);
    const R_xlen_t pattern_xlength = XLENGTH(pattern);
    if (subject_xlength > R_LEN_T_MAX || pattern_xlength > R_LEN_T_MAX)
        Rf_error("long character vectors are not supported");
    const R_len_t subject_length = static_cast<R_len_t>(subject_xlength);
    const R_len_t pattern_length = static_cast<R_len_t>(pattern_xlength);
    bool recycling_warning = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, recycling_warning
    );
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);


    R_len_t empty_pattern_warnings = 0;

    try {
        charport::Reader subject_reader;
        charport::Reader pattern_reader;
        charport::StrViews subject_views;
        charport::StrViews pattern_views;
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        std::vector<shared::StringView> subjects;
        std::vector<shared::StringView> patterns;
        shared::FixedMatcher matcher;
        io::OutputBuilder output(0);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                output.reset(vectorize_length);
                if (vectorize_length > 0) {
                    subject_reader.reset(str);
                    if (subject_reader.size() != subject_length) {
                        throw std::runtime_error(
                            "Reader length changed during fixed extraction"
                        );
                    }
                    subject_views.resize(subject_length);
                    subject_reader.views(
                        0, subject_length,
                        subject_views.ptrs(), subject_views.lengths(),
                        subject_views.encodings()
                    );

                    pattern_reader.reset(pattern);
                    if (pattern_reader.size() != pattern_length) {
                        throw std::runtime_error(
                            "Reader length changed during fixed extraction"
                        );
                    }
                    pattern_views.resize(pattern_length);
                    pattern_reader.views(
                        0, pattern_length,
                        pattern_views.ptrs(), pattern_views.lengths(),
                        pattern_views.encodings()
                    );

                    if (!extract_direct(
                            subject_views, pattern_views, options,
                            vectorize_length, output
                        )) {
                        output.reset(vectorize_length);
                        normalize_views(
                            subject_views, subject_converter,
                            subject_storage, subjects
                        );
                        normalize_views(
                            pattern_views, pattern_converter,
                            pattern_storage, patterns
                        );
                        empty_pattern_warnings =
                            count_empty_patterns(patterns);

                        for (R_len_t lane = 0;
                                lane < pattern_length; ++lane) {
                            const shared::StringView& pattern_value = patterns[
                                static_cast<std::size_t>(lane)
                            ];
                            R_len_t i = lane;
                            for (;;) {
                                const shared::StringView& subject = subjects[
                                    static_cast<std::size_t>(
                                        i % subject_length
                                    )
                                ];
                                if (subject.is_na() ||
                                        pattern_value.is_na() ||
                                        pattern_value.len <= 0) {
                                    output.set_na(i);
                                }
                                else {
                                    shared::FixedRange match{0, 0};
                                    const bool found = matcher.find_first(
                                        subject, pattern_value,
                                        options, match
                                    );
                                    if (!found) {
                                        output.set_na(i);
                                    }
                                    else {
                                        output.set_validated(
                                            i,
                                            matched_output_view(
                                                subject.ptr+match.start,
                                                match.end-match.start
                                            )
                                        );
                                    }
                                }

                                if (pattern_length >= vectorize_length-i)
                                    break;
                                i += pattern_length;
                            }
                        }
                    }
                }

                result = entry_protections.reprotect_one(
                    output.to_sexp(), result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_empty_pattern_warnings(empty_pattern_warnings);
    );
}

} } // namespace charr::altrep_backend
