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
#include "ci_utf8.h"
#include "ci_container_utf16.h"
#include "ci_container_usearch.h"
#include "ci_container_bytesearch.h"
#include "ci_container_integer.h"
#include "ci_container_logical.h"
#include "../read_lines_common.h"
#include <string>
#include <vector>
#include <unicode/brkiter.h>
#include <unicode/rbbi.h>
namespace charr { namespace base {

using namespace std;


SEXP ci_read_lines(SEXP path, SEXP encoding)
{
    PROTECT(path = ci__prepare_arg_string_1(path, "con"));
    PROTECT(encoding = ci__prepare_arg_string_1(encoding, "encoding"));

    STRI__ERROR_HANDLER_BEGIN(2)
    SEXP ret = R_NilValue;
    bool file_failed = false;
    charr::read_lines::FileCondition file_condition =
        charr::read_lines::FileCondition::open_failed;
    int file_errno = 0;
    size_t invalid_warning_count = 0;
    char invalid_warning[256] = {0};
    try {
        SEXP path_string = STRING_ELT(path, 0);
        if (path_string == NA_STRING)
            throw StriException("invalid 'description' argument");
        if (STRING_ELT(encoding, 0) == NA_STRING)
            throw StriException("invalid 'encoding' value");

        {
        const char* original_path = Rf_translateChar(path_string);
        const char* expanded = R_ExpandFileName(original_path);
        const string expanded_path(expanded);
        vector<char> bytes;
        try {
            bytes = charr::read_lines::read_file(expanded_path.c_str());
        }
        catch (const charr::read_lines::FileConditionError& error) {
            file_failed = true;
            file_condition = error.condition();
            file_errno = error.error();
        }

        if (file_failed) {
            bytes.clear();
        }
        else {

            const char* utf8 = bytes.empty() ? "" : bytes.data();
            int utf8_length = static_cast<int>(bytes.size());
            if (charr::read_lines::has_utf8_bom(utf8, utf8_length)) {
                utf8 += 3;
                utf8_length -= 3;
            }

            charr::read_lines::ScanResult scan =
                charr::read_lines::scan_utf8(utf8, utf8_length, false);
            if (scan.embedded_nul)
                throw StriException("embedded nul in string");

            vector<char> repaired;
            if (!scan.invalid.empty()) {
                const string warning = charr::read_lines::invalid_warning(
                    utf8, scan.invalid[0]
                );
                std::snprintf(
                    invalid_warning, sizeof(invalid_warning),
                    "%s", warning.c_str()
                );
                invalid_warning_count = scan.invalid.size();
                repaired = charr::read_lines::repair_utf8(
                    utf8, utf8_length, scan.invalid
                );
                utf8 = repaired.empty() ? "" : repaired.data();
                utf8_length = static_cast<int>(repaired.size());
                scan = charr::read_lines::scan_utf8(
                    utf8, utf8_length, false
                );
            }

            STRI__PROTECT(ret = Rf_allocVector(
                STRSXP, static_cast<R_xlen_t>(scan.lines.size())
            ));
            for (size_t i=0; i<scan.lines.size(); ++i) {
                const charr::read_lines::LineSlice& line = scan.lines[i];
                SET_STRING_ELT(
                    ret, static_cast<R_xlen_t>(i),
                    Rf_mkCharLenCE(
                        utf8+line.begin, line.length, CE_UTF8
                    )
                );
            }
        }
        }

        if (file_failed) {
            const char* description = Rf_translateChar(path_string);
            if (file_condition ==
                    charr::read_lines::FileCondition::directory) {
                Rf_warning(
                    "'raw = FALSE' but '%s' is not a regular file",
                    description
                );
                Rf_warning(
                    "cannot open file '%s': it is a directory", description
                );
            }
            else {
                Rf_warning(
                    "cannot open file '%s': %s", description,
                    std::strerror(file_errno)
                );
            }
            throw StriException("cannot open the connection");
        }

        for (size_t i=0; i<invalid_warning_count; ++i)
            Rf_warning("%s", invalid_warning);
    }
    catch (const StriException&) {
        throw;
    }
    catch (const std::exception& error) {
        throw StriException("%s", error.what());
    }

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;)
}


/**
 * Split a single string into text lines
 *
 * @param str character vector
 *
 * @return character vector
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-04)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci_split_lines1(SEXP str)
{
    PROTECT(str = ci__prepare_arg_string_1(str, "str"));
    R_len_t vectorize_length = LENGTH(str);

    STRI__ERROR_HANDLER_BEGIN(1)
    Utf8Input str_cont(str, vectorize_length);

    if (str_cont.isNA(0)) {
        STRI__UNPROTECT_ALL
        return str;
    }

    const char* str_cur_s = str_cont.get(0).data();
    R_len_t str_cur_n = str_cont.get(0).length();

    charr::read_lines::ScanResult scan =
        charr::read_lines::scan_utf8(str_cur_s, str_cur_n, false);

    SEXP ans;
    STRI__PROTECT(ans = Rf_allocVector(
        STRSXP, static_cast<R_xlen_t>(scan.lines.size())
    ));
    for (size_t i=0; i<scan.lines.size(); ++i) {
        const charr::read_lines::LineSlice& line = scan.lines[i];
        SET_STRING_ELT(
            ans, static_cast<R_xlen_t>(i),
            Rf_mkCharLenCE(str_cur_s+line.begin, line.length, CE_UTF8)
        );
    }
    STRI__UNPROTECT_ALL
    return ans;

    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}


/**
 * Split a string into text lines
 *
 * @param str character vector
 * @param omit_empty logical vector
 *
 * @return list of character vectors
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-08-04)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-10-30)
 *                removed `n_max` arg, as it doesn't make sense
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-05)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci_split_lines(SEXP str, SEXP omit_empty)
{
    PROTECT(str = ci__prepare_arg_string(str, "str"));
//   n_max = ci__prepare_arg_integer(n_max, "n_max");
    PROTECT(omit_empty = ci__prepare_arg_logical(omit_empty, "omit_empty"));
    R_len_t vectorize_length = ci__recycling_rule(true, 2, LENGTH(str), /*LENGTH(n_max), */LENGTH(omit_empty));

    STRI__ERROR_HANDLER_BEGIN(2)
    Utf8Input str_cont(str, vectorize_length);
//   StriContainerInteger   n_max_cont(n_max, vectorize_length);
    StriContainerLogical   omit_empty_cont(omit_empty, vectorize_length);

    SEXP ret;
    STRI__PROTECT(ret = Rf_allocVector(VECSXP, vectorize_length));

    for (R_len_t i = str_cont.vectorize_init();
            i != str_cont.vectorize_end();
            i = str_cont.vectorize_next(i))
    {
        if (str_cont.isNA(i)) {
            SET_VECTOR_ELT(ret, i, ci__vector_NA_strings(1));
            continue;
        }

        const char* str_cur_s = str_cont.get(i).data();
        R_len_t str_cur_n = str_cont.get(i).length();
//      int  n_max_cur        = n_max_cont.get(i);
        int  omit_empty_cur   = omit_empty_cont.get(i);

//      if (n_max_cur < 0)
//         n_max_cur = INT_MAX;
//      else if (n_max_cur == 0) {
//         SET_VECTOR_ELT(ret, i, Rf_allocVector(STRSXP, 0));
//         continue;
//      }

//#define STRI_INDEX_NEWLINE_CR   0
//#define STRI_INDEX_NEWLINE_LF   1
//#define STRI_INDEX_NEWLINE_CRLF 2
//#define STRI_INDEX_NEWLINE_NEL  3
//#define STRI_INDEX_NEWLINE_VT   4
//#define STRI_INDEX_NEWLINE_FF   5
//#define STRI_INDEX_NEWLINE_LS   6
//#define STRI_INDEX_NEWLINE_PS   7
//#define STRI_INDEX_NEWLINE_LAST 8

//      int counts[STRI_INDEX_NEWLINE_LAST];
//      for (R_len_t j=0; j<STRI_INDEX_NEWLINE_LAST; ++j)
//         counts[j] = 0;

        charr::read_lines::ScanResult scan =
            charr::read_lines::scan_utf8(
                str_cur_s, str_cur_n, omit_empty_cur != 0,
                true
            );

        SEXP ans;
        STRI__PROTECT(ans = Rf_allocVector(
            STRSXP, static_cast<R_xlen_t>(scan.lines.size())
        ));

        for (size_t j=0; j<scan.lines.size(); ++j) {
            const charr::read_lines::LineSlice& line = scan.lines[j];
            SET_STRING_ELT(
                ans, static_cast<R_xlen_t>(j),
                Rf_mkCharLenCE(
                    str_cur_s+line.begin, line.length, CE_UTF8
                )
            );
        }

        SET_VECTOR_ELT(ret, i, ans);
        STRI__UNPROTECT(1);
    }

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}

} } // namespace charr::base
