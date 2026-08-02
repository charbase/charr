// Derived from stringi.
// Copyright (c) 2013-2025, Marek Gagolewski. See inst/COPYRIGHTS.

#include "ci_stringi.h"
#include "fixed/options.h"
#include "io/string_view.h"
#include "../shared/entrypoint.h"
#include "../shared/fixed_search.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <cstddef>
#include <exception>
#include <vector>

namespace charr { namespace base_backend {

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


CHARR_R_HELPER bool direct_view(
    SEXP value, shared::StringView& output, bool& modified
) noexcept
{
    modified = false;
    if (value == NA_STRING) {
        output = shared::StringView{
            nullptr, shared::missing_string_length,
            shared::StringEncoding::missing
        };
        return true;
    }
    if (!IS_ASCII(value) && !IS_UTF8(value))
        return false;

    output = shared::StringView{
        CHAR(value), LENGTH(value),
        IS_ASCII(value)
            ? shared::StringEncoding::ascii
            : shared::StringEncoding::utf8
    };
    if (output.enc == shared::StringEncoding::utf8 &&
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


CHARR_R_HELPER bool extract_direct(
    SEXP subjects, SEXP patterns,
    shared::FixedSearchOptions options,
    R_len_t vectorize_length, SEXP output
) noexcept
{
    if (options.case_insensitive || options.overlap ||
            vectorize_length <= 0) {
        return false;
    }

    const R_len_t subject_length = LENGTH(subjects);
    const R_len_t pattern_length = LENGTH(patterns);
    if (subject_length <= 0 || pattern_length <= 0)
        return false;

    const SEXP* subject_values = STRING_PTR_RO(subjects);
    const SEXP* pattern_values = STRING_PTR_RO(patterns);
    for (R_len_t i = 0; i < vectorize_length; ++i) {
        const SEXP pattern_sexp = pattern_values[i % pattern_length];
        shared::StringView pattern;
        bool pattern_modified = false;
        if (!direct_view(pattern_sexp, pattern, pattern_modified) ||
                pattern_modified ||
                (!pattern.is_na() && pattern.len <= 0)) {
            return false;
        }

        shared::StringView subject;
        bool subject_modified = false;
        if (!direct_view(
                subject_values[i % subject_length],
                subject, subject_modified
            )) {
            return false;
        }

        if (subject.is_na() || pattern.is_na()) {
            SET_STRING_ELT(output, i, NA_STRING);
            continue;
        }

        const int match = shared::find_first_exact_bytes(
            subject.ptr, subject.len, pattern.ptr, pattern.len
        );
        SET_STRING_ELT(
            output, i, match < 0 ? NA_STRING : pattern_sexp
        );
    }
    return true;
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
    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    pattern = entry_protections.protect_one(ci__prepare_arg_string_r(pattern, "pattern"));

    const R_len_t subject_length = LENGTH(str);
    const R_len_t pattern_length = LENGTH(pattern);
    bool recycling_warning = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, recycling_warning
    );
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);

    R_len_t empty_pattern_warnings = 0;

    try {
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        std::vector<shared::StringView> subjects;
        std::vector<shared::StringView> patterns;
        shared::FixedMatcher matcher;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                result = entry_protections.reprotect_one(
                    Rf_allocVector(STRSXP, vectorize_length),
                    result_index
                );

                if (vectorize_length > 0 && !extract_direct(
                        str, pattern, options,
                        vectorize_length, result
                    )) {
                    const SEXP* subject_values = STRING_PTR_RO(str);
                    subjects.resize(
                        static_cast<std::size_t>(subject_length)
                    );
                    for (R_len_t i = 0; i < subject_length; ++i) {
                        subjects[static_cast<std::size_t>(i)] =
                            shared::normalize_utf8(
                                io::as_shared_view(subject_values[i]),
                                subject_converter, subject_storage
                            );
                    }

                    const SEXP* pattern_values = STRING_PTR_RO(pattern);
                    patterns.resize(
                        static_cast<std::size_t>(pattern_length)
                    );
                    for (R_len_t i = 0; i < pattern_length; ++i) {
                        patterns[static_cast<std::size_t>(i)] =
                            shared::normalize_utf8(
                                io::as_shared_view(pattern_values[i]),
                                pattern_converter, pattern_storage
                            );
                    }
                    empty_pattern_warnings = count_empty_patterns(patterns);

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
                            if (subject.is_na() || pattern_value.is_na() ||
                                    pattern_value.len <= 0) {
                                SET_STRING_ELT(result, i, NA_STRING);
                            }
                            else {
                                shared::FixedRange match{0, 0};
                                const bool found = matcher.find_first(
                                    subject, pattern_value, options, match
                                );
                                SET_STRING_ELT(
                                    result, i,
                                    found
                                        ? Rf_mkCharLenCE(
                                            subject.ptr+match.start,
                                            match.end-match.start,
                                            CE_UTF8
                                        )
                                        : NA_STRING
                                );
                            }

                            if (pattern_length >= vectorize_length-i)
                                break;
                            i += pattern_length;
                        }
                    }
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_empty_pattern_warnings(empty_pattern_warnings);
    );
}

} } // namespace charr::base_backend
