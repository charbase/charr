
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
#include "collator/options.h"
#include "io/string_view.h"
#include "../shared/collation_search.h"
#include "../shared/collator.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"

#include <cstddef>
#include <exception>


namespace charr { namespace base_backend {

namespace search_coll_count {

CHARR_NEUTRAL_HELPER R_len_t recycling_length(
    R_len_t subject_length, R_len_t pattern_length, bool& warning
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


CHARR_CXX_HELPER void require_icu_success(UErrorCode status)
{
    if (U_FAILURE(status))
        throw StriException(status);
}


CHARR_CXX_HELPER void count_normalized(
    const shared::CollationInputs& subjects,
    const shared::CollationInputs& patterns,
    R_len_t subject_length, R_len_t pattern_length,
    R_len_t vectorize_length,
    shared::Collator& collator,
    shared::CollationMatcher& matcher,
    int* output
)
{
    for (R_len_t lane = 0; lane < pattern_length; ++lane) {
        const shared::CollationInput pattern = patterns.get(
            static_cast<std::size_t>(lane)
        );
        R_len_t i = lane;
        for (;;) {
            if (pattern.missing || pattern.length <= 0) {
                output[i] = NA_INTEGER;
            }
            else {
                const shared::CollationInput subject = subjects.get(
                    static_cast<std::size_t>(i % subject_length)
                );
                if (subject.missing) {
                    output[i] = NA_INTEGER;
                }
                else if (subject.length <= 0) {
                    output[i] = 0;
                }
                else {
                    UErrorCode status = U_ZERO_ERROR;
                    output[i] = matcher.count(
                        collator.get(), subject, pattern, status
                    );
                    require_icu_success(status);
                }
            }

            if (pattern_length >= vectorize_length-i)
                break;
            i += pattern_length;
        }
    }
}


CHARR_R_HELPER void emit_warnings(
    bool root_fallback_warning,
    bool recycling_warning,
    R_len_t empty_pattern_warnings
) noexcept
{
    if (root_fallback_warning) {
        Rf_warning(
            "%s", ICUError::getICUerrorName(U_USING_DEFAULT_WARNING)
        );
    }
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);
    for (R_len_t i = 0; i < empty_pattern_warnings; ++i)
        Rf_warning(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
}

} // namespace search_coll_count

using namespace search_coll_count;


/**
 * Count pattern occurrences in a string using collation
 *
 * @param str character vector
 * @param pattern character vector
 * @param opts_collator collator options
 * @return integer vector
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *          corrected behavior on empty str/pattern
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-23)
 *          make StriException-friendly,
 *          use collation::PatternSet
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-08)
 *          new fun: ci_count_coll (opts_collator == NA not allowed)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
CHARR_ENTRYPOINT SEXP ci_count_coll(
    SEXP str, SEXP pattern, SEXP opts_collator
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    str = entry_protections.protect_one(ci__prepare_arg_string_r(str, "str"));
    pattern = entry_protections.protect_one(ci__prepare_arg_string_r(pattern, "pattern"));
    const shared::CollatorOptions options =
        collator::prepare_options(opts_collator);

    const R_len_t subject_length = LENGTH(str);
    const R_len_t pattern_length = LENGTH(pattern);
    bool recycling_needed = false;
    const R_len_t vectorize_length = recycling_length(
        subject_length, pattern_length, recycling_needed
    );

    bool root_fallback_warning = false;
    bool recycling_warning = false;
    R_len_t empty_pattern_warnings = 0;

    try {
        shared::Collator collator_owner;
        shared::NativeToUtf8 subject_converter;
        shared::NativeToUtf8 pattern_converter;
        shared::SliceArena subject_storage;
        shared::SliceArena pattern_storage;
        shared::CollationInputs subjects;
        shared::CollationInputs patterns;
        shared::CollationMatcher matcher;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const shared::CollatorOpenResult opened =
                    collator_owner.reset(options);
                root_fallback_warning = opened.root_fallback;
                require_icu_success(opened.status);
                recycling_warning = recycling_needed;

                if (vectorize_length > 0) {
                    subjects.resize(
                        static_cast<std::size_t>(subject_length)
                    );
                    const SEXP* subject_values = STRING_PTR_RO(str);
                    for (R_len_t i = 0; i < subject_length; ++i) {
                        const shared::StringView value =
                            shared::normalize_utf8_preserve_bom(
                                io::as_shared_view(subject_values[i]),
                                subject_converter, subject_storage
                            );
                        subjects.set(static_cast<std::size_t>(i), value);
                    }

                    patterns.resize(
                        static_cast<std::size_t>(pattern_length)
                    );
                    const SEXP* pattern_values = STRING_PTR_RO(pattern);
                    for (R_len_t i = 0; i < pattern_length; ++i) {
                        const shared::StringView value =
                            shared::normalize_utf8_preserve_bom(
                                io::as_shared_view(pattern_values[i]),
                                pattern_converter, pattern_storage
                            );
                        patterns.set(static_cast<std::size_t>(i), value);
                    }

                    R_len_t normalized_empty_patterns = 0;
                    for (R_len_t i = 0; i < pattern_length; ++i) {
                        const shared::CollationInput value = patterns.get(
                            static_cast<std::size_t>(i)
                        );
                        if (!value.missing && value.length <= 0)
                            ++normalized_empty_patterns;
                    }
                    empty_pattern_warnings = normalized_empty_patterns;
                }

                result = entry_protections.reprotect_one(
                    Rf_allocVector(INTSXP, vectorize_length), result_index
                );
                int* output = INTEGER(result);
                if (vectorize_length > 0) {
                    count_normalized(
                        subjects, patterns,
                        subject_length, pattern_length,
                        vectorize_length,
                        collator_owner, matcher, output
                    );
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_warnings(
            root_fallback_warning,
            recycling_warning,
            empty_pattern_warnings
        );
    );
}

} } // namespace charr::base_backend
