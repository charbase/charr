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
#include "ci_container_utf8.h"
#include "ci_container_integer.h"
#include "ci_container_double.h"
#include "ci_builder.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>


#define STRI_SPRINTF_NOT_PROVIDED (NA_INTEGER+1)  /* -2**31+2 */

#define STRI_SPRINTF_SPEC_INTEGER "dioxX"
#define STRI_SPRINTF_SPEC_DOUBLE "feEgGaA"
#define STRI_SPRINTF_SPEC_STRING "s"

#define STRI_SPRINTF_SPEC_TYPE ( \
    STRI_SPRINTF_SPEC_INTEGER    \
    STRI_SPRINTF_SPEC_DOUBLE     \
    STRI_SPRINTF_SPEC_STRING     \
)

#define STRI_SPRINTF_FLAGS "-+ 0#"
// TODO: Single UNIX Specification has "'" flag too, we can use it for formatting with ICU

#define STRI_SPRINTF_ACCEPTED_CHARS ( \
    STRI_SPRINTF_SPEC_INTEGER         \
    STRI_SPRINTF_SPEC_DOUBLE          \
    STRI_SPRINTF_SPEC_STRING          \
    STRI_SPRINTF_FLAGS                \
    ".*$"                             \
    "0123456789"                      \
)


/** data types for sprintf
 *
 * @version 1.6.2 (Marek Gagolewski, 2021-05-24)
 */
typedef enum {
    STRI_SPRINTF_TYPE_UNDEFINED=0,
    STRI_SPRINTF_TYPE_INTEGER,
    STRI_SPRINTF_TYPE_DOUBLE,
    STRI_SPRINTF_TYPE_STRING,
} StriSprintfType;


/** data types for sprintf
 *
 * @version 1.6.2 (Marek Gagolewski, 2021-05-24)
 */
typedef enum {
    STRI_SPRINTF_FORMAT_STATUS_OK=0,
    STRI_SPRINTF_FORMAT_STATUS_IS_NA,
    STRI_SPRINTF_FORMAT_STATUS_NEEDS_PADDING
} StriSprintfFormatStatus;


/**
 * if delim found, stops right after delim, modifies jc in place
 * if delim not found, returns STRI_SPRINTF_NOT_PROVIDED or throws an error
 * ignores leading 0s
 * non-negative values only
 *
 * @version 1.6.2 (Marek Gagolewski, 2021-05-24)
 * @version 1.6.3 (Marek Gagolewski, 2021-06-10)
 *     return STRI_SPRINTF_NOT_PROVIDED instead of NA_INTEGER
 */
int ci__atoi_to_delim(
    const char* f,
    R_len_t& jc,
    R_len_t j0,
    R_len_t j1,
    char delim,
    bool throw_error=true,
    int max_val=99999
) {
    R_len_t j = jc;
    STRI_ASSERT(j0 <= j && j <= j1)

    if (f[j] < '0' || f[j] > '9')
        throw StriException(
            MSG__INVALID_FORMAT_SPECIFIER_SUB "; " MSG__EXPECTED_NONNEGATIVE,
            j1-j0+1, f+j0);

    int val = (int)f[j++]-(int)'0';

    while (true) {
        if (f[j] == delim) { j++; break; }

        if (j >= j1 || f[j] < '0' || f[j] > '9') {
            if (throw_error)
                throw StriException(
                    MSG__INVALID_FORMAT_SPECIFIER_SUB, // TODO: error details
                    j1-j0+1, f+j0);
            else
                return STRI_SPRINTF_NOT_PROVIDED;
        }

        val = val*10 + ((int)f[j++]-(int)'0');  // this ignores leading 0s
        if (val > max_val)
            throw StriException(
                MSG__INVALID_FORMAT_SPECIFIER_SUB "; " MSG__EXPECTED_SMALLER,
                j1-j0+1, f+j0);
    }

    // found.
    jc = j;  // passed by reference
    return val;
}


/**
 * stops at a non-digit, modifies jc in place
 * ignores leading 0s
 * non-negative values only
 *
 * @version 1.6.2 (Marek Gagolewski, 2021-05-24)
 */
int ci__atoi_to_other(const char* f, R_len_t& jc, R_len_t j0, R_len_t j1, int max_val=99999)
{
    STRI_ASSERT(j0 <= jc && jc < j1)

    if (f[jc] < '0' || f[jc] > '9')
        throw StriException(
            MSG__INVALID_FORMAT_SPECIFIER_SUB "; " MSG__EXPECTED_NONNEGATIVE,
            j1-j0+1, f+j0);

    int val = (int)f[jc++]-(int)'0';

    while (jc < j1) {
        if (f[jc] < '0' || f[jc] > '9')
            break;

        val = val*10 + ((int)f[jc++]-(int)'0');  // this ignores leading 0s
        if (val > max_val)
            throw StriException(
                MSG__INVALID_FORMAT_SPECIFIER_SUB "; " MSG__EXPECTED_SMALLER,
                j1-j0+1, f+j0);
    }
    return val;
}


/**
 * preflight - get something which possibly is a format spec
 * throws an error on any chars outside of [0-9*$. +0#-]
 *
 * @returns index of the first char in STRI_SPRINTF_SPEC_TYPE
 *
 * @version 1.6.2 (Marek Gagolewski, 2021-05-24)
 */
int ci__find_type_spec(const char* f, R_len_t j0, R_len_t n)
{
    R_len_t j1 = j0;
    STRI_ASSERT(f[j0-1] == '%');
    while (true) {
        if (j1 >= n) {
            // Deviation from stringi: String8 has no terminator, so bound the
            // diagnostic by the remaining bytes in this format record.
            throw StriException(
                MSG__INVALID_FORMAT_SPECIFIER_SUB, n-j0, f+j0
            ); // dangling %...
        } else if (strchr(STRI_SPRINTF_SPEC_TYPE, f[j1]) != nullptr)
            break;
        else if (strchr(STRI_SPRINTF_FLAGS, f[j1]) != nullptr)
            ;
        else if (f[j1] == '*' || f[j1] == '$' || f[j1] == '.')
            ;
        else if (f[j1] >= '0' && f[j1] <= '9')
            ;
        else {
            throw StriException(
                MSG__INVALID_FORMAT_SPECIFIER_SUB "; " MSG__EXPECTED_CHAR_IN_SET,
                n-j0, f+j0, STRI_SPRINTF_ACCEPTED_CHARS
            );
        }

        j1++;
    }
    return j1;
}


/** An owned, recyclable vector of normalized UTF-8 records. */
class StriSprintfOwnedStrings
{
private:
    std::vector<String8> values;
    R_len_t nrecycle;

public:
    StriSprintfOwnedStrings() : values(), nrecycle(0) {}

    void assign(
        const StriContainerUTF8& source, R_len_t n, R_len_t recycle_length
    )
    {
        std::vector<String8> replacement(static_cast<size_t>(n));
        for (R_len_t i=0; i<n; ++i)
            replacement[static_cast<size_t>(i)].assignOwned(source.getNAble(i));
        values.swap(replacement);
        nrecycle = n > 0 ? recycle_length : 0;
    }

    const String8& getNAble(R_len_t i) const
    {
        if (values.empty())
            throw std::runtime_error("cannot recycle an empty string vector");
        return values[static_cast<size_t>(i)%values.size()];
    }

    R_len_t vectorize_init() const
    {
        return values.empty() ? nrecycle : 0;
    }

    R_len_t vectorize_end() const
    {
        return nrecycle;
    }

    R_len_t vectorize_next(R_len_t i) const
    {
        const R_len_t n = static_cast<R_len_t>(values.size());
        if (i == nrecycle-1-(nrecycle%n))
            return nrecycle;
        i += n;
        if (i >= nrecycle)
            return (i%n)+1;
        return i;
    }
};


/** Enables the fetching of the i-th/next integer/real/string datum from `...`.
 *
 * @version 1.6.2 (Marek Gagolewski, 2021-05-24)
 * @version 1.7.4 (Marek Gagolewski, 2021-08-05)
 *     #449: segfaults; protect lazily coerced objects
 */
class StriSprintfDataProvider
{
private:
    SEXP x;      // protected outside
    SEXP cache;  // protected outside; integer/double/string slots per argument
    R_len_t narg;
    R_len_t vectorize_length;
    ci::DeferredWarnings& warnings;
    std::vector< std::unique_ptr<StriContainerInteger> > x_integer;
    std::vector< std::unique_ptr<StriContainerDouble> > x_double;
    std::vector< std::unique_ptr<StriSprintfOwnedStrings> > x_string;
    std::vector<unsigned char> used;
    R_len_t cur_elem;  // 0..vectorize_length-1
    R_len_t cur_item;  // 0..narg-1

public:
    StriSprintfDataProvider(
        SEXP x, SEXP cache, R_len_t narg, R_len_t vectorize_length,
        ci::DeferredWarnings& warnings
    ) :
        x(x),
        cache(cache),
        narg(narg),
        vectorize_length(vectorize_length),
        warnings(warnings),
        x_integer(static_cast<size_t>(narg)),
        x_double(static_cast<size_t>(narg)),
        x_string(static_cast<size_t>(narg)),
        used(static_cast<size_t>(narg), 0),
        cur_elem(-1),
        cur_item(0)
    {
    }

    R_len_t countUnused() const
    {
        R_len_t num_unused = 0;
        for (R_len_t j=0; j<narg; ++j)
            if (!used[static_cast<size_t>(j)])
                ++num_unused;
        return num_unused;
    }

    void reset(R_len_t elem)
    {
        cur_elem = elem;
        cur_item = 0;
    }

    /** Gets the next (i negative) or the i-th integer datum
     *  Can be NA, so check with ... == NA_INTEGER.
     *
     *  i == STRI_SPRINTF_NOT_PROVIDED 0 means "get next unconsumed"
     */
    int getIntegerOrNA(int i=STRI_SPRINTF_NOT_PROVIDED)
    {
        if (i == STRI_SPRINTF_NOT_PROVIDED) i = (cur_item++);
        // else do not advance cur_item

        if (i < 0) throw StriException(MSG__EXPECTED_LARGER);
        else if (i >= narg) throw StriException(MSG__ARG_NEED_MORE);

        const size_t index = static_cast<size_t>(i);
        if (!x_integer[index]) {
            SEXP y = charport::unwind_protect([&]() -> SEXP {
                SEXP prepared = PROTECT(ci__prepare_arg_integer(
                    VECTOR_ELT(x, i), "...",
                    false/*factors_as_strings*/, false/*allow_error*/,
                    &warnings
                ));
                if (prepared != R_NilValue)
                    SET_VECTOR_ELT(cache, i, prepared);
                UNPROTECT(1);
                return prepared;
            });
            if (y == R_NilValue)
                throw StriException(MSG__ARG_EXPECTED_INTEGER, "...");
            charport::unwind_protect([&]() -> SEXP {
                x_integer[index].reset(
                    new StriContainerInteger(y, vectorize_length)
                );
                return R_NilValue;
            });
            used[index] = 1;
        }

        return x_integer[index]->getNAble(cur_elem);
    }

    /** Gets the next (i negative) or the i-th real datum;
     *  Can be NA, so check with ISNA(...).
     *
     *  i == STRI_SPRINTF_NOT_PROVIDED means "get next unconsumed"
     */
    double getDoubleOrNA(int i=STRI_SPRINTF_NOT_PROVIDED)
    {
        if (i == STRI_SPRINTF_NOT_PROVIDED) i = (cur_item++);
        // else do not advance cur_item

        if (i < 0) throw StriException(MSG__EXPECTED_LARGER);
        else if (i >= narg) throw StriException(MSG__ARG_NEED_MORE);

        const size_t index = static_cast<size_t>(i);
        if (!x_double[index]) {
            SEXP y = charport::unwind_protect([&]() -> SEXP {
                SEXP prepared = PROTECT(ci__prepare_arg_double(
                    VECTOR_ELT(x, i), "...",
                    false/*factors_as_strings*/, false/*allow_error*/,
                    &warnings
                ));
                if (prepared != R_NilValue)
                    SET_VECTOR_ELT(
                        cache, static_cast<R_xlen_t>(narg)+i, prepared
                    );
                UNPROTECT(1);
                return prepared;
            });
            if (y == R_NilValue)
                throw StriException(MSG__ARG_EXPECTED_NUMERIC, "...");
            std::unique_ptr<StriContainerDouble> prepared(
                new StriContainerDouble(y, vectorize_length)
            );
            x_double[index] = std::move(prepared);
            used[index] = 1;
        }

        return x_double[index]->getNAble(cur_elem);
    }

    /** Gets the next (i negative) or the i-th real datum
     *  Can be NA, so check with ....isNA().
     *
     *  i == STRI_SPRINTF_NOT_PROVIDED means "get next unconsumed"
     */
    const String8& getStringOrNA(int i=STRI_SPRINTF_NOT_PROVIDED)
    {
        if (i == STRI_SPRINTF_NOT_PROVIDED) i = (cur_item++);
        // else do not advance cur_item

        if (i < 0) throw StriException(MSG__EXPECTED_LARGER);
        else if (i >= narg) throw StriException(MSG__ARG_NEED_MORE);

        const size_t index = static_cast<size_t>(i);
        if (!x_string[index]) {
            SEXP y = charport::unwind_protect([&]() -> SEXP {
                SEXP prepared = PROTECT(ci__prepare_arg_string(
                    VECTOR_ELT(x, i), "...", false/*allow_error*/,
                    &warnings
                ));
                if (prepared != R_NilValue)
                    SET_VECTOR_ELT(
                        cache, 2*static_cast<R_xlen_t>(narg)+i, prepared
                    );
                UNPROTECT(1);
                return prepared;
            });
            if (y == R_NilValue)
                throw StriException(MSG__ARG_EXPECTED_STRING, "...");

            std::unique_ptr<StriSprintfOwnedStrings> owned(
                new StriSprintfOwnedStrings
            );
            {
                // Deviation from stringi: a lazily coerced string argument is
                // normalized through a short Reader, then copied so later
                // coercions cannot overlap a live character borrow.
                ci::ReaderContext context(warnings);
                const R_len_t length = ci::checked_r_len(
                    context.size(y), "character vectors"
                );
                StriContainerUTF8 source(context, y, length);
                owned->assign(source, length, vectorize_length);
            }
            x_string[index] = std::move(owned);
            used[index] = 1;
        }

        return x_string[index]->getNAble(cur_elem);
    }
};


/** Parses and stores info on a single sprintf format (conversion) specifier
 *
 * @version 1.6.2 (Marek Gagolewski, 2021-05-24)
 * @version 1.6.3 (Marek Gagolewski, 2021-06-10)
 *     distinguish between NA_INTEGER and STRI_SPRINTF_NOT_PROVIDED
 */
class StriSprintfFormatSpec
{
private:
    StriSprintfDataProvider* data;
    const String8& na_string;
    const String8& inf_string;
    const String8& nan_string;
    bool use_length;

    StriSprintfType type;
    char type_spec;

    int which_datum;       // can be STRI_SPRINTF_NOT_PROVIDED (== consume next datum)

    // see normalise() for info on which options are mutually exclusive etc.
    bool pad_from_right;   // '-'
    bool pad_zero;         // '0'
    bool sign_space;       // ' '
    bool sign_plus;        // '+'
    bool alternate_output; // '#'
    int min_width;         // can be NA_INTEGER or STRI_SPRINTF_NOT_PROVIDED
    int precision;         // can be NA_INTEGER or STRI_SPRINTF_NOT_PROVIDED or negative (but then like '-')
    // TODO: flag "'" -- localised formatting with ICU


public:
    StriSprintfFormatSpec(
        const char* f,
        R_len_t j0,
        R_len_t j1,
        StriSprintfDataProvider* data,
        const String8& na_string,
        const String8& inf_string,
        const String8& nan_string,
        bool use_length
    ) :
        data(data),
        na_string(na_string),
        inf_string(inf_string),
        nan_string(nan_string),
        use_length(use_length)
    {
        // f[j0..j1] may be a format specifier (without the preceding %)
        // %<DATUM INDEX$><FLAGS><FIELD WIDTH><.PRECISION><CONVERSION SPECIFIER>
        //       1            2        3           4            5 == f[j1]

        STRI_ASSERT(f[j0-1] == '%')
        type_spec = f[j1];

        if (strchr(STRI_SPRINTF_SPEC_INTEGER, type_spec) != nullptr)
            type = STRI_SPRINTF_TYPE_INTEGER;
        else if (strchr(STRI_SPRINTF_SPEC_DOUBLE, type_spec) != nullptr)
            type = STRI_SPRINTF_TYPE_DOUBLE;
        else
            type = STRI_SPRINTF_TYPE_STRING;

        pad_from_right = false;
        pad_zero = false;
        sign_space = false;
        sign_plus = false;
        alternate_output = false;
        min_width = STRI_SPRINTF_NOT_PROVIDED;
        precision = STRI_SPRINTF_NOT_PROVIDED;
        // eEfFgG - default precision = 6
        // aA - default precision = depends on the input
        // dioxX - default precision = 1
        // s - default precision - unspecified

        // gG uses eE if precision <= exponent < -4

        R_len_t jc = j0;

        // 1. optional [0-9]*\$  - which datum is to be formatted?
        which_datum = STRI_SPRINTF_NOT_PROVIDED;
        if (f[jc] >= '0' && f[jc] <= '9') { // trailing 0s will be ignored
            // arg pos spec if digits followed by '$'
            // we can also have '0' flag at this pos, but this will not be
            // followed by '$' and the call below will return NA_INTEGER
            which_datum = ci__atoi_to_delim(
                f, /*by reference*/jc, j0, j1, /*delimiter*/'$', false/*throw_error*/
            );
            // result can be < 0; incorrect indexes will be caught by get*
            if (which_datum != STRI_SPRINTF_NOT_PROVIDED) which_datum--; /*0-based indexing*/
        }

        // 2. optional flags [ +0#-]
        while (true) {
            if      (f[jc] == ' ') sign_space       = true;
            else if (f[jc] == '+') sign_plus        = true;
            else if (f[jc] == '0') pad_zero         = true;
            else if (f[jc] == '-') pad_from_right   = true;
            else if (f[jc] == '#') alternate_output = true;
            else break;
            jc++;
        }

        // 3. optional field width: none / 123 / * / *0123$
        if (f[jc] >= '1' && f[jc] <= '9') {  // note that 0 is treated above
            min_width = ci__atoi_to_other(f, /*by reference*/jc, j0, j1);
        }
        else if (f[jc] == '*') {  // take from ... args
            jc++;
            int which_width = STRI_SPRINTF_NOT_PROVIDED;
            if (f[jc] >= '0' && f[jc] <= '9') {
                which_width = ci__atoi_to_delim(
                    f, /*by reference*/jc, j0, j1, /*delimiter*/'$'
                );
                if (which_width != STRI_SPRINTF_NOT_PROVIDED) which_width--; /*0-based indexing*/
            }
            min_width = data->getIntegerOrNA(which_width);
        }
        // else if . -- treated below
        // else if type spec like dfgxo -- treated below
        // else probably an error, will be caught below

        // 4. optional field precision: none / .0123 / . / .* / .0123$
        if (f[jc] == '.') {
            jc++;
            if (jc == j1) {
                // precision "." is ".0"
                precision = 0;
            }
            if (f[jc] >= '0' && f[jc] <= '9') {  // trailing 0s will be ignored
                precision = ci__atoi_to_other(f, /*by reference*/jc, j0, j1);
            }
            else if (f[jc] == '*') {  // take from ... args
                jc++;
                int which_precision = STRI_SPRINTF_NOT_PROVIDED;
                if (f[jc] >= '0' && f[jc] <= '9') {
                    which_precision = ci__atoi_to_delim(
                        f, /*by reference*/jc, j0, j1, /*delimiter*/'$'
                    );
                    if (which_precision != STRI_SPRINTF_NOT_PROVIDED) which_precision--; /*0-based indexing*/
                }
                precision = data->getIntegerOrNA(which_precision);
            }
            // else error, exception thrown below
        }

        // now we should be at the conversion specifier
        if (jc != j1)
            throw StriException(MSG__INVALID_FORMAT_SPECIFIER_SUB, j1-j0+1, f+j0);

        normalise();
    }


    std::string getFormatString(bool use_sign=true, bool use_pad=true)
    {
        // note that trimming based on width/length is done elsewhere
        normalise();
        std::string f("%");
        if (alternate_output) f.push_back('#');
        if (use_sign && sign_space) f.push_back(' ');
        if (use_sign && sign_plus) f.push_back('+');
        if (use_pad && pad_from_right) f.push_back('-');
        if (use_pad && pad_zero) f.push_back('0');
        if (use_pad && min_width > 0)   // and hence not STRI_SPRINTF_NOT_PROVIDED or NA_INTEGER
            f.append(std::to_string(min_width));
        if (precision >= 0) {  // and hence not STRI_SPRINTF_NOT_PROVIDED or NA_INTEGER
            f.push_back('.');
            f.append(std::to_string(precision));
        }
        f.push_back(type_spec);
        return f;
    }


    void normalise()
    {
        if (type_spec == 'i')
            type_spec = 'd';  // synonym

        // TODO: warnings when switching off the flags?

        if (min_width == NA_INTEGER)
            ;
        else if (min_width == STRI_SPRINTF_NOT_PROVIDED)
            ;
        else if (min_width == 0)
            min_width = STRI_SPRINTF_NOT_PROVIDED;
        else if (min_width < 0) {
            min_width = -min_width;
            pad_from_right = true;
        }

        if (precision == NA_INTEGER)
            ;
        else if (precision == STRI_SPRINTF_NOT_PROVIDED)
            ;
        else if (precision < 0)
            precision = STRI_SPRINTF_NOT_PROVIDED;

        if (pad_from_right)
            pad_zero = false;

        if (sign_plus)
            sign_space = false;

        if (type == STRI_SPRINTF_TYPE_STRING) {
            pad_zero = false;    // [-Wformat=] even warns about this
            sign_plus = false;   // [-Wformat=] even warns about this
            sign_space = false;  // [-Wformat=] even warns about this
            alternate_output = false;
            // precision = maximum width/length, yes, we support it.
        }
        else if (type == STRI_SPRINTF_TYPE_INTEGER) {
            // precision -- minimal number of digits that must appear

            if (type_spec != 'd') {  // and not i, because i->d
                sign_plus = false;   // [-Wformat=] even warns about this
                sign_space = false;  // [-Wformat=] even warns about this
            }
        }
    }


    StriSprintfFormatStatus formatDatum(std::string& preformatted_datum)
    {
        StriSprintfFormatStatus status;
        if (type == STRI_SPRINTF_TYPE_INTEGER) {
            int datum = data->getIntegerOrNA(which_datum);
            status = preformatDatum_doxX(preformatted_datum/*by reference*/, datum);
        }
        else if (type == STRI_SPRINTF_TYPE_DOUBLE) {
            double datum = data->getDoubleOrNA(which_datum);
            status = preformatDatum_feEgGaA(preformatted_datum/*by reference*/, datum);
        }
        else { // string
            const String8& datum = data->getStringOrNA(which_datum);
            status = preformatDatum_s(preformatted_datum, datum);
        }

        if (status != STRI_SPRINTF_FORMAT_STATUS_NEEDS_PADDING)
            return status;

        if (min_width <= 0)  // includes NA_INTEGER and STRI_SPRINTF_NOT_PROVIDED
            return STRI_SPRINTF_FORMAT_STATUS_OK;  // no trimming needed

        STRI_ASSERT(min_width > 0);

        R_len_t datum_size;
        const char* datum_data = preformatted_datum.empty() ?
            "" : preformatted_datum.data();
        if (use_length)  // number of code points
            datum_size = ci__length_string(
                datum_data, static_cast<R_len_t>(preformatted_datum.length())
            );
        else
            datum_size = ci__width_string(
                datum_data, static_cast<R_len_t>(preformatted_datum.length())
            );

        if (datum_size < min_width) {
            // now we need to pad with spaces from left or right up to min_width
            // based on width or length (use_length)

            // btw: pad_from_right always add spaces
            // btw: pad_zero   "-00000" "+00000" " 00000" "0x0000" "0X0000"
            //     but not NA/Inf/... and only numerics,
            //     and this needs_padding no more (already dealt with)

            if (pad_from_right)
                preformatted_datum.append(min_width-datum_size, ' ');
            else
                preformatted_datum.assign(std::string(min_width-datum_size, ' ') + preformatted_datum);
        }

        return STRI_SPRINTF_FORMAT_STATUS_OK;
    }


private:

    StriSprintfFormatStatus preformatDatum_doxX(std::string& preformatted_datum, int datum)
    {
        STRI_ASSERT(type_spec != 'i');  // normalised i->d
        bool isna = (datum == NA_INTEGER || min_width == NA_INTEGER || precision == NA_INTEGER);
        if (!isna) {
            size_t bufsize = static_cast<size_t>(std::max(0, min_width));
            bufsize += std::max(0, precision);
            bufsize += 128; // "just in case"  (0x, sign, dot, and stuff)
            std::vector<char> buf(bufsize);

            // oh, oh, oh, so lazy, using std::snprintf (good enough)
            // TODO: use ICU NumberFormat for '%d' (locale dependent) when "'" flag is set
            std::string format_string = getFormatString();
            // Deviation from stringi: std::snprintf is the one genuine
            // C-string consumer here because its generated format parameter
            // must be terminated. Copy its fixed-size result by byte count.
            int written = std::snprintf(
                buf.data(), buf.size(), format_string.c_str(), datum
            );
            size_t result_size = (written < 0)?0:static_cast<size_t>(written);
            if (result_size >= buf.size()) result_size = buf.size()-1;
            preformatted_datum.append(buf.data(), result_size);

            return STRI_SPRINTF_FORMAT_STATUS_OK;  /* all in ASCII, padding done by std::snprintf */
        }
        else if (na_string.isNA())
            return STRI_SPRINTF_FORMAT_STATUS_IS_NA;
        else {
            STRI_ASSERT(type_spec == 'd' || !sign_plus);
            STRI_ASSERT(type_spec == 'd' || !sign_space);

            if (sign_plus) {
                // glibc produces "+nan", but we will output " nan" instead
                preformatted_datum.push_back(' ');
            }
            else if (sign_space)
                preformatted_datum.push_back(' ');
            // else no sign

            STRI_ASSERT(!na_string.isNA());
            preformatted_datum.append(
                na_string.data(), static_cast<size_t>(na_string.length())
            );
            return STRI_SPRINTF_FORMAT_STATUS_NEEDS_PADDING;  /* might need padding (na_string can be fancy Unicode) */
        }
    }


    StriSprintfFormatStatus preformatDatum_feEgGaA(std::string& preformatted_datum, double datum)
    {
        bool isna = (ISNA(datum) || min_width == NA_INTEGER || precision == NA_INTEGER);
        if (R_FINITE(datum) && !isna) {
            size_t bufsize = static_cast<size_t>(std::max(0, min_width));
            bufsize += std::max(0, precision);
            bufsize += 128; // "just in case"  (0x, sign, dot, and stuff)
            std::vector<char> buf(bufsize);

            // lazybones, using std::sprintf (the good-enough approach)
            // TODO: use ICU NumberFormat for '%feEgG' (locale dependent) when "'" flag is set
            std::string format_string = getFormatString();
            // Same deliberate generated-format C-string boundary and
            // explicit-length fixed-result copy as the integer path above.
            int written = std::snprintf(
                buf.data(), buf.size(), format_string.c_str(), datum
            );
            size_t result_size = (written < 0)?0:static_cast<size_t>(written);
            if (result_size >= buf.size()) result_size = buf.size()-1;
            preformatted_datum.append(buf.data(), result_size);

            return STRI_SPRINTF_FORMAT_STATUS_OK;  /* all in ASCII, padding done by std::snprintf */
        }
        else if (
            (na_string.isNA() && isna) ||
            (nan_string.isNA() && ISNAN(datum)) ||
            (inf_string.isNA() && std::isinf(datum))
        ) {
            return STRI_SPRINTF_FORMAT_STATUS_IS_NA;
        }
        else {
            if (isna || ISNAN(datum)) {
                if (sign_plus) {
                    // glibc produces "+nan", but we will output " nan" instead
                    preformatted_datum.push_back(' ');
                }
                else if (sign_space)
                    preformatted_datum.push_back(' ');
                // else no sign
            }
            else if (datum < 0.0 /* minus infinity */)
                preformatted_datum.push_back('-');
            else { // plus infinity
                if (sign_plus)
                    preformatted_datum.push_back('+');
                else if (sign_space)
                    preformatted_datum.push_back(' ');
                // else no sign
            }

            // alternate_output has no effect (use inf_string etc. instead)
            if (isna) {
                STRI_ASSERT(!na_string.isNA());
                preformatted_datum.append(
                    na_string.data(), static_cast<size_t>(na_string.length())
                );
            }
            else if (ISNAN(datum)) {
                STRI_ASSERT(!nan_string.isNA());
                preformatted_datum.append(
                    nan_string.data(), static_cast<size_t>(nan_string.length())
                );
            }
            else {
                STRI_ASSERT(!inf_string.isNA());
                preformatted_datum.append(
                    inf_string.data(), static_cast<size_t>(inf_string.length())
                );
            }

            return STRI_SPRINTF_FORMAT_STATUS_NEEDS_PADDING;  /* might need padding (na_string can be fancy Unicode) */
        }
    }


    StriSprintfFormatStatus preformatDatum_s(std::string& preformatted_datum, const String8& datum)
    {
        STRI_ASSERT(!pad_zero);
        STRI_ASSERT(!sign_plus);
        STRI_ASSERT(!sign_space);
        STRI_ASSERT(!alternate_output);

        bool isna = (datum.isNA() || min_width == NA_INTEGER || precision == NA_INTEGER);
        if (!isna) {
            R_len_t datum_size = datum.length();  // this is byte count
            if (precision >= 0) {
                if (use_length) {
                    // ha! output no more than <precision> code points
                    datum_size = ci__length_string(
                        datum.data(), datum_size, precision
                    );
                }
                else {
                    // ho! output code points of total width no more than precision characters
                    datum_size = ci__width_string(
                        datum.data(), datum_size, precision
                    );
                }
            }
            preformatted_datum.append(
                datum.data(), static_cast<size_t>(datum_size)
            );
        }
        else if (na_string.isNA())
            return STRI_SPRINTF_FORMAT_STATUS_IS_NA;
        else { // isNA
            if (na_string.isNA())
                return STRI_SPRINTF_FORMAT_STATUS_IS_NA;

            // output na_string, possibly trimmed
            R_len_t na_string_size = na_string.length();  // this is byte count
            if (precision >= 0) {
                if (use_length) {
                    // ha! output no more than <precision> code points
                    na_string_size = ci__length_string(
                        na_string.data(), na_string_size, precision
                    );
                }
                else {
                    // ho! output code points of total width no more than precision characters
                    na_string_size = ci__width_string(
                        na_string.data(), na_string_size, precision
                    );
                }
            }
            preformatted_datum.append(
                na_string.data(), static_cast<size_t>(na_string_size)
            );
        }

        return STRI_SPRINTF_FORMAT_STATUS_NEEDS_PADDING;  /* might need padding */
    }
};


/** Formats a single string
 *
 * @version 1.6.2 (Marek Gagolewski, 2021-05-24)
 */
struct CiSprintfResult {
    bool is_na;
    std::string bytes;

    CiSprintfResult() : is_na(false), bytes() {}
};


CiSprintfResult ci__sprintf_1(
    const String8& _f,
    StriSprintfDataProvider* data,
    const String8& na_string,
    const String8& inf_string,
    const String8& nan_string,
    bool use_length
) {
    STRI_ASSERT(!_f.isNA());
    R_len_t n = _f.length();
    const char* f = _f.data();

    CiSprintfResult result;
    result.bytes.reserve(static_cast<size_t>(n)+1);
    std::string& buf = result.bytes;

    R_len_t i=0;
    while (i < n) {
        // consume everything up to the next '%'
        if (f[i] != '%') { buf.push_back(f[i++]); continue; }

        // '%' found.
        i++;
        if (i >= n)  // dangling %
            throw StriException(MSG__INVALID_FORMAT_SPECIFIER_SUB, 0, "");

        // if "%%", then output '%' and continue looking for the next '%'
        if (f[i] == '%') { buf.push_back('%'); i++; continue; }

        // We have %., where . is not a %% -- a possible format specifier
        // pre-flight stage -- look for the indef of a type spec (dfFgGs etc.)
        R_len_t j0 = i;  // start
        R_len_t j1 = ci__find_type_spec(f, i, n);  // stop
        i = j1+1; // in the next iteration, start right after the format spec
        // now f[j0..j1] may be a format specifier (without the preceding %)

        StriSprintfFormatSpec spec(
            f, j0, j1, data,
            na_string, inf_string, nan_string, use_length
        );

        std::string formatted_datum;
        if (spec.formatDatum(formatted_datum) == STRI_SPRINTF_FORMAT_STATUS_IS_NA) {
            result.is_na = true;
            result.bytes.clear();
            return result;
        }

        buf.append(formatted_datum);
    }

    return result;
}


/**
 * Format a string
 *
 * vectorized over format and each vector in x
 *
 * @param format character vector
 * @param x list of vectors
 * @param na_string single string, can be NA
 * @param inf_string single string
 * @param nan_string single string
 * @param use_length single logical value
 * @return character vector
 *
 * @version 1.6.2 (Marek Gagolewski, 2021-05-24)
*/
SEXP ci_sprintf(SEXP format, SEXP x, SEXP na_string,
    SEXP inf_string, SEXP nan_string, SEXP use_length)
{
    bool use_length_val = ci__prepare_arg_logical_1_notNA(use_length, "use_length");
    PROTECT(x = ci__prepare_arg_list(x, "x"));
    PROTECT(format = ci__prepare_arg_string(format, "format"));
    PROTECT(na_string = ci__prepare_arg_string_1(na_string, "na_string"));
    PROTECT(inf_string = ci__prepare_arg_string_1(inf_string, "inf_string"));
    PROTECT(nan_string = ci__prepare_arg_string_1(nan_string, "nan_string"));

    R_len_t format_length = LENGTH(format);
    R_len_t vectorize_length = format_length;
    R_len_t narg = LENGTH(x);

    // TODO: allow for the Unicode plus and minus
    // TODO: ICU number format  1,234.567 / 1 234,567 / etc.

    STRI__ERROR_HANDLER_BEGIN(5)
    std::vector<R_len_t> argument_lengths(static_cast<size_t>(narg), 0);
    charport::unwind_protect([&]() -> SEXP {
        for (R_len_t j=0; j<narg; j++) {
            SEXP argument = VECTOR_ELT(x, j);
            if (Rf_isNull(argument)) {
                vectorize_length = 0;
                continue;
            }

            if (!Rf_isVector(argument))
                throw StriException(MSG__ARG_EXPECTED_VECTOR, "...");

            const R_len_t cur_length = LENGTH(argument);
            argument_lengths[static_cast<size_t>(j)] = cur_length;
            if (vectorize_length > 0) {
                if (cur_length <= 0)
                    vectorize_length = 0;
                else if (vectorize_length < cur_length)
                    vectorize_length = cur_length;
            }
        }
        return R_NilValue;
    });

    SEXP ret;

    if (vectorize_length <= 0) {
        {
            charport::charvec::Builder builder(0);
            STRI__PROTECT(ret = builder.to_sexp());
        }
        STRI__UNPROTECT_ALL
        return ret;
    }

    // ASSERT: vectorize_length > 0
    // ASSERT: all elements in x meet Rf_isVector(VECTOR_ELT(x, j))

    if (vectorize_length % format_length != 0)
        STRI__DEFERRED_WARNINGS.push(MSG__WARN_RECYCLING_RULE);

    for (R_len_t j=0; j<narg; j++)
        if (vectorize_length % argument_lengths[static_cast<size_t>(j)] != 0)
            STRI__DEFERRED_WARNINGS.push(MSG__WARN_RECYCLING_RULE);

    // Deviation from stringi: keep controlled recycling diagnostics queued
    // until lazy coercion state and output staging have both been released.

    R_len_t num_unused = 0;
    {
        StriSprintfOwnedStrings format_owned;
        StriSprintfOwnedStrings na_string_owned;
        StriSprintfOwnedStrings inf_string_owned;
        StriSprintfOwnedStrings nan_string_owned;
        {
            // Deviation from stringi: the persistent format and sentinel
            // records are copied once from short Reader-backed containers.
            // No Reader remains active during lazy `...` coercions.
            ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
            StriContainerUTF8 format_cont(
                context, format, vectorize_length
            );
            StriContainerUTF8 na_string_cont(context, na_string, 1);
            StriContainerUTF8 inf_string_cont(context, inf_string, 1);
            StriContainerUTF8 nan_string_cont(context, nan_string, 1);
            format_owned.assign(
                format_cont, format_length, vectorize_length
            );
            na_string_owned.assign(na_string_cont, 1, 1);
            inf_string_owned.assign(inf_string_cont, 1, 1);
            nan_string_owned.assign(nan_string_cont, 1, 1);
        }

        SEXP cache;
        // Deviation from stringi: one protected cache replaces per-object
        // R_PreserveObject calls while retaining three independent lazy views.
        STRI__PROTECT(cache = charport::unwind_protect([&]() -> SEXP {
            return Rf_allocVector(
                VECSXP, static_cast<R_xlen_t>(3)*narg
            );
        }));

        StriSprintfDataProvider data(
            x, cache, narg, vectorize_length,
            STRI__DEFERRED_WARNINGS
        );
        charport::charvec::Builder builder(vectorize_length);

        for (
            R_len_t i = format_owned.vectorize_init();
            i != format_owned.vectorize_end();
            i = format_owned.vectorize_next(i)
        ) {
            const String8& format_cur = format_owned.getNAble(i);
            if (format_cur.isNA()) {
                builder.set_na(i);
                continue;
            }

            // The "parsing" of the format string is done from scratch
            // each time and the output strings are generated on the fly.
            // Let's keep the code simple; the possibility of having
            // *-fields of different max widths/numbers of different precisions
            // makes the process quite complicated anyway.
            data.reset(i);

            CiSprintfResult out = ci__sprintf_1(
                format_cur,
                &data,
                na_string_owned.getNAble(0),
                inf_string_owned.getNAble(0),
                nan_string_owned.getNAble(0),
                use_length_val
            );
            if (out.is_na)
                builder.set_na(i);
            else
                ci::builder_set(
                    builder, i, out.bytes,
                    cetype_ext_t::CE_ASCII_OR_UTF8
                );
        }

        // Deviation from stringi: count after a completely successful pass,
        // but do not warn from the data provider's destructor.
        num_unused = data.countUnused();
        STRI__PROTECT(ret = builder.to_sexp());
    }

    // Drop the cache, prepared inputs, and output staging before warning.
    // Re-protecting the already-created result does not allocate.
    std::vector<R_len_t>().swap(argument_lengths);
    STRI__UNPROTECT_ALL
    STRI__PROTECT(ret);

    if (num_unused == 1) {
        STRI__DEFERRED_WARNINGS.push(MSG__ARG_UNUSED_1);
    }
    else if (num_unused > 1) {
        char warning[StriException_BUFSIZE];
        std::snprintf(
            warning, StriException_BUFSIZE,
            MSG__ARG_UNUSED_N, num_unused
        );
        STRI__DEFERRED_WARNINGS.push(warning);
    }
    STRI__DEFERRED_WARNINGS.emit();

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}
