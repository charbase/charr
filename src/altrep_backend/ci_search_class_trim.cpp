
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
#include "ci_parallel.h"
#include "io/reader_utils.h"
#include "../shared/character_class.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"
#include "altrep_backend/io/string_view.h"
#include "altrep_backend/io/utf8_output.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace charr { namespace altrep_backend {


namespace search_class_trim {

struct TrimSlice {
    const char* data;
    R_len_t length;
    bool removed_non_ascii;
};


struct ScalarTrimPattern {
    const UnicodeSet* retained;
    std::array<unsigned char, 128> ascii_retained;
    bool missing;
};


CHARR_NEUTRAL_HELPER bool retained_contains(
    const UnicodeSet& retained, const unsigned char* ascii_retained,
    UChar32 code_point
) noexcept
{
    if (ascii_retained != nullptr && code_point <= 0x7f)
        return ascii_retained[code_point] != 0;
    return retained.contains(code_point);
}


// Compile-time direction flags keep the hot edge scan branch-free.
template<bool Left, bool Right>
CHARR_CXX_HELPER TrimSlice trim_slice(
    const char* data, R_len_t length,
    const UnicodeSet& retained, const unsigned char* ascii_retained,
    bool strip_bom
)
{
    bool removed_non_ascii = false;
    if (strip_bom && STRI__ENC_HAS_BOM_UTF8(data, length)) {
        data += 3;
        length -= 3;
        removed_non_ascii = true;
    }

    R_len_t begin = 0;
    R_len_t end = length;

    if (Left) {
        UChar32 code_point;
        for (R_len_t cursor = 0; cursor < length; ) {
            U8_NEXT(data, cursor, length, code_point);
            if (code_point < 0)
                throw StriException(MSG__INVALID_UTF8);
            if (retained_contains(
                    retained, ascii_retained, code_point
                ))
                break;
            removed_non_ascii = removed_non_ascii || code_point > 0x7f;
            begin = cursor;
        }
    }

    if (Right && begin < length) {
        UChar32 code_point;
        for (R_len_t cursor = length; cursor > 0; ) {
            U8_PREV(data, 0, cursor, code_point);
            if (code_point < 0)
                throw StriException(MSG__INVALID_UTF8);
            if (retained_contains(
                    retained, ascii_retained, code_point
                ))
                break;
            removed_non_ascii = removed_non_ascii || code_point > 0x7f;
            end = cursor;
        }
    }

    return TrimSlice{
        data + begin, end - begin, removed_non_ascii
    };
}


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


CHARR_CXX_HELPER void normalize_views(
    const charport::StrViews& source,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    std::vector<shared::StringView>& output,
    bool preserve_bom
)
{
    output.resize(static_cast<std::size_t>(source.size()));
    for (R_xlen_t i = 0; i < source.size(); ++i) {
        const shared::StringView value = io::as_shared_view(source[i]);
        if (value.enc == shared::StringEncoding::bytes)
            throw StriException(MSG__BYTESENC);
        output[static_cast<std::size_t>(i)] = preserve_bom
            ? shared::normalize_utf8_preserve_bom(
                value, converter, storage
            )
            : shared::normalize_utf8(value, converter, storage);
    }
}


CHARR_CXX_HELPER void require_icu_success(UErrorCode status)
{
    if (U_FAILURE(status))
        throw StriException(status);
}


CHARR_NEUTRAL_HELPER bool strip_input_bom(
    const charport::StrView& original,
    const shared::StringView& normalized
) noexcept
{
    if (!STRI__ENC_HAS_BOM_UTF8(normalized.ptr, normalized.len))
        return false;
    if (original.enc == CETYPE_EXT_UTF8 ||
            original.enc == CETYPE_EXT_ASCII_OR_UTF8)
        return true;
    return original.enc == CETYPE_EXT_NATIVE &&
        STRI__ENC_HAS_BOM_UTF8(original.ptr, original.len);
}


CHARR_NEUTRAL_HELPER cetype_ext_t trimmed_encoding(
    const charport::StrView& original,
    const shared::StringView& normalized,
    const TrimSlice& trimmed
) noexcept
{
    if (trimmed.length == 0 ||
            original.enc == CETYPE_EXT_ASCII)
        return CETYPE_EXT_ASCII;

    // A definite UTF-8 mark promises a non-ASCII record. Removing only ASCII
    // edge code points cannot invalidate that promise. Ambiguous marks and
    // converted records need one scan of the retained slice.
    if (original.enc == CETYPE_EXT_UTF8 &&
            normalized.enc == shared::StringEncoding::utf8 &&
            !trimmed.removed_non_ascii)
        return CETYPE_EXT_UTF8;

    return io::is_ascii(
        trimmed.data, static_cast<std::size_t>(trimmed.length)
    ) ? CETYPE_EXT_ASCII : CETYPE_EXT_UTF8;
}


CHARR_CXX_HELPER ScalarTrimPattern make_scalar_pattern(
    const shared::CharacterClassSet& patterns
)
{
    ScalarTrimPattern scalar{nullptr, {}, patterns.is_na(0)};
    if (scalar.missing)
        return scalar;

    scalar.retained = &patterns.get(0);
    for (std::size_t i = 0; i < scalar.ascii_retained.size(); ++i) {
        scalar.ascii_retained[i] = static_cast<unsigned char>(
            scalar.retained->contains(static_cast<UChar32>(i))
        );
    }
    return scalar;
}


CHARR_CXX_HELPER bool source_is_direct_utf8(
    const charport::StrViews& source
)
{
    for (R_xlen_t i = 0; i < source.size(); ++i) {
        const charport::StrView value = source[i];
        if (value.is_na())
            continue;
        if (value.ptr == nullptr || value.len < 0)
            throw std::runtime_error("Reader returned an invalid string view");

        switch (value.enc.value) {
        case CETYPE_EXT_ASCII.value:
        case CETYPE_EXT_UTF8.value:
        case CETYPE_EXT_ASCII_OR_UTF8.value:
            break;
        case CETYPE_EXT_BYTES.value:
            throw StriException(MSG__BYTESENC);
        case CETYPE_EXT_LATIN1.value:
        case CETYPE_EXT_NATIVE.value:
            return false;
        case CETYPE_EXT_NA.value:
            throw std::logic_error(
                "non-missing Reader record has NA encoding"
            );
        default:
            throw std::runtime_error(
                "Reader returned an unknown string encoding"
            );
        }
    }
    return true;
}


CHARR_CXX_HELPER void add_payload_length(
    std::size_t& total, R_len_t length
)
{
    const std::size_t amount = static_cast<std::size_t>(length);
    if (amount > std::numeric_limits<std::size_t>::max() - total)
        throw std::length_error("character output payload is too large");
    total += amount;
}


template<bool ScalarPattern>
CHARR_CXX_HELPER const UnicodeSet* select_pattern(
    R_len_t index, const shared::CharacterClassSet& patterns,
    const ScalarTrimPattern& scalar, bool& missing,
    const unsigned char*& ascii_retained
)
{
    if constexpr (ScalarPattern) {
        missing = scalar.missing;
        ascii_retained = scalar.ascii_retained.data();
        return scalar.retained;
    }
    else {
        missing = patterns.is_na(static_cast<std::size_t>(index));
        ascii_retained = nullptr;
        return missing
            ? nullptr
            : &patterns.get(static_cast<std::size_t>(index));
    }
}


template<
    bool Left, bool Right, bool NormalizedSource,
    bool ScalarPattern, bool RecycledSource
>
CHARR_CXX_HELPER void trim_records(
    const charport::StrViews& original_source,
    const std::vector<shared::StringView>& source,
    R_len_t output_begin, R_len_t output_end,
    const shared::CharacterClassSet& patterns,
    const ScalarTrimPattern& scalar,
    charport::charvec::Store& output
)
{
    const R_len_t source_length = static_cast<R_len_t>(
        original_source.size()
    );
    const R_len_t output_length = output_end - output_begin;
    output = charport::charvec::Store(
        static_cast<std::size_t>(output_length), 0
    );
    std::size_t payload_length = 0;

    for (R_len_t i = output_begin; i < output_end; ++i) {
        const std::size_t output_index = static_cast<std::size_t>(
            i - output_begin
        );
        const R_len_t source_index = RecycledSource
            ? i % source_length
            : i;
        const charport::StrView original = original_source[source_index];
        shared::StringView value = io::as_shared_view(original);
        if constexpr (NormalizedSource) {
            value = source[static_cast<std::size_t>(source_index)];
        }
        bool pattern_missing;
        const unsigned char* ascii_retained;
        const UnicodeSet* retained = select_pattern<ScalarPattern>(
            i, patterns, scalar, pattern_missing, ascii_retained
        );
        if (value.is_na() || pattern_missing) {
            const charport::StrView missing =
                charport::charvec::components::na_record();
            output.records.set(
                output_index,
                missing.ptr, missing.len, missing.enc
            );
            continue;
        }

        const TrimSlice trimmed = trim_slice<Left, Right>(
            value.ptr, value.len, *retained, ascii_retained,
            strip_input_bom(original, value)
        );
        const cetype_ext_t encoding = trimmed_encoding(
            original, value, trimmed
        );
        const char* record_data = trimmed.length == 0
            ? charport::charvec::components::empty_data()
            : trimmed.data;
        output.records.set(
            output_index,
            record_data, trimmed.length, encoding
        );
        add_payload_length(payload_length, trimmed.length);
    }

    if (payload_length > 0) {
        char* destination = output.slices.push_front(payload_length);
        for (R_len_t i = 0; i < output_length; ++i) {
            const std::size_t index = static_cast<std::size_t>(i);
            const charport::StrView record = output.records.view(index);
            if (record.is_na() || record.len == 0)
                continue;
            std::memcpy(
                destination, record.ptr,
                static_cast<std::size_t>(record.len)
            );
            output.records.set(index, destination, record.len, record.enc);
            destination += record.len;
        }
    }
}


template<bool Left, bool Right, bool NormalizedSource>
CHARR_CXX_HELPER void trim_dispatch(
    const charport::StrViews& original_source,
    const std::vector<shared::StringView>& source,
    R_len_t output_length,
    const shared::CharacterClassSet& patterns,
    charport::charvec::Store& output
)
{
    const bool scalar_pattern = patterns.size() == 1;
    const ScalarTrimPattern scalar = scalar_pattern
        ? make_scalar_pattern(patterns)
        : ScalarTrimPattern{nullptr, {}, false};
    const bool recycled_source =
        static_cast<R_len_t>(original_source.size()) != output_length;
    if (scalar_pattern) {
        if (recycled_source) {
            trim_records<Left, Right, NormalizedSource, true, true>(
                original_source, source, 0, output_length,
                patterns, scalar, output
            );
        }
        else {
            trim_records<Left, Right, NormalizedSource, true, false>(
                original_source, source, 0, output_length,
                patterns, scalar, output
            );
        }
    }
    else if (recycled_source) {
        trim_records<Left, Right, NormalizedSource, false, true>(
            original_source, source, 0, output_length,
            patterns, scalar, output
        );
    }
    else {
        trim_records<Left, Right, NormalizedSource, false, false>(
            original_source, source, 0, output_length,
            patterns, scalar, output
        );
    }
}


template<bool Left, bool Right>
CHARR_CXX_HELPER void compute_trim(
    const charport::StrViews& subject_views,
    const charport::StrViews& pattern_views,
    R_len_t vectorize_length, bool negate,
    shared::NativeToUtf8& subject_converter,
    shared::NativeToUtf8& pattern_converter,
    shared::SliceArena& subject_storage,
    shared::SliceArena& pattern_storage,
    std::vector<shared::StringView>& subjects,
    std::vector<shared::StringView>& patterns,
    shared::CharacterClassSet& pattern_set,
    charport::charvec::Store& output
)
{
    normalize_views(
        pattern_views, pattern_converter, pattern_storage,
        patterns, false
    );
    require_icu_success(pattern_set.reset(patterns, negate));

    if (vectorize_length <= 0)
        return;

    const bool direct_source = source_is_direct_utf8(subject_views);
    if (direct_source) {
        trim_dispatch<Left, Right, false>(
            subject_views, subjects, vectorize_length,
            pattern_set, output
        );
    }
    else {
        normalize_views(
            subject_views, subject_converter, subject_storage,
            subjects, true
        );
        trim_dispatch<Left, Right, true>(
            subject_views, subjects, vectorize_length,
            pattern_set, output
        );
    }
}


/*
 * Where the threaded trim plan stages its output. trim_records sizes its
 * store to the range it is handed and addresses it from that range's start,
 * so a worker that draws several chunks produces one store per chunk rather
 * than one store covering a contiguous slice. Worker order is therefore no
 * longer task order, and each store has to travel with the first task of the
 * chunk that produced it so the stores can be put back in task order before
 * they are joined.
 *
 * The rows are sized before the region starts and a worker only ever appends
 * to its own row, so no two workers touch the same vector.
 */
class CHARR_OWNER_TYPE TrimChunkOutputs {
public:
    CHARR_CXX_HELPER TrimChunkOutputs()
        : begins_(), stores_()
    {
    }

    TrimChunkOutputs(const TrimChunkOutputs&) = delete;
    TrimChunkOutputs& operator=(const TrimChunkOutputs&) = delete;
    TrimChunkOutputs(TrimChunkOutputs&&) = delete;
    TrimChunkOutputs& operator=(TrimChunkOutputs&&) = delete;

    CHARR_CXX_HELPER void reset(unsigned workers)
    {
        begins_.clear();
        stores_.clear();
        begins_.resize(static_cast<std::size_t>(workers));
        stores_.resize(static_cast<std::size_t>(workers));
    }

    CHARR_CXX_HELPER void add(
        unsigned worker, R_len_t begin, io::OutputStore&& store
    )
    {
        const std::size_t row = static_cast<std::size_t>(worker);
        begins_[row].push_back(begin);
        stores_[row].push_back(std::move(store));
    }

    // Chunks ascend within a worker, so recovering task order is a merge of
    // rows that are already ordered rather than a sort.
    CHARR_CXX_HELPER [[nodiscard]] io::OutputStore concatenate()
    {
        const std::size_t rows = stores_.size();
        std::size_t total = 0;
        for (std::size_t row = 0; row < rows; ++row)
            total += stores_[row].size();

        std::vector<std::size_t> cursors(rows, 0);
        std::vector<io::OutputStore> ordered;
        ordered.reserve(total);
        for (;;) {
            std::size_t next = rows;
            R_len_t lowest = 0;
            for (std::size_t row = 0; row < rows; ++row) {
                if (cursors[row] >= stores_[row].size())
                    continue;
                const R_len_t candidate = begins_[row][cursors[row]];
                if (next == rows || candidate < lowest) {
                    next = row;
                    lowest = candidate;
                }
            }
            if (next == rows)
                break;
            ordered.push_back(std::move(stores_[next][cursors[next]]));
            ++cursors[next];
        }
        return io::concat_stores(ordered);
    }

private:
    std::vector<std::vector<R_len_t>> begins_;
    std::vector<std::vector<io::OutputStore>> stores_;
};


template<
    bool Left, bool Right, bool NormalizedSource,
    bool ScalarPattern, bool RecycledSource
>
class TrimBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER TrimBody(
        const charport::StrViews& original,
        const std::vector<shared::StringView>& normalized,
        const shared::CharacterClassSet& patterns,
        const ScalarTrimPattern& scalar,
        TrimChunkOutputs& outputs
    )
        : original_(original), normalized_(normalized),
          patterns_(patterns), scalar_(scalar), outputs_(outputs)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        while (context.next_chunk()) {
            const R_len_t begin = static_cast<R_len_t>(context.begin);
            io::OutputStore chunk;
            trim_records<
                Left, Right, NormalizedSource, ScalarPattern, RecycledSource
            >(
                original_, normalized_, begin,
                static_cast<R_len_t>(context.end),
                patterns_, scalar_, chunk
            );
            outputs_.add(context.worker, begin, std::move(chunk));
        }
    }

private:
    const charport::StrViews& original_;
    const std::vector<shared::StringView>& normalized_;
    const shared::CharacterClassSet& patterns_;
    ScalarTrimPattern scalar_;
    TrimChunkOutputs& outputs_;
};


template<
    bool Left, bool Right, bool NormalizedSource,
    bool ScalarPattern, bool RecycledSource
>
CHARR_CXX_HELPER void run_trim_parallel(
    const charport::StrViews& subject_views,
    const std::vector<shared::StringView>& subjects,
    const shared::CharacterClassSet& pattern_set,
    const ScalarTrimPattern& scalar,
    R_len_t vectorize_length,
    const shared::ParallelPlan& plan,
    TrimChunkOutputs& outputs
)
{
    TrimBody<
        Left, Right, NormalizedSource, ScalarPattern, RecycledSource
    > body(subject_views, subjects, pattern_set, scalar, outputs);
    shared::run_parallel(plan, vectorize_length, body);
}


template<bool Left, bool Right, bool NormalizedSource>
CHARR_CXX_HELPER void trim_parallel_dispatch(
    const charport::StrViews& subject_views,
    const std::vector<shared::StringView>& subjects,
    R_len_t vectorize_length,
    const shared::CharacterClassSet& pattern_set,
    const shared::ParallelPlan& plan,
    TrimChunkOutputs& outputs,
    io::OutputStore& output
)
{
    outputs.reset(plan.workers);

    const bool scalar_pattern = pattern_set.size() == 1;
    const ScalarTrimPattern scalar = scalar_pattern
        ? make_scalar_pattern(pattern_set)
        : ScalarTrimPattern{nullptr, {}, false};
    const bool recycled_source =
        static_cast<R_len_t>(subject_views.size()) != vectorize_length;

    if (scalar_pattern) {
        if (recycled_source) {
            run_trim_parallel<
                Left, Right, NormalizedSource, true, true
            >(
                subject_views, subjects, pattern_set, scalar,
                vectorize_length, plan, outputs
            );
        }
        else {
            run_trim_parallel<
                Left, Right, NormalizedSource, true, false
            >(
                subject_views, subjects, pattern_set, scalar,
                vectorize_length, plan, outputs
            );
        }
    }
    else if (recycled_source) {
        run_trim_parallel<
            Left, Right, NormalizedSource, false, true
        >(
            subject_views, subjects, pattern_set, scalar,
            vectorize_length, plan, outputs
        );
    }
    else {
        run_trim_parallel<
            Left, Right, NormalizedSource, false, false
        >(
            subject_views, subjects, pattern_set, scalar,
            vectorize_length, plan, outputs
        );
    }

    output = outputs.concatenate();
}


template<bool Left, bool Right>
CHARR_CXX_HELPER void compute_trim_parallel(
    const charport::StrViews& subject_views,
    const charport::StrViews& pattern_views,
    R_len_t vectorize_length, bool negate,
    shared::NativeToUtf8& subject_converter,
    shared::NativeToUtf8& pattern_converter,
    shared::SliceArena& subject_storage,
    shared::SliceArena& pattern_storage,
    std::vector<shared::StringView>& subjects,
    std::vector<shared::StringView>& patterns,
    shared::CharacterClassSet& pattern_set,
    const shared::ParallelPlan& plan,
    TrimChunkOutputs& outputs,
    io::OutputStore& output
)
{
    normalize_views(
        pattern_views, pattern_converter, pattern_storage,
        patterns, false
    );
    require_icu_success(pattern_set.reset(patterns, negate));
    const bool direct_source = source_is_direct_utf8(subject_views);
    if (!direct_source) {
        normalize_views(
            subject_views, subject_converter, subject_storage,
            subjects, true
        );
    }
    if (direct_source) {
        trim_parallel_dispatch<Left, Right, false>(
            subject_views, subjects, vectorize_length,
            pattern_set, plan, outputs, output
        );
    }
    else {
        trim_parallel_dispatch<Left, Right, true>(
            subject_views, subjects, vectorize_length,
            pattern_set, plan, outputs, output
        );
    }
}


CHARR_R_HELPER void emit_warnings(bool recycling_warning) noexcept
{
    if (recycling_warning)
        Rf_warning(MSG__WARN_RECYCLING_RULE);
}

} // namespace search_class_trim

using namespace search_class_trim;


/**
 * Trim characters from a charclass from both sides of the string
 *
 * @param str character vector
 * @param pattern character vector
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-04)
 *          Use ci__trim_leftright
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *          make StriException-friendly
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-06-10) negate
*/
CHARR_ENTRYPOINT SEXP ci_trim_both(
    SEXP str, SEXP pattern, SEXP negate
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();


    const bool negate_value = ci__prepare_arg_logical_1_notNA_r(
        negate, "negate"
    );
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    pattern = entry_protections.protect_one(
        ci__prepare_arg_string_r(pattern, "pattern")
    );


    bool recycling_warning = false;

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
        shared::CharacterClassSet pattern_set;
        charport::charvec::Store output;
        TrimChunkOutputs parallel_outputs;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t subject_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                const R_len_t pattern_length = io::checked_r_len(
                    XLENGTH(pattern), "character vectors"
                );
                const R_len_t vectorize_length = recycling_length(
                    subject_length, pattern_length, recycling_warning
                );
                const shared::ParallelPlan plan = shared::parallel_plan(
                    true, vectorize_length
                );
                subject_reader.reset(str);
                if (subject_reader.size() != subject_length) {
                    throw std::runtime_error(
                        "Reader length changed during character-class trimming"
                    );
                }

                subject_views.resize(subject_length);
                if (subject_length > 0) {
                    subject_reader.views(
                        0, subject_length,
                        subject_views.ptrs(), subject_views.lengths(),
                        subject_views.encodings()
                    );
                }
                if (pattern_length > 0) {
                    pattern_reader.reset(pattern);
                    if (pattern_reader.size() != pattern_length) {
                        throw std::runtime_error(
                            "Reader length changed during character-class trimming"
                        );
                    }

                    pattern_views.resize(pattern_length);
                    pattern_reader.views(
                        0, pattern_length,
                        pattern_views.ptrs(), pattern_views.lengths(),
                        pattern_views.encodings()
                    );

                    if (plan.workers > 1) {
                        compute_trim_parallel<true, true>(
                            subject_views, pattern_views,
                            vectorize_length, negate_value,
                            subject_converter, pattern_converter,
                            subject_storage, pattern_storage,
                            subjects, patterns, pattern_set, plan,
                            parallel_outputs, output
                        );
                    }
                    else {
                        compute_trim<true, true>(
                            subject_views, pattern_views,
                            vectorize_length, negate_value,
                            subject_converter, pattern_converter,
                            subject_storage, pattern_storage,
                            subjects, patterns, pattern_set, output
                        );
                    }
                }

                result = entry_protections.reprotect_one(
                    io::finalize(std::move(output)),
                    result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_warnings(recycling_warning);
    );
}


/**
 * Trim characters from a charclass from the left of the string
 *
 * @param str character vector
 * @param pattern character vector
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-04)
 *          Use ci__trim_leftright
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-06-10) negate
*/
CHARR_ENTRYPOINT SEXP ci_trim_left(
    SEXP str, SEXP pattern, SEXP negate
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();


    const bool negate_value = ci__prepare_arg_logical_1_notNA_r(
        negate, "negate"
    );
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    pattern = entry_protections.protect_one(
        ci__prepare_arg_string_r(pattern, "pattern")
    );


    bool recycling_warning = false;

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
        shared::CharacterClassSet pattern_set;
        charport::charvec::Store output;
        TrimChunkOutputs parallel_outputs;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t subject_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                const R_len_t pattern_length = io::checked_r_len(
                    XLENGTH(pattern), "character vectors"
                );
                const R_len_t vectorize_length = recycling_length(
                    subject_length, pattern_length, recycling_warning
                );
                const shared::ParallelPlan plan = shared::parallel_plan(
                    true, vectorize_length
                );
                subject_reader.reset(str);
                if (subject_reader.size() != subject_length) {
                    throw std::runtime_error(
                        "Reader length changed during character-class trimming"
                    );
                }

                subject_views.resize(subject_length);
                if (subject_length > 0) {
                    subject_reader.views(
                        0, subject_length,
                        subject_views.ptrs(), subject_views.lengths(),
                        subject_views.encodings()
                    );
                }
                if (pattern_length > 0) {
                    pattern_reader.reset(pattern);
                    if (pattern_reader.size() != pattern_length) {
                        throw std::runtime_error(
                            "Reader length changed during character-class trimming"
                        );
                    }

                    pattern_views.resize(pattern_length);
                    pattern_reader.views(
                        0, pattern_length,
                        pattern_views.ptrs(), pattern_views.lengths(),
                        pattern_views.encodings()
                    );

                    if (plan.workers > 1) {
                        compute_trim_parallel<true, false>(
                            subject_views, pattern_views,
                            vectorize_length, negate_value,
                            subject_converter, pattern_converter,
                            subject_storage, pattern_storage,
                            subjects, patterns, pattern_set, plan,
                            parallel_outputs, output
                        );
                    }
                    else {
                        compute_trim<true, false>(
                            subject_views, pattern_views,
                            vectorize_length, negate_value,
                            subject_converter, pattern_converter,
                            subject_storage, pattern_storage,
                            subjects, patterns, pattern_set, output
                        );
                    }
                }

                result = entry_protections.reprotect_one(
                    io::finalize(std::move(output)),
                    result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_warnings(recycling_warning);
    );
}


/**
 * Trim characters from a charclass from the right of the string
 *
 * @param str character vector
 * @param pattern character vector
 * @return character vector
 *
 * @version 0.1-?? (Bartek Tartanus)
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-04)
 *          Use ci__trim_leftright
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-06-10) negate
*/
CHARR_ENTRYPOINT SEXP ci_trim_right(
    SEXP str, SEXP pattern, SEXP negate
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();


    const bool negate_value = ci__prepare_arg_logical_1_notNA_r(
        negate, "negate"
    );
    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    pattern = entry_protections.protect_one(
        ci__prepare_arg_string_r(pattern, "pattern")
    );


    bool recycling_warning = false;

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
        shared::CharacterClassSet pattern_set;
        charport::charvec::Store output;
        TrimChunkOutputs parallel_outputs;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t subject_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                const R_len_t pattern_length = io::checked_r_len(
                    XLENGTH(pattern), "character vectors"
                );
                const R_len_t vectorize_length = recycling_length(
                    subject_length, pattern_length, recycling_warning
                );
                const shared::ParallelPlan plan = shared::parallel_plan(
                    true, vectorize_length
                );
                subject_reader.reset(str);
                if (subject_reader.size() != subject_length) {
                    throw std::runtime_error(
                        "Reader length changed during character-class trimming"
                    );
                }

                subject_views.resize(subject_length);
                if (subject_length > 0) {
                    subject_reader.views(
                        0, subject_length,
                        subject_views.ptrs(), subject_views.lengths(),
                        subject_views.encodings()
                    );
                }
                if (pattern_length > 0) {
                    pattern_reader.reset(pattern);
                    if (pattern_reader.size() != pattern_length) {
                        throw std::runtime_error(
                            "Reader length changed during character-class trimming"
                        );
                    }

                    pattern_views.resize(pattern_length);
                    pattern_reader.views(
                        0, pattern_length,
                        pattern_views.ptrs(), pattern_views.lengths(),
                        pattern_views.encodings()
                    );

                    if (plan.workers > 1) {
                        compute_trim_parallel<false, true>(
                            subject_views, pattern_views,
                            vectorize_length, negate_value,
                            subject_converter, pattern_converter,
                            subject_storage, pattern_storage,
                            subjects, patterns, pattern_set, plan,
                            parallel_outputs, output
                        );
                    }
                    else {
                        compute_trim<false, true>(
                            subject_views, pattern_views,
                            vectorize_length, negate_value,
                            subject_converter, pattern_converter,
                            subject_storage, pattern_storage,
                            subjects, patterns, pattern_set, output
                        );
                    }
                }

                result = entry_protections.reprotect_one(
                    io::finalize(std::move(output)),
                    result_index
                );
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END(
        emit_warnings(recycling_warning);
    );
}

} } // namespace charr::altrep_backend
