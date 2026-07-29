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
#include "ci_utf8.h"
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>
#include <unicode/brkiter.h>
#include <unicode/bytestream.h>
#include <unicode/normalizer2.h>
#include <unicode/stringpiece.h>
#include <unicode/uniset.h>

namespace charr { namespace altrep_backend {


namespace wrap {

struct CiWrapIcuState {
    BreakIterator* iterator;
    UText* text;

    explicit CiWrapIcuState(BreakIterator* iterator1)
        : iterator(iterator1), text(NULL)
    {
    }

    ~CiWrapIcuState()
    {
        if (iterator)
            delete iterator;
        if (text)
            utext_close(text);
    }

    CiWrapIcuState(const CiWrapIcuState&) = delete;
    CiWrapIcuState& operator=(const CiWrapIcuState&) = delete;
};

struct CiWrapScratch {
    std::vector<R_len_t> end_orig;
    std::vector<R_len_t> widths_orig;
    std::vector<R_len_t> widths_trim;
    std::vector<R_len_t> end_trim;
    std::vector<R_len_t> wrap_after;
    std::vector<double> cost;
    std::vector<double> best;
    std::vector<uint8_t> breaks;

    void clear_words()
    {
        end_orig.clear();
        widths_orig.clear();
        widths_trim.clear();
        end_trim.clear();
        wrap_after.clear();
    }
};

struct CiWrapRecordView {
    const char* data;
    R_len_t length;
    bool ascii;
};

bool ci__wrap_has_bom(const char* data, R_len_t length)
{
    return length >= 3 && STRI__ENC_HAS_BOM_UTF8(data, length);
}

class CiWrapRecordNormalizer {
private:
    const Normalizer2* normalizer;
    std::string prepared;
    std::string normalized;

    static bool is_ascii(const char* data, R_len_t length)
    {
        for (R_len_t i = 0; i < length; ++i) {
            if (static_cast<unsigned char>(data[i]) >= 0x80)
                return false;
        }
        return true;
    }

    void append_space(bool& in_space_run)
    {
        if (!in_space_run)
            prepared.push_back(' ');
        in_space_run = true;
    }

public:
    explicit CiWrapRecordNormalizer(const Normalizer2* normalizer_) :
        normalizer(normalizer_) {}

    CiWrapRecordView run(
        const char* value, R_len_t length,
        const UnicodeSet& linebreaks, const UnicodeSet& whitespaces
    )
    {
        prepared.clear();
        prepared.reserve(static_cast<std::size_t>(length));

        R_len_t cursor = 0;
        bool field_start = true;
        bool in_space_run = false;

        // The first field and the next two whole-record stages each consume
        // one leading BOM after the UTF-8 input adapter has consumed its own.
        int leading_bom_stages = 3;
        while (leading_bom_stages > 0 &&
                ci__wrap_has_bom(value+cursor, length-cursor)) {
            cursor += 3;
            --leading_bom_stages;
            field_start = false;
        }

        while (cursor < length) {
            if (field_start &&
                    ci__wrap_has_bom(value+cursor, length-cursor)) {
                cursor += 3;
                field_start = false;
                continue;
            }

            const R_len_t begin = cursor;
            UChar32 code_point;
            U8_NEXT(value, cursor, length, code_point);
            if (code_point < 0)
                throw StriException(MSG__INVALID_UTF8);

            if (linebreaks.contains(code_point)) {
                if (code_point == '\r' && cursor < length &&
                        value[cursor] == '\n')
                    ++cursor;
                append_space(in_space_run);
                field_start = true;
                continue;
            }

            field_start = false;
            if (code_point == ' ' || code_point == '\t') {
                append_space(in_space_run);
                continue;
            }

            prepared.append(
                value+begin, static_cast<std::size_t>(cursor-begin)
            );
            in_space_run = false;
        }

        R_len_t begin = 0;
        const R_len_t prepared_length = static_cast<R_len_t>(prepared.size());
        while (begin < prepared_length) {
            const R_len_t previous = begin;
            UChar32 code_point;
            U8_NEXT(prepared.data(), begin, prepared_length, code_point);
            if (!whitespaces.contains(code_point)) {
                begin = previous;
                break;
            }
        }

        R_len_t end = prepared_length;
        while (end > begin) {
            R_len_t previous = end;
            UChar32 code_point;
            U8_PREV(prepared.data(), 0, previous, code_point);
            if (!whitespaces.contains(code_point))
                break;
            end = previous;
        }

        const char* trimmed_data = end == begin
            ? ""
            : prepared.data()+begin;
        const R_len_t trimmed_length = end-begin;
        if (is_ascii(trimmed_data, trimmed_length))
            return CiWrapRecordView{trimmed_data, trimmed_length, true};

        normalized.clear();
        UErrorCode status = U_ZERO_ERROR;
        StringByteSink<std::string> sink(&normalized);
        normalizer->normalizeUTF8(
            0,
            StringPiece(trimmed_data, trimmed_length),
            sink, nullptr, status
        );
        STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
        if (normalized.size() > static_cast<std::size_t>(R_LEN_T_MAX))
            throw StriException("normalized string exceeds R's string length limit");

        const char* data = normalized.empty() ? "" : normalized.data();
        R_len_t output_length = static_cast<R_len_t>(normalized.size());
        if (ci__wrap_has_bom(data, output_length)) {
            data += 3;
            output_length -= 3;
        }
        return CiWrapRecordView{
            data, output_length, is_ascii(data, output_length)
        };
    }
};

} // namespace wrap

using namespace wrap;


/** Greedy word wrap algorithm
 *
 * @param wrap_after [out]
 * @param nwords number of "words"
 * @param width_val maximal desired out line width
 * @param widths_orig ith word width original
 * @param widths_trim ith word width trimmed
 * @param add_para_1
 * @param add_para_n
 *
 * @version 0.1-?? (Bartek Tartanus)
 *          original implementation
 *
 * @version 0.2-2 (Marek Gagolewski, 2014-04-28)
 *          BreakIterator usage mods
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-06)
 *    new args: add_para_1, add_para_n
 */
void ci__wrap_greedy(std::vector<R_len_t>& wrap_after,
                       R_len_t nwords, int width_val,
                       const std::vector<R_len_t>& widths_orig,
                       const std::vector<R_len_t>& widths_trim,
                       int add_para_1, int add_para_n)
{
    R_len_t cur_len = add_para_1+widths_orig[0];
    for (R_len_t j = 1; j < nwords; ++j) {
        if (cur_len + widths_trim[j] > width_val) {
            cur_len = add_para_n+widths_orig[j];
            wrap_after.push_back(j-1);
        }
        else {
            cur_len += widths_orig[j];
        }
    }
}


/** Dynamic word wrap algorithm
 * (Knuth's word wrapping algorithm that minimizes raggedness of formatted text)
 *
 * @param wrap_after [out]
 * @param nwords number of "words"
 * @param width_val maximal desired out line width
 * @param exponent_val cost function exponent
 * @param widths_orig ith word width original
 * @param widths_trim ith word width trimmed
 * @param add_para_1
 * @param add_para_a
 *
 * @version 0.1-?? (Bartek Tartanus)
 *          original implementation
 *
 * @version 0.2-2 (Marek Gagolewski, 2014-04-30)
 *          BreakIterator usage mods
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-06)
 *    new args: add_para_1, add_para_n,
 *    cost of the last line is zero
 */
void ci__wrap_dynamic(CiWrapScratch& scratch,
                        R_len_t nwords, int width_val, double exponent_val,
                        const std::vector<R_len_t>& widths_orig,
                        const std::vector<R_len_t>& widths_trim,
                        int add_para_1, int add_para_n)
{
    const std::size_t words = static_cast<std::size_t>(nwords);
    if (words != 0 &&
            words > std::numeric_limits<std::size_t>::max()/words) {
        throw std::length_error("word-wrap matrix is too large");
    }
    const std::size_t matrix_size = words*words;
    if (matrix_size > scratch.cost.max_size() ||
            matrix_size > scratch.breaks.max_size()) {
        throw std::length_error("word-wrap matrix is too large");
    }
    scratch.cost.resize(matrix_size);
    scratch.best.resize(words);
    scratch.breaks.assign(matrix_size, 0);
    std::vector<double>& cost = scratch.cost;
    std::vector<double>& f = scratch.best;
    std::vector<uint8_t>& where = scratch.breaks;
#define IDX(i,j) static_cast<std::size_t>(i)*words+static_cast<std::size_t>(j)
    // where cost[IDX(i,j)] == cost of printing words i..j in a single line, i<=j

    // calculate costs:
    // there is some "punishment" for leaving blanks at the end of each line
    // (number of "blank" codepoints ^ exponent_val)
    for (int i=0; i<nwords; i++) {
        int sum = 0;
        for (int j=i; j<nwords; j++) {
            if (j > i) {
                if (cost[IDX(i,j-1)] < 0.0) { // already Inf
                    cost[IDX(i,j)] = -1.0; // Inf
                    continue;
                }
                else {
                    sum -= widths_trim[j-1];
                    sum += widths_orig[j-1];
                }
            }
            sum += widths_trim[j];
            int ct = width_val - sum;
            if (i == 0) ct -= add_para_1;
            else        ct -= add_para_n;

            if (j == nwords-1) { // last line == cost 0
                if (j == i || ct >= 0)
                    cost[IDX(i,j)] = 0.0;
                else
                    cost[IDX(i,j)] = -1.0/*Inf*/;
            }
            else if (j == i)
                // some words don't fit in a line at all -> cost 0.0
                cost[IDX(i,j)] = (ct < 0) ? 0.0 : pow((double)ct, exponent_val);
            else
                cost[IDX(i,j)] = (ct < 0) ? -1.0/*"Inf"*/ : pow((double)ct, exponent_val);
        }
    }

    // f[j] == total cost of (optimally) printing words 0..j
    // where[IDX(i,j)] == false iff
    // we don't wrap after i-th word, i<=j
    // when (optimally) printing words 0..j

    for (int j=0; j<nwords; ++j) {
        if (cost[IDX(0,j)] >= 0.0) {
            // no breaking needed: words 0..j fit in one line
            f[j] = cost[IDX(0,j)];
            continue;
        }

        // let i = optimal way of printing of words 0..i + printing i+1..j
        int i = 0;
        while (i <= j) {
            if (cost[IDX(i+1,j)] >= 0.0) break;
            ++i;
        }

        double best_i = f[i] + cost[IDX(i+1,j)];
        for (int k=i+1; k<j; ++k) {
            if (cost[IDX(k+1,j)] < 0.0) continue;
            double best_cur = f[k] + cost[IDX(k+1,j)];
            if (best_cur < best_i) {
                best_i = best_cur;
                i = k;
            }
        }
        for (int k=0; k<i; ++k)
            where[IDX(k,j)] = where[IDX(k,i)];
        where[IDX(i,j)] = true;
        f[j] = best_i;
    }

    //result is in the last row of where...
    for (int k=0; k<nwords; ++k)
        if (where[IDX(k, nwords-1)])
            scratch.wrap_after.push_back(k);
#undef IDX
}


namespace wrap {

struct StriWrapLineStart {
    std::string str;
    R_len_t nbytes;
    R_len_t count;
    R_len_t width;
    bool ascii;

    StriWrapLineStart(const io::Utf8Record& s, R_len_t v) :
        str(
            s.isNA() ? "" : s.data(),
            s.isNA() ? 0 : static_cast<std::size_t>(s.length())
        ) {
        if (s.isNA()) {
            nbytes = count = width = 0;
            ascii = false;
            return;
        }
        nbytes  = s.length()+v;
        count   = s.countCodePoints()+v;
        width   = ci__width_string(s.data(), s.length())+v;
        ascii   = s.isASCII();
        str.append(std::string(v, ' '));
    }
};

} // namespace wrap


bool ci__wrap_ascii_fits(
    const char* value, R_len_t length, int width_val, int line_start_width,
    bool use_length, const UnicodeSet& whitespaces,
    R_len_t& output_end
)
{
    if (length <= 0)
        return false;

    R_len_t measured = 0;
    R_len_t last_width = 0;
    bool last_whitespace = false;
    bool reset = true;
    UChar32 previous = 0;
    UChar32 current = 0;
    for (R_len_t i = 0; i < length; ++i) {
        const unsigned char byte = static_cast<unsigned char>(value[i]);
        if (byte >= 0x80)
            return false;
        if (byte >= 0x0a && byte <= 0x0d)
            throw StriException(MSG__NEWLINE_FOUND);

        previous = current;
        current = byte;
        last_width = use_length
            ? 1
            : ci__width_char_with_context(current, previous, reset);
        measured += last_width;
        last_whitespace = whitespaces.contains(current);
    }

    output_end = length-(last_whitespace ? 1 : 0);
    const int64_t trimmed = static_cast<int64_t>(measured)-
        (last_whitespace ? last_width : 0);
    return static_cast<int64_t>(line_start_width)+trimmed <= width_val;
}


void ci__wrap_add_word(
    CiWrapScratch& scratch, const char* value,
    R_len_t begin, R_len_t end, bool use_length,
    const UnicodeSet& linebreaks, const UnicodeSet& whitespaces
)
{
    R_len_t width_orig = 0;
    R_len_t width_trim = 0;
    R_len_t count_orig = 0;
    R_len_t count_trim = 0;
    R_len_t end_trim = begin;
    UChar32 previous;
    UChar32 current = 0;
    bool reset = true;

    R_len_t j = begin;
    while (j < end) {
        const R_len_t previous_byte = j;
        previous = current;
        U8_NEXT(value, j, end, current);
        if (current < 0)
            throw StriException(MSG__INVALID_UTF8);
        if (linebreaks.contains(current))
            throw StriException(MSG__NEWLINE_FOUND);

        width_orig += ci__width_char_with_context(
            current, previous, reset
        );
        ++count_orig;
        if (whitespaces.contains(current)) {
            width_trim = ci__width_char_with_context(
                current, previous, reset
            );
            count_trim = 1;
            end_trim = previous_byte;
        }
        else {
            width_trim = 0;
            count_trim = 0;
            end_trim = j;
        }
    }

    scratch.end_orig.push_back(end);
    scratch.widths_orig.push_back(
        use_length ? count_orig : width_orig
    );
    scratch.widths_trim.push_back(
        use_length ? count_orig-count_trim : width_orig-width_trim
    );
    scratch.end_trim.push_back(end_trim);
}


bool ci__wrap_fits_one_line(
    const CiWrapScratch& scratch, int line_start_width, int width_val
)
{
    int64_t measured = line_start_width;
    for (R_len_t width : scratch.widths_orig)
        measured += width;
    measured -= scratch.widths_orig.back()-scratch.widths_trim.back();
    return measured <= width_val;
}


bool ci__wrap_is_ascii(const char* value, R_len_t length)
{
    for (R_len_t i = 0; i < length; ++i) {
        if (static_cast<unsigned char>(value[i]) >= 0x80)
            return false;
    }
    return true;
}


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
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-06-09)
 *    BIGSKIP: no more CHARSXP on out on "" input
 */
SEXP ci_wrap(SEXP str, SEXP width, SEXP cost_exponent,
               SEXP indent, SEXP exdent, SEXP prefix, SEXP initial, SEXP whitespace_only,
               SEXP use_length, SEXP locale, SEXP normalize, SEXP output_mode)
{
    // Mode 0 returns one line vector per input, mode 1 flattens every line,
    // and mode 2 joins each input's lines for the public str_wrap() result.
    const int output_mode_val = Rf_asInteger(output_mode);
    const bool flatten_val = output_mode_val == 1;
    const bool join_val = output_mode_val == 2;
    bool normalize_val       = ci__prepare_arg_logical_1_notNA(normalize, "normalize");
    bool use_length_val      = ci__prepare_arg_logical_1_notNA(use_length, "use_length");
    double exponent_val      = ci__prepare_arg_double_1_notNA(cost_exponent, "cost_exponent");
    bool whitespace_only_val = ci__prepare_arg_logical_1_notNA(whitespace_only, "whitespace_only");

    int width_val = ci__prepare_arg_integer_1_notNA(width, "width");
    if (width_val <= 0) width_val = 0;

    int indent_val = ci__prepare_arg_integer_1_notNA(indent, "indent");
    if (indent_val < 0) Rf_error(MSG__INCORRECT_NAMED_ARG "; " MSG__EXPECTED_POSITIVE, "indent");

    int exdent_val = ci__prepare_arg_integer_1_notNA(exdent, "exdent");
    if (exdent_val < 0) Rf_error(MSG__INCORRECT_NAMED_ARG "; " MSG__EXPECTED_POSITIVE, "exdent");


    const char* qloc = ci__prepare_arg_locale(locale, "locale"); /* this is R_alloc'ed */
    PROTECT(str     = ci__prepare_arg_string(str, "str"));
    PROTECT(prefix  = ci__prepare_arg_string_1(prefix, "prefix"));
    PROTECT(initial = ci__prepare_arg_string_1(initial, "initial"));

    BreakIterator* briter = NULL;

    STRI__ERROR_HANDLER_BEGIN(3)
    SEXP ret;
    {
    ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
    R_len_t str_length = 0;
    std::vector<charport::charvec::Store> stores;
    std::optional<charport::charvec::Store> joined_store;
    charport::charvec::GrowableBuilder flat_output;
    {
    // Deviation from stringi: scope ICU state with the Reader-backed work so
    // it is gone before deferred warnings or R output assembly can unwind.
    Locale loc = Locale::createFromName(qloc);
    UErrorCode status = U_ZERO_ERROR;
    briter = BreakIterator::createLineInstance(loc, status);
    STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

    // NOTE: this is very invasive for there are very few dedicated brkiters!
    if (status == U_USING_DEFAULT_WARNING && qloc) {
        UErrorCode status2 = U_ZERO_ERROR;
        const char* valid_locale = briter->getLocaleID(ULOC_VALID_LOCALE, status2);
        if (valid_locale && !strcmp(valid_locale, "root")) {
            // Deviation from stringi: queue the controllable warning so
            // Reader and ICU resources are released before R handles it.
            context.warn(ICUError::getICUerrorName(status));
        }
    }

    str_length = ci::checked_r_len(
        context.size(str), "character vectors"
    );
    // Deviation from stringi: retain each lazy child as a Store until list
    // assembly. The line count is known before a child is constructed, so its
    // storage is exact rather than growable.
    if (!flatten_val && !join_val)
        stores.reserve(static_cast<std::size_t>(str_length));
    {
        io::IndexedUtf8Input str_cont(
            context, str, str_length
        );
        io::Utf8Input prefix_cont(context, prefix, 1);
        io::Utf8Input initial_cont(context, initial, 1);
        // Deviation from stringi: this guard is declared after the Reader
        // containers so exception unwinding releases ICU's borrowed view
        // before it releases the Reader borrow.
        CiWrapIcuState icu_state(briter);
        briter = NULL;


    // prepare indent/exdent/prefix/initial stuff:
    // 1st line, 1st para (i==0, u==0): initial+indent
    // nth line, 1st para (i==0, u> 0): prefix +exdent
    // 1st line, nth para (i> 0, u==0): prefix +indent
    // nth line, nth para (i> 0, u> 0): prefix +exdent
    StriWrapLineStart ii(initial_cont.get(0), indent_val);
    StriWrapLineStart pi(prefix_cont.get(0), indent_val);
    StriWrapLineStart pe(prefix_cont.get(0), exdent_val);


    status = U_ZERO_ERROR;
    //Unicode Newline Guidelines - Unicode Technical Report #13
    UnicodeSet uset_linebreaks(UnicodeString::fromUTF8("[\\u000A-\\u000D\\u0085\\u2028\\u2029]"), status);
    STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
    uset_linebreaks.freeze();

    status = U_ZERO_ERROR;
    UnicodeSet uset_whitespaces(UnicodeString::fromUTF8("\\p{White_space}"), status);
    STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
    uset_whitespaces.freeze();

    const Normalizer2* normalizer = nullptr;
    if (normalize_val) {
        status = U_ZERO_ERROR;
        normalizer = Normalizer2::getNFCInstance(status);
        STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})
    }
    charport::charvec::Builder output(0);
    charport::charvec::Builder joined_output(join_val ? str_length : 0);
    CiWrapScratch scratch;
    CiWrapRecordNormalizer record_normalizer(normalizer);

    for (R_len_t i = 0; i < str_length; ++i)
    {
        const bool source_is_na = str_cont.isNA(i);
        CiWrapRecordView record{nullptr, 0, false};
        if (!source_is_na) {
            const io::Utf8Record& source = str_cont.get(i);
            record = CiWrapRecordView{
                source.data(), source.length(), source.isASCII()
            };
            if (normalize_val) {
                record = record_normalizer.run(
                    source.data(), source.length(),
                    uset_linebreaks, uset_whitespaces
                );
            }
        }

        if (source_is_na || prefix_cont.isNA(0) || initial_cont.isNA(0)) {
            if (flatten_val)
                ci::builder_append(
                    flat_output, NULL, 0, cetype_ext_t::CE_NA
                );
            else if (join_val)
                joined_output.set_na(i);
            else
                stores.push_back(charport::charvec::Store::scalar(
                    NULL, 0, cetype_ext_t::CE_NA
                ));
            continue;
        }

        const char* str_cur_s = record.data;
        R_len_t str_cur_n = record.length;
        const bool str_cur_ascii = record.ascii;
        const int first_start_width = use_length_val
            ? ((i == 0) ? ii.count : pi.count)
            : ((i == 0) ? ii.width : pi.width);
        const int next_start_width = use_length_val ? pe.count : pe.width;
        auto line_start = [&](R_len_t line) -> const StriWrapLineStart& {
            return line == 0 ? ((i == 0) ? ii : pi) : pe;
        };
        auto emit_line = [&] (
            R_len_t line, R_len_t begin, R_len_t end
        ) {
            const StriWrapLineStart& start = line_start(line);
            const R_len_t body_length = end-begin;
            const std::size_t body_size = static_cast<std::size_t>(body_length);
            const std::size_t output_size = start.str.size()+body_size;
            const bool output_ascii = start.ascii &&
                (str_cur_ascii || ci__wrap_is_ascii(
                    str_cur_s+begin, body_length
                ));
            const cetype_ext_t output_encoding = output_ascii
                ? cetype_ext_t::CE_ASCII
                : cetype_ext_t::CE_UTF8;
            char* destination;
            if (flatten_val) {
                destination = flat_output.append_reserve(
                    output_size, output_encoding
                );
            }
            else if (join_val) {
                destination = joined_output.reserve(
                    i, output_size, output_encoding
                );
            }
            else {
                destination = output.reserve(
                    line, output_size, output_encoding
                );
            }
            if (!start.str.empty())
                std::memcpy(destination, start.str.data(), start.str.size());
            if (body_size > 0) {
                std::memcpy(
                    destination+start.str.size(), str_cur_s+begin, body_size
                );
            }
        };

        R_len_t ascii_end = 0;
        if (ci__wrap_ascii_fits(
                str_cur_s, str_cur_n, width_val, first_start_width,
                use_length_val, uset_whitespaces, ascii_end
        )) {
            if (!flatten_val && !join_val)
                output.reset(1);
            emit_line(0, 0, ascii_end);
            if (!flatten_val && !join_val)
                stores.push_back(output.release_store());
            continue;
        }

        status = U_ZERO_ERROR;
        icu_state.text = utext_openUTF8(
            icu_state.text, str_cur_s, str_cur_n, &status
        );
        STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

        status = U_ZERO_ERROR;
        icu_state.iterator->setText(icu_state.text, status);
        STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

        scratch.clear_words();
        R_len_t match = icu_state.iterator->first();
#ifndef NDEBUG
        if (match != 0)
            throw StriException("NDEBUG: ci_wrap: (first boundary != 0)");
#endif
        R_len_t previous_end = 0;
        while (match != BreakIterator::DONE) {
            bool accepted = !whitespace_only_val;
            if (whitespace_only_val) {
                if (match > 0 && match < str_cur_n) {
                    UChar32 c;
                    U8_GET((const uint8_t*)str_cur_s, 0, match-1, str_cur_n, c);
                    if (uset_whitespaces.contains(c))
                        accepted = true;
                }
                else
                    accepted = true;
            }
            if (accepted && match > 0) {
                ci__wrap_add_word(
                    scratch, str_cur_s, previous_end, match,
                    use_length_val, uset_linebreaks, uset_whitespaces
                );
                previous_end = match;
            }

            match = icu_state.iterator->next();
        }

        const R_len_t nwords = static_cast<R_len_t>(scratch.end_orig.size());
        if (nwords == 0) {
            const cetype_ext_t encoding = str_cur_ascii
                ? cetype_ext_t::CE_ASCII
                : cetype_ext_t::CE_UTF8;
            if (flatten_val) {
                ci::builder_append(
                    flat_output, str_cur_s,
                    static_cast<std::size_t>(str_cur_n), encoding
                );
            }
            else if (join_val) {
                ci::builder_set(
                    joined_output, i, str_cur_s,
                    static_cast<std::size_t>(str_cur_n), encoding
                );
            }
            else {
                stores.push_back(ci::scalar_store(
                    str_cur_s, static_cast<std::size_t>(str_cur_n), encoding
                ));
            }
            continue;
        }

        if (!ci__wrap_fits_one_line(
                scratch, first_start_width, width_val
        )) {
            if (exponent_val <= 0.0) {
                ci__wrap_greedy(
                    scratch.wrap_after, nwords, width_val,
                    scratch.widths_orig, scratch.widths_trim,
                    first_start_width, next_start_width
                );
            }
            else {
                ci__wrap_dynamic(
                    scratch, nwords, width_val, exponent_val,
                    scratch.widths_orig, scratch.widths_trim,
                    first_start_width, next_start_width
                );
            }
        }

        const R_len_t nlines = static_cast<R_len_t>(
            scratch.wrap_after.size()+1
        );
        if (join_val) {
            const std::size_t maximum = static_cast<std::size_t>(R_LEN_T_MAX);
            std::size_t joined_size = static_cast<std::size_t>(nlines-1);
            bool joined_ascii = true;
            auto add_line = [&] (
                R_len_t line, R_len_t begin, R_len_t end
            ) {
                const StriWrapLineStart& start = line_start(line);
                const R_len_t body_length = end-begin;
                const std::size_t body_size = static_cast<std::size_t>(
                    body_length
                );
                if (start.str.size() > maximum-joined_size)
                    throw StriException("wrapped string exceeds R's string length limit");
                joined_size += start.str.size();
                if (body_size > maximum-joined_size)
                    throw StriException("wrapped string exceeds R's string length limit");
                joined_size += body_size;
                joined_ascii = joined_ascii && start.ascii &&
                    (str_cur_ascii || ci__wrap_is_ascii(
                        str_cur_s+begin, body_length
                    ));
            };

            R_len_t joined_last_pos = 0;
            R_len_t line = 0;
            for (R_len_t wrap_after_cur : scratch.wrap_after) {
                const R_len_t cur_pos = scratch.end_trim[wrap_after_cur];
                add_line(line, joined_last_pos, cur_pos);
                joined_last_pos = scratch.end_orig[wrap_after_cur];
                ++line;
            }
            add_line(
                nlines-1, joined_last_pos,
                scratch.end_trim[nwords-1]
            );

            char* destination = joined_output.reserve(
                i, joined_size,
                joined_ascii
                    ? cetype_ext_t::CE_ASCII
                    : cetype_ext_t::CE_UTF8
            );
            std::size_t destination_pos = 0;
            auto emit_joined_line = [&] (
                R_len_t current_line, R_len_t begin, R_len_t end
            ) {
                if (current_line > 0)
                    destination[destination_pos++] = '\n';
                const StriWrapLineStart& start = line_start(current_line);
                if (!start.str.empty()) {
                    std::memcpy(
                        destination+destination_pos,
                        start.str.data(), start.str.size()
                    );
                    destination_pos += start.str.size();
                }
                const std::size_t body_size = static_cast<std::size_t>(
                    end-begin
                );
                if (body_size > 0) {
                    std::memcpy(
                        destination+destination_pos,
                        str_cur_s+begin, body_size
                    );
                    destination_pos += body_size;
                }
            };

            joined_last_pos = 0;
            line = 0;
            for (R_len_t wrap_after_cur : scratch.wrap_after) {
                const R_len_t cur_pos = scratch.end_trim[wrap_after_cur];
                emit_joined_line(line, joined_last_pos, cur_pos);
                joined_last_pos = scratch.end_orig[wrap_after_cur];
                ++line;
            }
            emit_joined_line(
                nlines-1, joined_last_pos,
                scratch.end_trim[nwords-1]
            );
            continue;
        }

        R_len_t last_pos = 0;
        if (!flatten_val)
            output.reset(nlines);
        R_len_t u = 0;
        for (R_len_t wrap_after_cur : scratch.wrap_after) {
            const R_len_t cur_pos = scratch.end_trim[wrap_after_cur];
            emit_line(u, last_pos, cur_pos);
            last_pos = scratch.end_orig[wrap_after_cur];
            ++u;
        }

        emit_line(nlines-1, last_pos, scratch.end_trim[nwords-1]);
        if (!flatten_val)
            stores.push_back(output.release_store());
    }
    if (join_val)
        joined_store.emplace(joined_output.release_store());
    }
    }

    // Deviation from stringi: protect the complete lazy list, then release
    // its staging Stores before deferred warnings are replayed.
    if (flatten_val) {
        STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
            return flat_output.to_sexp();
        }));
    }
    else if (join_val) {
        STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
            return charport::charvec::wrap(std::move(*joined_store));
        }));
    }
    else {
        STRI__PROTECT(ret = ci::unwind_protect([&]() -> SEXP {
            return Rf_allocVector(VECSXP, str_length);
        }));
        ci::unwind_protect([&]() -> SEXP {
            for (R_len_t i=0; i<str_length; ++i) {
                SEXP ans = PROTECT(charport::charvec::wrap(
                    std::move(stores[i])
                ));
                SET_VECTOR_ELT(ret, i, ans);
                UNPROTECT(1);
            }
            return R_NilValue;
        });
    }
    }
    STRI__DEFERRED_WARNINGS.emit();

    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END({
        if (briter) {
            delete briter;
            briter = NULL;
        }
    })
}

} } // namespace charr::altrep_backend
