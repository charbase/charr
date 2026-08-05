
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
#include "ci_parallel.h"
#include "io/reader_utils.h"
#include "io/string_view.h"
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
    if (value.enc == CETYPE_EXT_ASCII) {
        return CiSubFrameInput{data, length, false, true, false};
    }
    if (value.enc == CETYPE_EXT_BYTES)
        throw StriException(MSG__BYTESENC);

    if (value.enc == CETYPE_EXT_UTF8 ||
            value.enc == CETYPE_EXT_ASCII_OR_UTF8) {
        const bool ascii = value.enc == CETYPE_EXT_ASCII_OR_UTF8 &&
            io::is_ascii(data, static_cast<std::size_t>(length));
        if (!ascii && STRI__ENC_HAS_BOM_UTF8(data, length)) {
            data += 3;
            length -= 3;
        }
        return CiSubFrameInput{data, length, false, ascii, false};
    }

    shared::ByteView output;
    if (value.enc == CETYPE_EXT_LATIN1) {
        output = converter.latin1(data, length);
    }
    else if (value.enc == CETYPE_EXT_NATIVE) {
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


CHARR_NEUTRAL_HELPER std::size_t ci__sub_no_slot() noexcept
{
    return static_cast<std::size_t>(-1);
}


CHARR_NEUTRAL_HELPER CiSubFrameInput ci__sub_direct_frame_input(
    const charport::StrView& value
) noexcept
{
    if (value.is_na())
        return CiSubFrameInput{nullptr, 0, true, false, false};

    const char* data = value.ptr;
    R_len_t length = value.len;
    if (value.enc == CETYPE_EXT_ASCII)
        return CiSubFrameInput{data, length, false, true, false};
    if (value.enc == CETYPE_EXT_UTF8 ||
            value.enc == CETYPE_EXT_ASCII_OR_UTF8) {
        const bool ascii = value.enc == CETYPE_EXT_ASCII_OR_UTF8 &&
            io::is_ascii(data, static_cast<std::size_t>(length));
        if (!ascii && STRI__ENC_HAS_BOM_UTF8(data, length)) {
            data += 3;
            length -= 3;
        }
        return CiSubFrameInput{data, length, false, ascii, false};
    }
    return CiSubFrameInput{nullptr, 0, true, false, false};
}


CHARR_CXX_HELPER void ci__sub_preflight_frame_inputs(
    const charport::StrViews& values,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    std::vector<std::size_t>& converted_slots,
    std::vector<shared::StringView>& converted_values
)
{
    const R_xlen_t size = values.size();
    for (R_xlen_t i = 0; i < size; ++i) {
        const CiSubFrameInput input = ci__sub_normalize_frame_input(
            values[i], converter
        );
        if (!input.converted)
            continue;
        const CiSubFrameInput stable = ci__sub_stabilize_frame_input(
            input, storage
        );
        if (converted_slots.empty()) {
            converted_slots.assign(
                static_cast<std::size_t>(size), ci__sub_no_slot()
            );
        }
        converted_slots[static_cast<std::size_t>(i)] =
            converted_values.size();
        converted_values.push_back(shared::StringView{
            stable.data, stable.length, shared::StringEncoding::utf8
        });
    }
}


CHARR_CXX_HELPER void ci__sub_preflight_frame_input_at(
    const charport::StrViews& values, R_len_t index,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    std::vector<std::size_t>& converted_slots,
    std::vector<shared::StringView>& converted_values,
    std::vector<unsigned char>& ready
)
{
    const std::size_t position = static_cast<std::size_t>(index);
    if (ready[position])
        return;
    const CiSubFrameInput input = ci__sub_normalize_frame_input(
        values[index], converter
    );
    if (input.converted) {
        const CiSubFrameInput stable = ci__sub_stabilize_frame_input(
            input, storage
        );
        if (converted_slots.empty()) {
            converted_slots.assign(
                static_cast<std::size_t>(values.size()), ci__sub_no_slot()
            );
        }
        converted_slots[position] = converted_values.size();
        converted_values.push_back(shared::StringView{
            stable.data, stable.length, shared::StringEncoding::utf8
        });
    }
    ready[position] = 1;
}


CHARR_NEUTRAL_HELPER CiSubFrameInput ci__sub_parallel_frame_input_at(
    const charport::StrViews& values,
    const std::vector<std::size_t>& converted_slots,
    const std::vector<shared::StringView>& converted_values,
    R_len_t index
) noexcept
{
    if (!converted_slots.empty()) {
        const std::size_t slot = converted_slots[
            static_cast<std::size_t>(index)
        ];
        if (slot != ci__sub_no_slot()) {
            const shared::StringView& value = converted_values[slot];
            return CiSubFrameInput{
                value.ptr, value.len, false, false, false
            };
        }
    }
    return ci__sub_direct_frame_input(values[index]);
}


CHARR_CXX_HELPER void ci__sub_extract_one_parallel(
    const CiSubFrameInput& value,
    R_len_t current_from, R_len_t current_to, bool use_length,
    shared::substring::Utf8Indexer& indexer,
    io::ParallelOutputBuilder& builder,
    unsigned worker, R_len_t output_index,
    R_len_t& negative_lengths
)
{
    if (value.is_na || current_from == NA_INTEGER ||
            current_to == NA_INTEGER) {
        builder.set_na(worker, output_index);
        return;
    }
    if (use_length) {
        if (current_to == 0) {
            builder.set(
                worker, output_index, "", 0, CETYPE_EXT_ASCII
            );
            return;
        }
        if (current_to < 0) {
            builder.set_na(worker, output_index);
            ++negative_lengths;
            return;
        }
        current_to = shared::substring::length_endpoint(
            current_from, current_to
        );
    }

    indexer.reset(value.data, value.length, value.is_ascii);
    const shared::substring::ByteRange range =
        indexer.range(current_from, current_to);
    if (range.end > range.begin) {
        builder.set_validated(
            worker, output_index,
            charport::StrView{
                value.data+range.begin,
                range.end-range.begin,
                value.is_ascii
                    ? CETYPE_EXT_ASCII
                    : CETYPE_EXT_ASCII_OR_UTF8
            }
        );
    }
    else {
        builder.set(
            worker, output_index, "", 0, CETYPE_EXT_ASCII
        );
    }
}


class CiSubBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER CiSubBody(
        const charport::StrViews& values,
        const std::vector<std::size_t>& converted_slots,
        const std::vector<shared::StringView>& converted_values,
        R_len_t str_length,
        const int* from, R_len_t from_length,
        const int* to, R_len_t to_length,
        const int* lengths, R_len_t lengths_length,
        std::vector<R_len_t>& negative_lengths,
        io::ParallelOutputBuilder& builder
    ) noexcept
        : values_(values), converted_slots_(converted_slots),
          converted_values_(converted_values), str_length_(str_length),
          from_(from), from_length_(from_length),
          to_(to), to_length_(to_length),
          lengths_(lengths), lengths_length_(lengths_length),
          negative_lengths_(negative_lengths), builder_(builder)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        shared::substring::Utf8Indexer indexer;
        // Bound to this worker's slot, so it spans every chunk it draws.
        R_len_t& negative_lengths = negative_lengths_[context.worker];
        while (context.next_chunk()) {
            for (R_xlen_t task = context.begin; task < context.end; ++task) {
                const R_len_t i = static_cast<R_len_t>(task);
                const CiSubFrameInput value =
                    ci__sub_parallel_frame_input_at(
                        values_, converted_slots_, converted_values_,
                        i % str_length_
                    );
                ci__sub_extract_one_parallel(
                    value, from_[i % from_length_],
                    to_ ? to_[i % to_length_]
                        : lengths_[i % lengths_length_],
                    lengths_ != nullptr, indexer, builder_, context.worker, i,
                    negative_lengths
                );
            }
        }
    }

private:
    const charport::StrViews& values_;
    const std::vector<std::size_t>& converted_slots_;
    const std::vector<shared::StringView>& converted_values_;
    R_len_t str_length_;
    const int* from_;
    R_len_t from_length_;
    const int* to_;
    R_len_t to_length_;
    const int* lengths_;
    R_len_t lengths_length_;
    std::vector<R_len_t>& negative_lengths_;
    io::ParallelOutputBuilder& builder_;
};


CHARR_CXX_HELPER void ci__sub_replace_one_parallel(
    const CiSubFrameInput& source,
    const CiSubFrameInput& replacement,
    R_len_t current_from, R_len_t current_to,
    bool use_length, bool omit_na,
    shared::substring::Utf8Indexer& indexer,
    io::ParallelOutputBuilder& builder,
    unsigned worker, R_len_t output_index
)
{
    if (source.is_na) {
        builder.set_na(worker, output_index);
        return;
    }
    if (current_from == NA_INTEGER || current_to == NA_INTEGER ||
            replacement.is_na) {
        if (omit_na) {
            builder.set_validated(
                worker, output_index,
                charport::StrView{
                    source.length == 0 ? "" : source.data,
                    source.length,
                    source.is_ascii ? CETYPE_EXT_ASCII : CETYPE_EXT_UTF8
                }
            );
        }
        else {
            builder.set_na(worker, output_index);
        }
        return;
    }
    if (use_length && current_to < 0) {
        builder.set_validated(
            worker, output_index,
            charport::StrView{
                source.length == 0 ? "" : source.data,
                source.length,
                source.is_ascii ? CETYPE_EXT_ASCII : CETYPE_EXT_UTF8
            }
        );
        return;
    }
    if (use_length) {
        current_to = current_to <= 0
            ? 0
            : shared::substring::length_endpoint(
                current_from, current_to
            );
    }

    indexer.reset(source.data, source.length, source.is_ascii);
    shared::substring::ByteRange range =
        indexer.range(current_from, current_to);
    if (range.end < range.begin)
        range.end = range.begin;

    const std::size_t prefix = static_cast<std::size_t>(range.begin);
    const std::size_t replacement_length =
        static_cast<std::size_t>(replacement.length);
    const std::size_t suffix = static_cast<std::size_t>(
        source.length-range.end
    );
    std::size_t output_size = shared::substring::checked_output_size(
        prefix, replacement_length
    );
    output_size = shared::substring::checked_output_size(
        output_size, suffix
    );
    const bool output_ascii =
        (source.is_ascii || io::is_ascii(source.data, prefix)) &&
        (replacement.is_ascii ||
         io::is_ascii(replacement.data, replacement_length)) &&
        (source.is_ascii ||
         io::is_ascii(source.data+range.end, suffix));
    char* output = builder.reserve(
        worker, output_index, output_size,
        output_ascii ? CETYPE_EXT_ASCII : CETYPE_EXT_UTF8
    );
    if (prefix > 0)
        std::memcpy(output, source.data, prefix);
    if (replacement_length > 0) {
        std::memcpy(
            output+prefix, replacement.data, replacement_length
        );
    }
    if (suffix > 0) {
        std::memcpy(
            output+prefix+replacement_length,
            source.data+range.end, suffix
        );
    }
}


class CiSubReplacementBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER CiSubReplacementBody(
        const charport::StrViews& source_values,
        const std::vector<std::size_t>& source_converted_slots,
        const std::vector<shared::StringView>& source_converted_values,
        const charport::StrViews& replacement_values,
        const std::vector<std::size_t>& replacement_converted_slots,
        const std::vector<shared::StringView>& replacement_converted_values,
        R_len_t source_length, R_len_t replacement_length,
        const int* from, R_len_t from_length,
        const int* to, R_len_t to_length,
        const int* lengths, R_len_t lengths_length,
        bool omit_na, io::ParallelOutputBuilder& builder
    ) noexcept
        : source_values_(source_values),
          source_converted_slots_(source_converted_slots),
          source_converted_values_(source_converted_values),
          replacement_values_(replacement_values),
          replacement_converted_slots_(replacement_converted_slots),
          replacement_converted_values_(replacement_converted_values),
          source_length_(source_length),
          replacement_length_(replacement_length),
          from_(from), from_length_(from_length),
          to_(to), to_length_(to_length),
          lengths_(lengths), lengths_length_(lengths_length),
          omit_na_(omit_na), builder_(builder)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        shared::substring::Utf8Indexer indexer;
        while (context.next_chunk()) {
            for (R_xlen_t task = context.begin; task < context.end; ++task) {
                const R_len_t i = static_cast<R_len_t>(task);
                ci__sub_replace_one_parallel(
                    ci__sub_parallel_frame_input_at(
                        source_values_, source_converted_slots_,
                        source_converted_values_, i % source_length_
                    ),
                    ci__sub_parallel_frame_input_at(
                        replacement_values_, replacement_converted_slots_,
                        replacement_converted_values_,
                        i % replacement_length_
                    ),
                    from_[i % from_length_],
                    to_ ? to_[i % to_length_]
                        : lengths_[i % lengths_length_],
                    lengths_ != nullptr, omit_na_, indexer, builder_,
                    context.worker, i
                );
            }
        }
    }

private:
    const charport::StrViews& source_values_;
    const std::vector<std::size_t>& source_converted_slots_;
    const std::vector<shared::StringView>& source_converted_values_;
    const charport::StrViews& replacement_values_;
    const std::vector<std::size_t>& replacement_converted_slots_;
    const std::vector<shared::StringView>& replacement_converted_values_;
    R_len_t source_length_;
    R_len_t replacement_length_;
    const int* from_;
    R_len_t from_length_;
    const int* to_;
    R_len_t to_length_;
    const int* lengths_;
    R_len_t lengths_length_;
    bool omit_na_;
    io::ParallelOutputBuilder& builder_;
};


class CiSubReplacementAllScalarBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER CiSubReplacementAllScalarBody(
        const charport::StrViews& source_values,
        const std::vector<std::size_t>& source_converted_slots,
        const std::vector<shared::StringView>& source_converted_values,
        const CiSubFrameInput& replacement,
        R_len_t from, R_len_t to, bool omit_na,
        io::ParallelOutputBuilder& builder
    ) noexcept
        : source_values_(source_values),
          source_converted_slots_(source_converted_slots),
          source_converted_values_(source_converted_values),
          replacement_(replacement), from_(from), to_(to),
          omit_na_(omit_na), builder_(builder)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        shared::substring::Utf8Indexer indexer;
        while (context.next_chunk()) {
            for (R_xlen_t task = context.begin; task < context.end; ++task) {
                const R_len_t outer = static_cast<R_len_t>(task);
                ci__sub_replace_one_parallel(
                    ci__sub_parallel_frame_input_at(
                        source_values_, source_converted_slots_,
                        source_converted_values_, outer
                    ),
                    replacement_, from_, to_, false, omit_na_, indexer,
                    builder_, context.worker, outer
                );
            }
        }
    }

private:
    const charport::StrViews& source_values_;
    const std::vector<std::size_t>& source_converted_slots_;
    const std::vector<shared::StringView>& source_converted_values_;
    CiSubFrameInput replacement_;
    R_len_t from_;
    R_len_t to_;
    bool omit_na_;
    io::ParallelOutputBuilder& builder_;
};


CHARR_CXX_HELPER std::size_t ci__sub_append_int_values(
    std::vector<int>& output, const int* values, R_len_t length
)
{
    const std::size_t offset = output.size();
    for (R_len_t i = 0; i < length; ++i)
        output.push_back(values[i]);
    return offset;
}


class CiSubAllBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER CiSubAllBody(
        const charport::StrViews& source_values,
        const std::vector<std::size_t>& source_converted_slots,
        const std::vector<shared::StringView>& source_converted_values,
        R_len_t source_length,
        const std::vector<std::size_t>& plans,
        const std::vector<int>& from_values,
        const std::vector<int>& to_values,
        const std::vector<int>& length_values,
        bool ignore_negative_length,
        std::vector<io::OutputStore>& stores
    ) noexcept
        : source_values_(source_values),
          source_converted_slots_(source_converted_slots),
          source_converted_values_(source_converted_values),
          source_length_(source_length), plans_(plans),
          from_values_(from_values), to_values_(to_values),
          length_values_(length_values),
          ignore_negative_length_(ignore_negative_length), stores_(stores)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        shared::substring::Utf8Indexer indexer;
        io::OutputBuilder builder(0);
        while (context.next_chunk()) {
            for (R_xlen_t task = context.begin; task < context.end; ++task) {
                const R_len_t outer = static_cast<R_len_t>(task);
                const std::size_t plan = static_cast<std::size_t>(outer)*7;
                const R_len_t inner_vectorize_length = plans_[plan+6];
                if (inner_vectorize_length <= 0) {
                    builder.reset(0);
                    stores_[static_cast<std::size_t>(outer)] =
                        builder.release_store();
                    continue;
                }

                const CiSubFrameInput source =
                    ci__sub_parallel_frame_input_at(
                        source_values_, source_converted_slots_,
                        source_converted_values_, outer % source_length_
                    );
                const R_len_t from_length = plans_[plan+1];
                const R_len_t to_length = plans_[plan+3];
                const R_len_t length_length = plans_[plan+5];
                const int* from = &from_values_[
                    static_cast<std::size_t>(plans_[plan])
                ];
                const int* to = to_length > 0
                    ? &to_values_[static_cast<std::size_t>(plans_[plan+2])]
                    : nullptr;
                const int* lengths = length_length > 0
                    ? &length_values_[
                        static_cast<std::size_t>(plans_[plan+4])
                    ]
                    : nullptr;

                R_len_t negative_lengths = 0;
                if (lengths && !source.is_na) {
                    for (R_len_t i = 0; i < inner_vectorize_length; ++i) {
                        const R_len_t current_from = from[i % from_length];
                        const R_len_t current_length =
                            lengths[i % length_length];
                        if (current_from != NA_INTEGER &&
                                current_length != NA_INTEGER &&
                                current_length < 0) {
                            ++negative_lengths;
                        }
                    }
                }
                const R_len_t output_length = ignore_negative_length_
                    ? inner_vectorize_length-negative_lengths
                    : inner_vectorize_length;
                builder.reset(output_length);
                R_len_t output = 0;
                for (R_len_t i = 0; i < inner_vectorize_length; ++i) {
                    R_len_t current_from = from[i % from_length];
                    R_len_t current_to = to
                        ? to[i % to_length]
                        : lengths[i % length_length];
                    if (ignore_negative_length_ && !source.is_na &&
                            current_from != NA_INTEGER &&
                            current_to != NA_INTEGER && lengths &&
                            current_to < 0) {
                        continue;
                    }
                    if (source.is_na || current_from == NA_INTEGER ||
                            current_to == NA_INTEGER) {
                        builder.set_na(output++);
                        continue;
                    }
                    if (lengths) {
                        if (current_to == 0) {
                            builder.set(
                                output++, "", 0, CETYPE_EXT_ASCII
                            );
                            continue;
                        }
                        if (current_to < 0) {
                            builder.set_na(output++);
                            continue;
                        }
                        current_to = shared::substring::length_endpoint(
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
                                    ? CETYPE_EXT_ASCII
                                    : CETYPE_EXT_ASCII_OR_UTF8
                            }
                        );
                    }
                    else {
                        builder.set(
                            output++, "", 0, CETYPE_EXT_ASCII
                        );
                    }
                }
                stores_[static_cast<std::size_t>(outer)] =
                    builder.release_store();
            }
        }
    }

private:
    const charport::StrViews& source_values_;
    const std::vector<std::size_t>& source_converted_slots_;
    const std::vector<shared::StringView>& source_converted_values_;
    R_len_t source_length_;
    const std::vector<std::size_t>& plans_;
    const std::vector<int>& from_values_;
    const std::vector<int>& to_values_;
    const std::vector<int>& length_values_;
    bool ignore_negative_length_;
    std::vector<io::OutputStore>& stores_;
};


CHARR_NEUTRAL_HELPER shared::StringView ci__sub_shared_frame_input(
    const CiSubFrameInput& input
) noexcept
{
    return shared::StringView{
        input.length == 0 ? "" : input.data,
        input.is_na ? shared::missing_string_length : input.length,
        input.is_na
            ? shared::StringEncoding::missing
            : input.is_ascii
                ? shared::StringEncoding::ascii
                : shared::StringEncoding::utf8
    };
}


CHARR_CXX_HELPER shared::substring::ReplacementWarning
ci__sub_validate_replacement_all(
    const CiSubFrameInput& source,
    const shared::StringView* replacements, R_len_t replacement_length,
    const int* from, R_len_t from_length,
    const int* to, R_len_t to_length,
    const int* lengths, R_len_t lengths_length,
    R_len_t vectorize_length, bool omit_na
)
{
    using shared::substring::ReplacementWarning;

    if (source.is_na || vectorize_length <= 0)
        return ReplacementWarning::none;
    if (replacement_length <= 0)
        return ReplacementWarning::replacement_zero;

    if (!omit_na) {
        for (R_len_t i = 0; i < vectorize_length; ++i) {
            const int current_from = from[i % from_length];
            const int current_to = to
                ? to[i % to_length]
                : lengths[i % lengths_length];
            if (current_from == NA_INTEGER || current_to == NA_INTEGER)
                return ReplacementWarning::none;
        }
        for (R_len_t i = 0; i < vectorize_length; ++i) {
            if (replacements[i % replacement_length].is_na())
                return ReplacementWarning::none;
        }
    }

    shared::substring::Utf8Indexer indexer;
    indexer.reset(source.data, source.length, source.is_ascii);
    const int source_codepoints = indexer.codepoint_count();
    int replaced = 0;
    int last_position = 0;
    int byte_position = 0;
    std::size_t output_size = 0;
    for (R_len_t i = 0; i < vectorize_length; ++i) {
        int current_from = from[i % from_length];
        int current_to = to
            ? to[i % to_length]
            : lengths[i % lengths_length];
        const shared::StringView& replacement =
            replacements[i % replacement_length];
        if (current_from == NA_INTEGER || current_to == NA_INTEGER ||
                replacement.is_na() || (!to && current_to < 0)) {
            continue;
        }

        ++replaced;
        current_from = shared::substring::replacement_all_from(
            current_from, source_codepoints
        );
        current_to = shared::substring::replacement_all_to(
            current_to, lengths != nullptr,
            current_from, source_codepoints
        );
        if (last_position > current_from) {
            throw std::invalid_argument(
                "index ranges must be sorted and mutually disjoint"
            );
        }

        const int begin_byte = indexer.forward(current_from);
        output_size = shared::substring::checked_output_size(
            output_size,
            static_cast<std::size_t>(begin_byte-byte_position)
        );
        output_size = shared::substring::checked_output_size(
            output_size, static_cast<std::size_t>(replacement.len)
        );
        byte_position = indexer.forward(current_to);
        last_position = current_to;
    }
    shared::substring::checked_output_size(
        output_size,
        static_cast<std::size_t>(source.length-byte_position)
    );
    return replaced > 0 && vectorize_length % replacement_length != 0
        ? ReplacementWarning::recycling
        : ReplacementWarning::none;
}


class CiSubReplacementAllBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER CiSubReplacementAllBody(
        const charport::StrViews& source_values,
        const std::vector<std::size_t>& source_converted_slots,
        const std::vector<shared::StringView>& source_converted_values,
        R_len_t source_length,
        const std::vector<std::size_t>& plans,
        const std::vector<shared::StringView>& replacement_values,
        const std::vector<int>& from_values,
        const std::vector<int>& to_values,
        const std::vector<int>& length_values,
        bool omit_na,
        std::vector<io::OutputStore>& stores,
        std::vector<int>& errors
    ) noexcept
        : source_values_(source_values),
          source_converted_slots_(source_converted_slots),
          source_converted_values_(source_converted_values),
          source_length_(source_length), plans_(plans),
          replacement_values_(replacement_values),
          from_values_(from_values), to_values_(to_values),
          length_values_(length_values),
          omit_na_(omit_na), stores_(stores), errors_(errors)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        shared::substring::ReplacementAssembler assembler;
        io::OutputBuilder builder(0);
        while (context.next_chunk()) {
            for (R_xlen_t task = context.begin; task < context.end; ++task) {
                const R_len_t outer = static_cast<R_len_t>(task);
                try {
                    const CiSubFrameInput source =
                        ci__sub_parallel_frame_input_at(
                            source_values_, source_converted_slots_,
                            source_converted_values_,
                            outer % source_length_
                        );
                    builder.reset(1);
                    if (source.is_na) {
                        builder.set_na(0);
                    }
                    else {
                        const std::size_t plan =
                            static_cast<std::size_t>(outer)*9;
                        const R_len_t from_length = static_cast<R_len_t>(
                            plans_[plan+1]
                        );
                        const R_len_t to_length = static_cast<R_len_t>(
                            plans_[plan+3]
                        );
                        const R_len_t length_length = static_cast<R_len_t>(
                            plans_[plan+5]
                        );
                        const R_len_t vectorize_length =
                            static_cast<R_len_t>(plans_[plan+6]);
                        const R_len_t replacement_length =
                            static_cast<R_len_t>(plans_[plan+8]);
                        const int* from = from_length > 0
                            ? &from_values_[plans_[plan]] : nullptr;
                        const int* to = to_length > 0
                            ? &to_values_[plans_[plan+2]] : nullptr;
                        const int* lengths = length_length > 0
                            ? &length_values_[plans_[plan+4]] : nullptr;
                        const shared::StringView* replacements =
                            replacement_length > 0
                                ? &replacement_values_[plans_[plan+7]]
                                : nullptr;
                        const shared::substring::ReplacementResult output =
                            assembler.build(
                                ci__sub_shared_frame_input(source),
                                replacements, replacement_length,
                                from, from_length, to, to_length,
                                lengths, length_length,
                                vectorize_length, omit_na_
                            );
                        if (output.value.is_na()) {
                            builder.set_na(0);
                        }
                        else {
                            builder.set_validated(
                                0,
                                charport::StrView{
                                    output.value.len == 0
                                        ? "" : output.value.ptr,
                                    output.value.len,
                                    output.value.enc ==
                                            shared::StringEncoding::ascii
                                        ? CETYPE_EXT_ASCII
                                        : CETYPE_EXT_UTF8
                                }
                            );
                        }
                    }
                    stores_[static_cast<std::size_t>(outer)] =
                        builder.release_store();
                }
                catch (const std::exception& error) {
                    errors_[static_cast<std::size_t>(outer)] = 1;
                    stores_[static_cast<std::size_t>(outer)] =
                        io::OutputStore::scalar(
                            error.what(), std::strlen(error.what())+1,
                            CETYPE_EXT_BYTES
                        );
                }
                catch (...) {
                    errors_[static_cast<std::size_t>(outer)] = 1;
                    stores_[static_cast<std::size_t>(outer)] =
                        io::OutputStore::scalar(
                            "unknown C++ exception", 22,
                            CETYPE_EXT_BYTES
                        );
                }
            }
        }
    }

private:
    const charport::StrViews& source_values_;
    const std::vector<std::size_t>& source_converted_slots_;
    const std::vector<shared::StringView>& source_converted_values_;
    R_len_t source_length_;
    const std::vector<std::size_t>& plans_;
    const std::vector<shared::StringView>& replacement_values_;
    const std::vector<int>& from_values_;
    const std::vector<int>& to_values_;
    const std::vector<int>& length_values_;
    bool omit_na_;
    std::vector<io::OutputStore>& stores_;
    std::vector<int>& errors_;
};


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
    const std::vector<shared::substring::ReplacementWarning>& warnings,
    std::size_t count
) noexcept
{
    if (count > warnings.size())
        count = warnings.size();
    for (std::size_t i = 0; i < count; ++i) {
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


CHARR_R_HELPER void ci__sub_emit_replacement_warnings_r(
    const std::vector<shared::substring::ReplacementWarning>& warnings
) noexcept
{
    ci__sub_emit_replacement_warnings_r(warnings, warnings.size());
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
        std::vector<std::size_t> converted_slots;
        std::vector<shared::StringView> converted_values;
        io::OutputBuilder builder(0);
        io::ParallelOutputBuilder parallel_builder;
        io::OutputBuilder filtered(0);
        io::OutputStore output_store(0, 0);
        std::vector<R_len_t> negative_length_counts;

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
                const shared::ParallelPlan plan = shared::parallel_plan(
                    true, vectorize_len
                );
                if (!scalar_bounds && plan.workers == 1 &&
                        vectorize_len > 0) {
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

                if (plan.workers > 1) {
                    ci__sub_preflight_frame_inputs(
                        views, converter, storage,
                        converted_slots, converted_values
                    );
                }
                else if (!scalar_bounds && vectorize_len > 0) {
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

                R_len_t negative_lengths = 0;
                if (plan.workers > 1) {
                    negative_length_counts.assign(plan.workers, 0);
                    parallel_builder.reset(vectorize_len, plan.workers);
                    CiSubBody body(
                        views, converted_slots, converted_values, str_len,
                        from_tab, from_len, to_tab, to_len,
                        length_tab, length_len,
                        negative_length_counts, parallel_builder
                    );
                    shared::run_parallel(plan, vectorize_len, body);
                    for (unsigned worker = 0;
                            worker < plan.workers; ++worker) {
                        negative_lengths += negative_length_counts[worker];
                    }
                    output_store = parallel_builder.release_store();
                }
                else {
                    builder.reset(vectorize_len);
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
                                    i, "", 0, CETYPE_EXT_ASCII
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
                                        ? CETYPE_EXT_ASCII
                                        : CETYPE_EXT_ASCII_OR_UTF8
                                }
                            );
                        }
                        else {
                            builder.set(
                                i, "", 0, CETYPE_EXT_ASCII
                            );
                        }
                    }
                    output_store = builder.release_store();
                }

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
                        const CiSubFrameInput value = plan.workers > 1
                            ? ci__sub_parallel_frame_input_at(
                                views, converted_slots, converted_values,
                                i % str_len
                            )
                            : inputs[
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
        std::vector<std::size_t> source_converted_slots;
        std::vector<shared::StringView> source_converted_values;
        std::vector<std::size_t> replacement_converted_slots;
        std::vector<shared::StringView> replacement_converted_values;
        io::OutputBuilder builder(0);
        io::ParallelOutputBuilder parallel_builder;
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
                const shared::ParallelPlan plan = shared::parallel_plan(
                    true, vectorize_len
                );
                if (!scalar_bounds && plan.workers == 1 &&
                        vectorize_len > 0) {
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
                    if (plan.workers > 1) {
                        ci__sub_preflight_frame_inputs(
                            replacement_views, replacement_converter,
                            replacement_storage,
                            replacement_converted_slots,
                            replacement_converted_values
                        );
                        ci__sub_preflight_frame_inputs(
                            source_views, source_converter, source_storage,
                            source_converted_slots, source_converted_values
                        );
                    }
                    else {
                        scalar_replacement = ci__sub_normalize_frame_input(
                            replacement_views[0], replacement_converter
                        );
                    }
                }
                else if (vectorize_len > 0) {
                    if (plan.workers > 1) {
                        ci__sub_preflight_frame_inputs(
                            source_views, source_converter, source_storage,
                            source_converted_slots, source_converted_values
                        );
                        ci__sub_preflight_frame_inputs(
                            replacement_views, replacement_converter,
                            replacement_storage,
                            replacement_converted_slots,
                            replacement_converted_values
                        );
                    }
                    else {
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
                }

                if (plan.workers > 1) {
                    parallel_builder.reset(vectorize_len, plan.workers);
                    CiSubReplacementBody body(
                        source_views, source_converted_slots,
                        source_converted_values,
                        replacement_views, replacement_converted_slots,
                        replacement_converted_values,
                        str_len, value_len,
                        from_tab, from_len, to_tab, to_len,
                        length_tab, length_len,
                        omit_na_1, parallel_builder
                    );
                    shared::run_parallel(plan, vectorize_len, body);
                    output_store = parallel_builder.release_store();
                }
                else {
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
                                            ? CETYPE_EXT_ASCII
                                            : CETYPE_EXT_UTF8
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
                                        ? CETYPE_EXT_ASCII
                                        : CETYPE_EXT_UTF8
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
                                ? CETYPE_EXT_ASCII
                                : CETYPE_EXT_UTF8
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
        std::vector<std::size_t> source_converted_slots;
        std::vector<shared::StringView> source_converted_values;
        std::vector<std::size_t> plans;
        std::vector<int> from_values;
        std::vector<int> to_values;
        std::vector<int> length_values;
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
                const shared::ParallelPlan plan = shared::parallel_plan(
                    true, vectorize_len
                );
                if (vectorize_len > 0) {
                    if (plan.workers == 1) {
                        sources.resize(static_cast<std::size_t>(str_len));
                    }
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

                if (plan.workers == 1) {
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
                                    CETYPE_EXT_ASCII
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
                                        ? CETYPE_EXT_ASCII
                                        : CETYPE_EXT_ASCII_OR_UTF8
                                }
                            );
                        }
                        else {
                            builder.set(
                                output++, "", 0,
                                CETYPE_EXT_ASCII
                            );
                        }
                    }
                    stores.push_back(builder.release_store());
                    callback_protections.release(inner_protected);
                  }
                }
                else {
                    plans.assign(
                        static_cast<std::size_t>(vectorize_len)*7, 0
                    );
                    for (R_len_t outer = 0;
                            outer < vectorize_len; ++outer) {
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
                                1, inner_from_len, inner_endpoint_len
                            );
                        const std::size_t position =
                            static_cast<std::size_t>(outer)*7;
                        plans[position] = ci__sub_append_int_values(
                            from_values, inner_from_tab, inner_from_len
                        );
                        plans[position+1] =
                            static_cast<std::size_t>(inner_from_len);
                        plans[position+2] = ci__sub_append_int_values(
                            to_values, inner_to_tab, inner_to_len
                        );
                        plans[position+3] =
                            static_cast<std::size_t>(inner_to_len);
                        plans[position+4] = ci__sub_append_int_values(
                            length_values, inner_length_tab,
                            inner_length_len
                        );
                        plans[position+5] =
                            static_cast<std::size_t>(inner_length_len);
                        plans[position+6] = static_cast<std::size_t>(
                            inner_vectorize_len
                        );
                        if (inner_vectorize_len > 0) {
                            ci__sub_preflight_frame_input_at(
                                source_views, outer % str_len,
                                converter, storage,
                                source_converted_slots,
                                source_converted_values, source_ready
                            );
                        }
                        callback_protections.release(inner_protected);
                    }
                    for (R_len_t outer = 0;
                            outer < vectorize_len; ++outer) {
                        stores.emplace_back(0, 0);
                    }
                    CiSubAllBody body(
                        source_views, source_converted_slots,
                        source_converted_values, str_len,
                        plans, from_values, to_values, length_values,
                        ignore_negative_length_1, stores
                    );
                    shared::run_parallel(plan, vectorize_len, body);
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
        charport::Reader parallel_replacement_reader;
        charport::Reader scalar_parallel_replacement_reader;
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
        std::vector<std::size_t> source_converted_slots;
        std::vector<shared::StringView> source_converted_values;
        std::vector<std::size_t> plans;
        std::vector<shared::StringView> parallel_replacements;
        std::vector<int> from_values;
        std::vector<int> to_values;
        std::vector<int> length_values;
        std::vector<io::OutputStore> stores;
        std::vector<std::size_t> warning_limits;
        std::vector<int> worker_errors;
        std::vector<shared::substring::ReplacementWarning>
            pending_warnings;
        std::size_t warning_emit_limit = 0;
        bool has_warning_emit_limit = false;
        io::OutputBuilder builder(0);
        io::ParallelOutputBuilder parallel_builder;
        io::OutputStore output_store(0, 0);

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                if (source_size < 0 || source_size > R_LEN_T_MAX) {
                    Rf_error("long character vectors are not supported");
                }
                str_len = static_cast<R_len_t>(source_size);
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
                bool use_parallel_sources = false;
                const bool lists_are_valid =
                    (Rf_isNull(from) || Rf_isVectorList(from)) &&
                    (Rf_isNull(to) || Rf_isVectorList(to)) &&
                    (Rf_isNull(length) || Rf_isVectorList(length)) &&
                    (Rf_isNull(value) || Rf_isVectorList(value));
                if (lists_are_valid) {
                    const bool raw_has_to = !Rf_isNull(to);
                    const bool raw_has_length = !Rf_isNull(length);
                    const int raw_outer_lengths[4] = {
                        str_len, LENGTH(from), LENGTH(value),
                        raw_has_to ? LENGTH(to)
                            : raw_has_length ? LENGTH(length) : 1
                    };
                    bool raw_recycling_warning = false;
                    const R_len_t raw_vectorize_len =
                        shared::substring::recycling_length(
                            raw_outer_lengths,
                            raw_has_to || raw_has_length ? 4 : 3,
                            raw_recycling_warning
                        );
                    use_parallel_sources = shared::parallel_plan(
                        true, raw_vectorize_len
                    ).workers > 1;
                }
                if (!use_parallel_sources) {
                    sources.resize(static_cast<std::size_t>(str_len));
                    for (R_len_t i = 0; i < str_len; ++i) {
                        sources[static_cast<std::size_t>(i)] =
                            ci__sub_stabilize_frame_input(
                                ci__sub_normalize_frame_input(
                                    source_views[i], source_converter
                                ),
                                source_storage
                            );
                    }
                }
                else {
                    ci__sub_preflight_frame_inputs(
                        source_views, source_converter, source_storage,
                        source_converted_slots, source_converted_values
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
                const shared::ParallelPlan plan = shared::parallel_plan(
                    true, vectorize_len
                );
                if (outer_recycling_warning) {
                    pending_warnings.push_back(
                        shared::substring::ReplacementWarning::
                            recycling_rule
                    );
                }

                try {
                 if (plan.workers == 1) {
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
                                            ? CETYPE_EXT_ASCII
                                            : CETYPE_EXT_UTF8
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
                                ? CETYPE_EXT_ASCII
                                : CETYPE_EXT_UTF8
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
                                    ? CETYPE_EXT_ASCII
                                    : CETYPE_EXT_UTF8
                            }
                        );
                    }
                    callback_protections.release(inner_protected);
                    callback_protections.release(replacement_protected);
                  }
                  output_store = builder.release_store();
                 }
                 else if (scalar_fast_path) {
                    bool needs_replacement = false;
                    for (R_len_t outer = 0;
                            outer < vectorize_len; ++outer) {
                        const CiSubFrameInput source =
                            ci__sub_parallel_frame_input_at(
                                source_views, source_converted_slots,
                                source_converted_values, outer
                            );
                        if (!source.is_na) {
                            needs_replacement = true;
                            break;
                        }
                    }

                    if (needs_replacement) {
                        SEXP inner_value =
                            callback_protections.protect_one(
                                ci__prepare_arg_string_r(
                                    scalar_value, "str"
                                )
                            );
                        const R_xlen_t replacement_size =
                            XLENGTH(inner_value);
                        if (replacement_size < 0 ||
                                replacement_size > R_LEN_T_MAX) {
                            Rf_error(
                                "long character vectors are not supported"
                            );
                        }
                        scalar_parallel_replacement_reader.reset(inner_value);
                        if (scalar_parallel_replacement_reader.size() !=
                                replacement_size) {
                            throw std::runtime_error(
                                "character vector length changed during an operation"
                            );
                        }
                        replacement_views.resize(replacement_size);
                        scalar_parallel_replacement_reader.views(
                            0, replacement_size,
                            replacement_views.ptrs(),
                            replacement_views.lengths(),
                            replacement_views.encodings()
                        );
                        scalar_replacement_input =
                            ci__sub_stabilize_frame_input(
                                ci__sub_normalize_frame_input(
                                    replacement_views[0],
                                    replacement_converter
                                ),
                                replacement_storage
                            );
                    }

                    parallel_builder.reset(vectorize_len, plan.workers);
                    CiSubReplacementAllScalarBody body(
                        source_views, source_converted_slots,
                        source_converted_values, scalar_replacement_input,
                        scalar_from, scalar_to, omit_na_1,
                        parallel_builder
                    );
                    shared::run_parallel(plan, vectorize_len, body);
                    output_store = parallel_builder.release_store();
                 }
                 else {
                    plans.assign(
                        static_cast<std::size_t>(vectorize_len)*9, 0
                    );
                    stores.reserve(static_cast<std::size_t>(vectorize_len));
                    for (R_len_t outer = 0;
                            outer < vectorize_len; ++outer) {
                        stores.emplace_back(0, 0);
                    }
                    warning_limits.assign(
                        static_cast<std::size_t>(vectorize_len),
                        pending_warnings.size()
                    );
                    worker_errors.assign(
                        static_cast<std::size_t>(vectorize_len), 0
                    );
                    SEXP replacement_keepalive =
                        callback_protections.protect_one(
                            Rf_allocVector(VECSXP, vectorize_len)
                        );

                    for (R_len_t outer = 0;
                            outer < vectorize_len; ++outer) {
                        warning_limits[static_cast<std::size_t>(outer)] =
                            pending_warnings.size();
                        const CiSubFrameInput source =
                            ci__sub_parallel_frame_input_at(
                                source_views, source_converted_slots,
                                source_converted_values, outer % str_len
                            );
                        if (source.is_na)
                            continue;

                        const std::size_t position =
                            static_cast<std::size_t>(outer)*9;
                        R_len_t replacement_len = 0;
                        SEXP inner_value =
                            callback_protections.protect_one(
                                ci__prepare_arg_string_r(
                                    VECTOR_ELT(
                                        value, outer % value_list_len
                                    ),
                                    "str"
                                )
                            );
                        SET_VECTOR_ELT(
                            replacement_keepalive, outer, inner_value
                        );
                        const R_xlen_t replacement_size =
                            XLENGTH(inner_value);
                        if (replacement_size < 0 ||
                                replacement_size > R_LEN_T_MAX) {
                            Rf_error(
                                "long character vectors are not supported"
                            );
                        }
                        replacement_len = static_cast<R_len_t>(
                            replacement_size
                        );
                        parallel_replacement_reader.reset(inner_value);
                        if (parallel_replacement_reader.size() !=
                                replacement_size) {
                            throw std::runtime_error(
                                "character vector length changed during an operation"
                            );
                        }
                        replacement_views.resize(replacement_size);
                        parallel_replacement_reader.views(
                            0, replacement_size,
                            replacement_views.ptrs(),
                            replacement_views.lengths(),
                            replacement_views.encodings()
                        );
                        plans[position+7] =
                            parallel_replacements.size();
                        plans[position+8] = static_cast<std::size_t>(
                            replacement_len
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
                            parallel_replacements.push_back(
                                ci__sub_shared_frame_input(replacement)
                            );
                        }
                        callback_protections.release(1);

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
                        const shared::substring::ReplacementWarning warning =
                            ci__sub_validate_replacement_all(
                                source,
                                replacement_len > 0
                                    ? &parallel_replacements[
                                        plans[position+7]
                                    ]
                                    : nullptr,
                                replacement_len,
                                inner_from_tab, inner_from_len,
                                inner_to_tab, inner_to_len,
                                inner_length_tab, inner_length_len,
                                inner_vectorize_len, omit_na_1
                            );
                        if (warning ==
                                shared::substring::ReplacementWarning::
                                    replacement_zero ||
                                warning ==
                                shared::substring::ReplacementWarning::
                                    recycling) {
                            pending_warnings.push_back(warning);
                        }
                        plans[position] = ci__sub_append_int_values(
                            from_values, inner_from_tab, inner_from_len
                        );
                        plans[position+1] =
                            static_cast<std::size_t>(inner_from_len);
                        plans[position+2] = ci__sub_append_int_values(
                            to_values, inner_to_tab, inner_to_len
                        );
                        plans[position+3] =
                            static_cast<std::size_t>(inner_to_len);
                        plans[position+4] = ci__sub_append_int_values(
                            length_values, inner_length_tab,
                            inner_length_len
                        );
                        plans[position+5] =
                            static_cast<std::size_t>(inner_length_len);
                        plans[position+6] = static_cast<std::size_t>(
                            inner_vectorize_len
                        );
                        warning_limits[static_cast<std::size_t>(outer)] =
                            pending_warnings.size();
                        callback_protections.release(inner_protected);
                    }

                    CiSubReplacementAllBody body(
                        source_views, source_converted_slots,
                        source_converted_values, str_len,
                        plans, parallel_replacements,
                        from_values, to_values, length_values,
                        omit_na_1,
                        stores, worker_errors
                    );
                    shared::run_parallel(plan, vectorize_len, body);
                    for (R_len_t outer = 0;
                            outer < vectorize_len; ++outer) {
                        if (worker_errors[
                                static_cast<std::size_t>(outer)
                            ]) {
                            warning_emit_limit = warning_limits[
                                static_cast<std::size_t>(outer)
                            ];
                            has_warning_emit_limit = true;
                            const io::OutputRecord error = stores[
                                static_cast<std::size_t>(outer)
                            ].view(0);
                            throw std::runtime_error(
                                error.ptr == nullptr
                                    ? "unknown C++ exception"
                                    : error.ptr
                            );
                        }
                    }
                    output_store = io::concat_stores(stores);
                 }
                }
                catch (...) {
                    if (has_warning_emit_limit) {
                        ci__sub_emit_replacement_warnings_r(
                            pending_warnings, warning_emit_limit
                        );
                    }
                    else {
                        ci__sub_emit_replacement_warnings_r(
                            pending_warnings
                        );
                    }
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
