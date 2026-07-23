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
#include "ci_container_utf8.h"
#include "ci_container_regex.h"
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <deque>
#include <utility>
using namespace std;


namespace {

struct CiMatchRegexMatrix {
    R_len_t nrow;
    R_len_t ncol;
    charport::charvec::Store output;
    vector<string> capture_names;

    CiMatchRegexMatrix()
        : nrow(0), ncol(0), output(0, 0), capture_names()
    {
    }
};


bool ci__match_has_capture_names(const vector<string>& names)
{
    for (vector<string>::const_iterator it=names.begin();
            it != names.end(); ++it) {
        if (!it->empty())
            return true;
    }
    return false;
}


SEXP ci__match_capture_dimnames(const vector<string>& names)
{
    if (!ci__match_has_capture_names(names))
        return R_NilValue;

    SEXP colnames, dimnames;
    PROTECT(dimnames = Rf_allocVector(VECSXP, 2));
    PROTECT(colnames = Rf_allocVector(STRSXP, names.size()+1));
    SET_STRING_ELT(colnames, 0, Rf_mkChar(""));
    for (size_t i=0; i<names.size(); ++i) {
        SET_STRING_ELT(
            colnames, i+1,
            Rf_mkCharLenCE(
                names[i].empty()?"":names[i].data(),
                names[i].size(), CE_UTF8
            )
        );
    }
    SET_VECTOR_ELT(dimnames, 1, colnames);
    UNPROTECT(2);
    return dimnames;
}


SEXP ci__match_set_matrix_attributes(
    SEXP matrix, R_len_t nrow, R_len_t ncol, SEXP dimnames
)
{
    SEXP dim;
    PROTECT(dim = Rf_allocVector(INTSXP, 2));
    INTEGER(dim)[0] = nrow;
    INTEGER(dim)[1] = ncol;
    Rf_setAttrib(matrix, R_DimSymbol, dim);
    if (!Rf_isNull(dimnames))
        Rf_setAttrib(matrix, R_DimNamesSymbol, dimnames);
    UNPROTECT(1);
    return matrix;
}


void ci__match_set_all_na(
    charport::charvec::Builder& builder, R_xlen_t size
)
{
    for (R_xlen_t i=0; i<size; ++i)
        builder.set_na(i);
}


void ci__match_set_cg_missing(
    charport::charvec::Builder& builder, R_xlen_t i,
    const charport::StrView& value
)
{
    if (value.is_na()) {
        builder.set_na(i);
        return;
    }
    ci::builder_set(
        builder, i, value.ptr, value.len, value.enc
    );
}


R_xlen_t ci__match_matrix_size(R_len_t nrow, R_len_t ncol)
{
    // Deviation from stringi: reject an unrepresentable flat Builder length
    // before multiplying the copied matrix dimensions.
    if (nrow < 0 || ncol < 0 ||
            (nrow > 0 &&
             static_cast<R_xlen_t>(ncol) > R_XLEN_T_MAX/nrow)) {
        throw length_error("matrix length exceeds R's vector limit");
    }
    return static_cast<R_xlen_t>(nrow)*ncol;
}

} // namespace



/**
 * Extract all capture groups of the first/last occurrence
 * of a regex pattern in each string
 *
 * @param str character vector
 * @param pattern character vector
 * @param opts_regex list
 * @param first logical - search for the first or the last occurrence?
 * @param cg_missing single string
 * @return character matrix
 *
 * @version 0.1-??? (Marek Gagolewski, 2013-06-22)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-06)
 *    new arg: cg_missing
 *
 * @version 1.0-2 (Marek Gagolewski, 2016-01-29)
 *    Issue #214: allow a regex pattern like `.*`  to match an empty string
 *
 * @version 1.1.8 (Marek Gagolewski, 2018-04-09)
 *    #288: ci_match did not return correct number of columns
 *    when input was empty
 *
 * @version 1.4.7 (Marek Gagolewski, 2020-08-24)
 *    Use StriContainerRegexPattern::getRegexOptions
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-19)
 *    #153: named capture groups
 */
SEXP ci__match_firstlast_regex(SEXP str, SEXP pattern, SEXP cg_missing, SEXP opts_regex, bool first)
{
    // @TODO: capture_groups arg (integer vector/set - which capture groups to extract)
    PROTECT(str = ci__prepare_arg_string(str, "str")); // prepare string argument
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern")); // prepare string argument
    PROTECT(cg_missing = ci__prepare_arg_string_1(cg_missing, "cg_missing"));
    UText* str_text = NULL; // may potentially be slower, but definitely is more convenient!
    STRI__ERROR_HANDLER_BEGIN(3)
    SEXP ret;
    {
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    R_len_t str_n = ci::checked_r_len(
        context.size(str), "character vectors"
    );
    R_len_t pattern_n = ci::checked_r_len(
        context.size(pattern), "character vectors"
    );
    R_len_t vectorize_length = 0;
    charport::unwind_protect([&]() -> SEXP {
        vectorize_length = ci__recycling_rule(
            false, 2, str_n, pattern_n
        );
        return R_NilValue;
    });
    // Deviation from stringi: queue the recycling warning until capture
    // staging, regex/Reader/UText owners, and the output Builder are gone.
    if (vectorize_length > 0 &&
            (vectorize_length % str_n != 0 ||
             vectorize_length % pattern_n != 0))
        context.warn(MSG__WARN_RECYCLING_RULE);

    StriRegexMatcherOptions pattern_opts;
    charport::unwind_protect([&]() -> SEXP {
        pattern_opts = StriContainerRegexPattern::getRegexOptions(
            STRI__DEFERRED_WARNINGS, opts_regex
        );
        return R_NilValue;
    });

    CiMatchRegexMatrix result;
    {
        StriContainerUTF8 str_cont(
            context, str, vectorize_length
        );
        StriContainerUTF8 cg_missing_cont(
            context, cg_missing, 1
        );
        // The copied container validates cg_missing as UTF-8 input, but
        // stringi inserts the original value and therefore preserves its mark.
        shared_ptr<ci::ReaderBorrow> cg_missing_borrow =
            context.acquire(cg_missing);
        const charport::StrView cg_missing_value =
            cg_missing_borrow->views()[0];

        // we don't know how many capture groups are there:
        vector< vector< pair<const char*, const char*> > > occurrences(
            static_cast<size_t>(vectorize_length)
        );
        R_len_t occurrences_max = 1;

        StriContainerRegexPattern pattern_cont(
            context, pattern,
            (str_n>0)?vectorize_length:pattern_n, pattern_opts
        );
        if (str_n == 0 && pattern_n > 0) {
            // we need to determine the number of capture groups anyway
            for (R_len_t i = pattern_cont.vectorize_init();
                    i != pattern_cont.vectorize_end();
                    i = pattern_cont.vectorize_next(i))
            {
                if ((pattern_cont).isNA(i) ||
                        (pattern_cont).get(i).length() <= 0) {
                    if (!(pattern_cont).isNA(i))
                        context.warn(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
                    continue;
                }

                RegexMatcher *matcher = pattern_cont.getMatcher(i); // will be deleted automatically
                int pattern_cur_groups = matcher->groupCount();
                if (occurrences_max < pattern_cur_groups+1)
                    occurrences_max=pattern_cur_groups+1;
            }
        }
        else
        {
            for (R_len_t i = pattern_cont.vectorize_init();
                    i != pattern_cont.vectorize_end();
                    i = pattern_cont.vectorize_next(i))
            {
                if ((pattern_cont).isNA(i) ||
                        (pattern_cont).get(i).length() <= 0) {
                    if (!(pattern_cont).isNA(i))
                        context.warn(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
                    continue;
                }

                UErrorCode status = U_ZERO_ERROR;
                RegexMatcher *matcher = pattern_cont.getMatcher(i); // will be deleted automatically
                int pattern_cur_groups = matcher->groupCount();
                if (occurrences_max < pattern_cur_groups+1)
                    occurrences_max=pattern_cur_groups+1;

                if ((str_cont).isNA(i)) {
                    continue;
                }

                str_text = utext_openUTF8(
                    str_text, str_cont.get(i).data(),
                    str_cont.get(i).length(), &status
                );
                STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
                const char* str_cur_s = str_cont.get(i).data();

                occurrences[i] = vector< pair<const char*, const char*> >(
                    pattern_cur_groups+1
                );
                matcher->reset(str_text);
                while (1) {
                    int m_res = (int)matcher->find(status);
                    STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
                    if (!m_res) break;
                    occurrences[i][0].first  = str_cur_s+(int)matcher->start(status);
                    occurrences[i][0].second = str_cur_s+(int)matcher->end(status);
                    for (R_len_t j=1; j<=pattern_cur_groups; ++j) {
                        int m_start = (int)matcher->start(j, status);
                        STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
                        int m_end = (int)matcher->end(j, status);
                        STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
                        if (m_start < 0 || m_end < 0) {
                            occurrences[i][j].first  = NULL;
                            occurrences[i][j].second = NULL;
                        }
                        else {
                            occurrences[i][j].first  = str_cur_s+m_start;
                            occurrences[i][j].second = str_cur_s+m_end;
                        }
                    }
                    if (first) break;
                }
            }
        }

        if (str_text) {
            utext_close(str_text);
            str_text = NULL;
        }

        result.nrow = vectorize_length;
        result.ncol = occurrences_max;
        R_xlen_t output_size = ci__match_matrix_size(
            vectorize_length, occurrences_max
        );
        charport::charvec::Builder output(output_size);
        ci__match_set_all_na(output, output_size);

        // Deviation from stringi: native output storage owns captured bytes,
        // and C++ owns capture names until the subject Reader ends.
        for (R_len_t i=0; i<vectorize_length; ++i) {
            R_len_t ni = static_cast<R_len_t>(occurrences[i].size());
            for (R_len_t j=0; j<ni; ++j) {
                pair<const char*, const char*> retij = occurrences[i][j];
                R_xlen_t output_i =
                    i+static_cast<R_xlen_t>(j)*vectorize_length;
                if (retij.first != NULL && retij.second != NULL) {
                    ci::builder_set(
                        output, output_i, retij.first,
                        static_cast<R_len_t>(retij.second-retij.first),
                        cetype_ext_t::CE_ASCII_OR_UTF8
                    );
                }
                else {
                    ci__match_set_cg_missing(
                        output, output_i, cg_missing_value
                    );
                }
            }
        }

        if (pattern_cont.get_n() == 1 &&
                !pattern_cont.isNA(0) &&
                pattern_cont.get(0).length() > 0) {
            const vector<string>& capture_names =
                pattern_cont.getCaptureGroupNames(0);
            if (ci__match_has_capture_names(capture_names))
                result.capture_names = capture_names;
        }
        result.output = output.release_store();
    }

    STRI__PROTECT(ret = charport::charvec::wrap(
        std::move(result.output)
    ));
    SEXP dimnames;
    STRI__PROTECT(dimnames = charport::unwind_protect([&]() -> SEXP {
        return ci__match_capture_dimnames(result.capture_names);
    }));
    charport::unwind_protect([&]() -> SEXP {
        return ci__match_set_matrix_attributes(
            ret, result.nrow, result.ncol, dimnames
        );
    });
    STRI__UNPROTECT(1);
    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(if (str_text) utext_close(str_text);)
}


/**
 * Extract all capture groups of the first occurrence of a regex pattern in each string
 *
 * @param str character vector
 * @param pattern character vector
 * @param cg_missing single string
 * @param opts_regex list
 * @return character matrix
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-22)
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-06)
 *    new arg: cg_missing
 */
SEXP ci_match_first_regex(SEXP str, SEXP pattern, SEXP cg_missing, SEXP opts_regex)
{
    return ci__match_firstlast_regex(str, pattern, cg_missing, opts_regex, true);
}


/**
 * Extract all capture groups of the  last occurrence of a regex pattern in each string
 *
 * @param str character vector
 * @param pattern character vector
 * @param cg_missing single string
 * @param opts_regex list
 * @return character matrix
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-22)
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-06)
 *    new arg: cg_missing
 */
SEXP ci_match_last_regex(SEXP str, SEXP pattern, SEXP cg_missing, SEXP opts_regex)
{
    return ci__match_firstlast_regex(str, pattern, cg_missing, opts_regex, false);
}


/**
 * Extract all capture groups of  all occurrences of a regex pattern in each string
 *
 * @param str character vector
 * @param pattern character vector
 * @param opts_regex list
 * @param cg_missing single string
 * @return list of character matrices
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-22)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-27)
 *    FR #117: omit_no_match arg added
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-06)
 *    new arg: cg_missing
 *
 * @version 1.0-2 (Marek Gagolewski, 2016-01-29)
 *    Issue #214: allow a regex pattern like `.*`  to match an empty string
 *
 * @version 1.7.1 (Marek Gagolewski, 2021-06-19)
 *    #153: named capture groups
 */
SEXP ci_match_all_regex(SEXP str, SEXP pattern, SEXP omit_no_match, SEXP cg_missing, SEXP opts_regex)
{
    bool omit_no_match1 = ci__prepare_arg_logical_1_notNA(omit_no_match, "omit_no_match");
    PROTECT(str = ci__prepare_arg_string(str, "str")); // prepare string argument
    PROTECT(pattern = ci__prepare_arg_string(pattern, "pattern")); // prepare string argument
    PROTECT(cg_missing = ci__prepare_arg_string_1(cg_missing, "cg_missing"));
    UText* str_text = NULL; // may potentially be slower, but definitely is more convenient!
    STRI__ERROR_HANDLER_BEGIN(3)
    SEXP ret;
    {
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    R_len_t str_n = ci::checked_r_len(
        context.size(str), "character vectors"
    );
    R_len_t pattern_n = ci::checked_r_len(
        context.size(pattern), "character vectors"
    );
    R_len_t vectorize_length = 0;
    charport::unwind_protect([&]() -> SEXP {
        vectorize_length = ci__recycling_rule(
            false, 2, str_n, pattern_n
        );
        return R_NilValue;
    });
    // Deviation from stringi: queue the recycling warning until capture
    // staging, regex/Reader/UText owners, and native output storage are gone.
    if (vectorize_length > 0 &&
            (vectorize_length % str_n != 0 ||
             vectorize_length % pattern_n != 0))
        context.warn(MSG__WARN_RECYCLING_RULE);

    StriRegexMatcherOptions pattern_opts;
    charport::unwind_protect([&]() -> SEXP {
        pattern_opts = StriContainerRegexPattern::getRegexOptions(
            STRI__DEFERRED_WARNINGS, opts_regex
        );
        return R_NilValue;
    });

    vector<CiMatchRegexMatrix> results(
        static_cast<size_t>(vectorize_length)
    );
    {
        StriContainerUTF8 str_cont(
            context, str, vectorize_length
        );
        StriContainerRegexPattern pattern_cont(
            context, pattern, vectorize_length, pattern_opts
        );
        StriContainerUTF8 cg_missing_cont(
            context, cg_missing, 1
        );
        // The copied container validates cg_missing as UTF-8 input, but
        // stringi inserts the original value and therefore preserves its mark.
        shared_ptr<ci::ReaderBorrow> cg_missing_borrow =
            context.acquire(cg_missing);
        const charport::StrView cg_missing_value =
            cg_missing_borrow->views()[0];
        charport::charvec::Builder output(0);

        for (R_len_t i = pattern_cont.vectorize_init();
                i != pattern_cont.vectorize_end();
                i = pattern_cont.vectorize_next(i))
        {
            CiMatchRegexMatrix& result = results[static_cast<size_t>(i)];
            if ((pattern_cont).isNA(i) ||
                    (pattern_cont).get(i).length() <= 0) {
                if (!(pattern_cont).isNA(i))
                    context.warn(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
                result.nrow = 1;
                result.ncol = 1;
                output.reset(1);
                output.set_na(0);
                result.output = output.release_store();
                continue;
            }

            UErrorCode status = U_ZERO_ERROR;
            RegexMatcher *matcher = pattern_cont.getMatcher(i); // will be deleted automatically
            R_len_t pattern_cur_groups = matcher->groupCount();
            result.ncol = pattern_cur_groups+1;
            const vector<string>& capture_names =
                pattern_cont.getCaptureGroupNames(i);
            if (ci__match_has_capture_names(capture_names))
                result.capture_names = capture_names;

            if ((str_cont).isNA(i)) {
                result.nrow = 1;
                output.reset(result.ncol);
                ci__match_set_all_na(output, result.ncol);
                result.output = output.release_store();
                continue;
            }

            str_text = utext_openUTF8(
                str_text, str_cont.get(i).data(),
                str_cont.get(i).length(), &status
            );
            STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

            matcher->reset(str_text);

            deque< pair<R_len_t, R_len_t> > occurrences;
            while (1) {
                int m_res = (int)matcher->find(status);
                STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
                if (!m_res) break;

                occurrences.push_back(pair<R_len_t, R_len_t>(
                    (R_len_t)matcher->start(status),
                    (R_len_t)matcher->end(status)
                ));
                STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
                for (R_len_t j=0; j<pattern_cur_groups; ++j) {
                    occurrences.push_back(pair<R_len_t, R_len_t>(
                        (R_len_t)matcher->start(j+1, status),
                        (R_len_t)matcher->end(j+1, status)
                    ));
                }
                STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
            }

            R_len_t noccurrences = static_cast<R_len_t>(
                occurrences.size()/(pattern_cur_groups+1)
            );
            result.nrow = (noccurrences <= 0)?
                (omit_no_match1?0:1):noccurrences;
            R_xlen_t output_size = ci__match_matrix_size(
                result.nrow, result.ncol
            );
            output.reset(output_size);
            ci__match_set_all_na(output, output_size);
            if (noccurrences <= 0) {
                result.output = output.release_store();
                continue;
            }

            // Deviation from stringi: native output storage owns captured
            // bytes, and C++ owns capture names until the subject Reader ends.
            const char* str_cur_s = str_cont.get(i).data();
            deque< pair<R_len_t, R_len_t> >::iterator iter = occurrences.begin();
            for (R_len_t j = 0; iter != occurrences.end(); ++j) {
                pair<R_len_t, R_len_t> curo = *iter;
                ci::builder_set(
                    output, j, str_cur_s+curo.first,
                    curo.second-curo.first,
                    cetype_ext_t::CE_ASCII_OR_UTF8
                );
                ++iter;
                for (R_len_t k = 0;
                        iter != occurrences.end() && k < pattern_cur_groups;
                        ++iter, ++k) {
                    curo = *iter;
                    R_xlen_t output_i =
                        j+static_cast<R_xlen_t>(k+1)*noccurrences;
                    if (curo.first < 0 || curo.second < 0) {
                        ci__match_set_cg_missing(
                            output, output_i, cg_missing_value
                        );
                    }
                    else {
                        ci::builder_set(
                            output, output_i,
                            str_cur_s+curo.first, curo.second-curo.first,
                            cetype_ext_t::CE_ASCII_OR_UTF8
                        );
                    }
                }
            }
            result.output = output.release_store();
        }

        if (str_text) {
            utext_close(str_text);
            str_text = NULL;
        }
    }

    STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
        return Rf_allocVector(VECSXP, vectorize_length);
    }));
    // Deviation from stringi: one assembly-level unwind bridge releases the
    // staged Stores and capture names if an R allocation fails. A bridge for
    // every matrix and attribute assignment only adds per-result overhead.
    charport::unwind_protect([&]() -> SEXP {
      for (R_len_t i=0; i<vectorize_length; ++i) {
          ci::UnwindCallbackProtector protector;
          CiMatchRegexMatrix& result = results[static_cast<size_t>(i)];
          SEXP cur_res;
          cur_res = protector.protect(charport::charvec::wrap(
              std::move(result.output)
          ));
          SEXP dimnames;
          dimnames = protector.protect(ci__match_capture_dimnames(
              result.capture_names
          ));
          ci__match_set_matrix_attributes(
                cur_res, result.nrow, result.ncol, dimnames
          );
          SET_VECTOR_ELT(ret, i, cur_res);
      }
      return R_NilValue;
    });
    }
    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(if (str_text) utext_close(str_text);)
}
