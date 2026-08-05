
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
#include "ci_string8buf.h"
#include "ci_ucnv.h"
#include "io/string_view.h"
#include "io/utf8_output.h"
#include "../shared/deferred_warnings.h"
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/string_view.h"
#include "../shared/unwind.h"

#include <charport.h>

#include <cstdint>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <vector>

namespace charr { namespace altrep_backend {

namespace encoding_conversion {

constexpr std::size_t maximum_buffer_length = 2147483647U;

enum class InputKind : unsigned char {
    null_value,
    raw_vector,
    raw_list,
    character
};

struct ByteRecord {
    const char* data;
    R_len_t length;
    bool missing;
};

struct CHARR_OWNER_TYPE RawResult {
    bool missing;
    std::vector<unsigned char> data;

    CHARR_CXX_HELPER RawResult() noexcept
        : missing(true), data() {}
};

CHARR_NEUTRAL_HELPER bool utf8_encoding_name(
    const char* encoding
) noexcept {
    return encoding != nullptr &&
        ucnv_compareNames(encoding, "UTF-8") == 0;
}

CHARR_NEUTRAL_HELPER bool valid_utf8_bytes(
    const char* data,
    R_len_t length,
    bool& ascii,
    bool& embedded_nul
) noexcept {
    ascii = true;
    embedded_nul = false;
    R_len_t i = 0;
    while (i < length) {
        const std::uint8_t byte = static_cast<std::uint8_t>(data[i]);
        if (byte == 0)
            embedded_nul = true;
        if (byte <= 0x7fU) {
            ++i;
            continue;
        }

        ascii = false;
        UChar32 code_point = 0;
        U8_NEXT(data, i, length, code_point);
        if (code_point < 0)
            return false;
    }
    return true;
}

CHARR_CXX_HELPER bool validate_identity_views(
    const charport::StrViews& input,
    bool respect_marks,
    std::vector<cetype_ext_t>& encodings
) {
    const R_xlen_t size = input.size();
    for (R_xlen_t i = 0; i < size; ++i) {
        const charport::StrView value = input[i];
        if (value.is_na()) {
            encodings[static_cast<std::size_t>(i)] = CETYPE_EXT_NA;
            continue;
        }
        if (value.len < 0 || (value.ptr == nullptr && value.len != 0))
            throw std::runtime_error("Reader returned an invalid string view");
        if (respect_marks &&
                value.enc != CETYPE_EXT_ASCII &&
                value.enc != CETYPE_EXT_UTF8 &&
                value.enc != CETYPE_EXT_ASCII_OR_UTF8) {
            return false;
        }
        if (value.enc == CETYPE_EXT_ASCII) {
            encodings[static_cast<std::size_t>(i)] = CETYPE_EXT_ASCII;
            continue;
        }

        bool ascii = false;
        bool embedded_nul = false;
        const char* data = value.ptr == nullptr ? "" : value.ptr;
        if (!valid_utf8_bytes(
                data, value.len, ascii, embedded_nul
            ) || embedded_nul) {
            return false;
        }
        encodings[static_cast<std::size_t>(i)] = ascii
            ? CETYPE_EXT_ASCII
            : CETYPE_EXT_UTF8;
    }
    return true;
}

CHARR_CXX_HELPER void copy_identity_views(
    const charport::StrViews& input,
    const std::vector<cetype_ext_t>& encodings,
    io::OutputBuilder& output
) {
    const R_xlen_t size = input.size();
    output.reset(size);
    for (R_xlen_t i = 0; i < size; ++i) {
        const cetype_ext_t encoding = encodings[
            static_cast<std::size_t>(i)
        ];
        if (encoding == CETYPE_EXT_NA) {
            output.set_na(i);
            continue;
        }
        const charport::StrView value = input[i];
        output.set(
            i, value.ptr, static_cast<std::size_t>(value.len), encoding
        );
    }
}

CHARR_R_HELPER ByteRecord explicit_record_r(
    SEXP input,
    InputKind kind,
    R_len_t index
) noexcept {
    if (kind == InputKind::null_value)
        return ByteRecord{nullptr, 0, true};

    if (kind == InputKind::raw_vector) {
        return ByteRecord{
            reinterpret_cast<const char*>(RAW(input)),
            LENGTH(input), false
        };
    }

    const SEXP value = VECTOR_ELT(input, index);
    if (Rf_isNull(value))
        return ByteRecord{nullptr, 0, true};
    return ByteRecord{
        reinterpret_cast<const char*>(RAW(value)),
        LENGTH(value), false
    };
}

CHARR_CXX_HELPER ByteRecord reader_record(
    const charport::StrView& value
) {
    if (value.is_na())
        return ByteRecord{nullptr, 0, true};
    if (value.len < 0 || (value.ptr == nullptr && value.len != 0))
        throw std::runtime_error("Reader returned an invalid string view");
    return ByteRecord{value.ptr, value.len, false};
}

CHARR_CXX_HELPER void decode_marked(
    const shared::StringView& input,
    shared::NativeToUtf8& converter,
    icu::UnicodeString& output
) {
    if (input.is_na()) {
        output.setToBogus();
        return;
    }
    if (input.len < 0 || (input.ptr == nullptr && input.len != 0))
        throw std::runtime_error("invalid marked string view");

    const char* data = input.ptr == nullptr ? "" : input.ptr;
    R_len_t length = input.len;
    switch (input.enc) {
    case shared::StringEncoding::ascii:
    case shared::StringEncoding::utf8:
    case shared::StringEncoding::ascii_or_utf8:
        break;
    case shared::StringEncoding::latin1: {
        const shared::ByteView converted = converter.latin1(data, length);
        data = converted.ptr;
        length = converted.len;
        break;
    }
    case shared::StringEncoding::native: {
        const shared::ByteView converted = converter.native(data, length);
        data = converted.ptr;
        length = converted.len;
        break;
    }
    case shared::StringEncoding::bytes:
        throw StriException(MSG__BYTESENC);
    case shared::StringEncoding::missing:
        output.setToBogus();
        return;
    case shared::StringEncoding::unknown:
        throw std::runtime_error("unknown marked string encoding");
    }

    output.setTo(icu::UnicodeString::fromUTF8(
        icu::StringPiece(data == nullptr ? "" : data, length)
    ));
}

CHARR_CXX_HELPER std::size_t transcode_utf16(
    UConverter* target_converter,
    const icu::UnicodeString& input,
    String8buf& output
) {
    const UChar* data = input.getBuffer();
    const R_len_t length = input.length();
    if (data == nullptr)
        throw StriException(MSG__INTERNAL_ERROR);

    std::size_t capacity = UCNV_GET_MAX_BYTES_FOR_STRING(
        static_cast<std::size_t>(length),
        ucnv_getMaxCharSize(target_converter)
    );
    if (capacity > maximum_buffer_length)
        capacity = maximum_buffer_length;
    output.resize(capacity, false);

    UErrorCode status = U_ZERO_ERROR;
    ucnv_resetFromUnicode(target_converter);
    std::size_t required = static_cast<std::size_t>(ucnv_fromUChars(
        target_converter, output.data(), output.size(),
        data, length, &status
    ));
    if (required <= output.size()) {
        STRI__CHECKICUSTATUS_THROW(status, {})
        return required;
    }
    if (required > maximum_buffer_length)
        throw StriException(MSG__BUF_SIZE_EXCEEDED);

    output.resize(required, false);
    status = U_ZERO_ERROR;
    ucnv_resetFromUnicode(target_converter);
    required = static_cast<std::size_t>(ucnv_fromUChars(
        target_converter, output.data(), output.size(),
        data, length, &status
    ));
    STRI__CHECKICUSTATUS_THROW(status, {})
    return required;
}

CHARR_CXX_HELPER std::size_t transcode_direct(
    UConverter* source_converter,
    UConverter* target_converter,
    const char* input,
    R_len_t input_length,
    String8buf& output
) {
    const std::size_t maximum = maximum_buffer_length;
    const std::size_t max_char_size = static_cast<std::size_t>(
        ucnv_getMaxCharSize(target_converter)
    );
    std::size_t capacity = 64;
    if (input_length > 0) {
        const std::size_t input_size = static_cast<std::size_t>(input_length);
        capacity = input_size > (maximum-1)/max_char_size
            ? maximum-1
            : input_size*max_char_size;
        if (capacity < 64)
            capacity = 64;
    }
    output.resize(capacity-1, false);

    const char empty[] = "";
    const char* source = input_length == 0 && input == nullptr
        ? empty
        : input;
    const char* source_limit = source+input_length;
    UChar pivot[1024];
    UChar* pivot_source = pivot;
    UChar* pivot_target = pivot;
    bool reset = true;
    std::size_t used = 0;

    for (;;) {
        char* target = output.data()+used;
        const char* target_limit = output.data()+output.size();
        UErrorCode status = U_ZERO_ERROR;
        ucnv_convertEx(
            target_converter, source_converter,
            &target, target_limit, &source, source_limit,
            pivot, &pivot_source, &pivot_target, pivot+1024,
            reset, true, &status
        );
        used = static_cast<std::size_t>(target-output.data());
        if (status != U_BUFFER_OVERFLOW_ERROR) {
            STRI__CHECKICUSTATUS_THROW(status, {})
            return used;
        }

        const std::size_t old_capacity = output.size();
        if (old_capacity >= maximum)
            throw StriException(MSG__BUF_SIZE_EXCEEDED);
        const std::size_t new_capacity = old_capacity > maximum/2
            ? maximum
            : old_capacity*2;
        output.resize(new_capacity-1, true);
        reset = false;
    }
}

CHARR_NEUTRAL_HELPER cetype_ext_t extended_encoding(
    cetype_t encoding
) noexcept {
    switch (encoding) {
    case CE_UTF8:
        return CETYPE_EXT_UTF8;
    case CE_LATIN1:
        return CETYPE_EXT_LATIN1;
    case CE_BYTES:
        return CETYPE_EXT_BYTES;
    default:
        return CETYPE_EXT_NATIVE;
    }
}

CHARR_CXX_HELPER void reject_embedded_nul(
    const char* data,
    std::size_t length
) {
    if (length > 0 && std::memchr(data, 0, length) != nullptr)
        throw StriException("embedded nul in string");
}

CHARR_CXX_HELPER void store_converted(
    R_len_t index,
    bool raw_output,
    const char* data,
    std::size_t length,
    cetype_ext_t encoding,
    std::vector<RawResult>& raw_results,
    io::OutputBuilder& output
) {
    if (raw_output) {
        RawResult& value = raw_results[static_cast<std::size_t>(index)];
        value.missing = false;
        value.data.assign(
            reinterpret_cast<const unsigned char*>(data),
            reinterpret_cast<const unsigned char*>(data)+length
        );
        return;
    }

    reject_embedded_nul(data, length);
    if (length > maximum_buffer_length)
        throw std::length_error("character output exceeds R's string length limit");
    if (data == nullptr) {
        if (length != 0)
            throw std::invalid_argument("null character output has nonzero length");
        data = "";
    }
    output.set_validated(
        index,
        make_strview(data, static_cast<int>(length), encoding)
    );
}

CHARR_CXX_HELPER void store_parallel_converted(
    R_len_t index,
    bool stage_output,
    const char* data,
    std::size_t length,
    cetype_ext_t encoding,
    std::vector<RawResult>& staged_results,
    io::ParallelOutputBuilder& output,
    unsigned worker
) {
    if (stage_output) {
        if (data == nullptr && length != 0) {
            throw std::invalid_argument(
                "null byte output has nonzero length"
            );
        }
        RawResult& value = staged_results[
            static_cast<std::size_t>(index)
        ];
        value.missing = false;
        if (length == 0) {
            value.data.clear();
        }
        else {
            value.data.assign(
                reinterpret_cast<const unsigned char*>(data),
                reinterpret_cast<const unsigned char*>(data)+length
            );
        }
        return;
    }

    reject_embedded_nul(data, length);
    if (length > maximum_buffer_length) {
        throw std::length_error(
            "character output exceeds R's string length limit"
        );
    }
    if (data == nullptr) {
        if (length != 0) {
            throw std::invalid_argument(
                "null character output has nonzero length"
            );
        }
        data = "";
    }
    output.set_validated(
        worker, index,
        make_strview(data, static_cast<int>(length), encoding)
    );
}

CHARR_CXX_HELPER R_len_t preconvert_native_records(
    std::vector<ByteRecord>& records,
    shared::NativeToUtf8& converter,
    shared::SliceArena& storage,
    std::exception_ptr& pending_error
) {
    for (std::size_t i = 0; i < records.size(); ++i) {
        try {
            ByteRecord& input = records[i];
            if (!input.missing) {
                const shared::ByteView converted = converter.native(
                    input.data, input.length
                );
                const std::size_t length = static_cast<std::size_t>(
                    converted.len
                );
                char* stable = storage.allocate(length);
                if (length > 0)
                    std::memcpy(stable, converted.ptr, length);
                input.data = stable;
                input.length = converted.len;
            }
        }
        catch (...) {
            pending_error = std::current_exception();
            return static_cast<R_len_t>(i);
        }
    }
    return static_cast<R_len_t>(records.size());
}

class IdentityBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER IdentityBody(
        const charport::StrViews& input,
        const std::vector<cetype_ext_t>& encodings,
        io::ParallelOutputBuilder& output
    ) noexcept
        : input_(input), encodings_(encodings), output_(output)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        while (context.next_chunk()) {
            for (R_xlen_t i = context.begin; i < context.end; ++i) {
                const cetype_ext_t encoding = encodings_[
                    static_cast<std::size_t>(i)
                ];
                if (encoding == CETYPE_EXT_NA) {
                    output_.set_na(context.worker, i);
                    continue;
                }
                const charport::StrView value = input_[i];
                output_.set(
                    context.worker, i, value.ptr,
                    static_cast<std::size_t>(value.len), encoding
                );
            }
        }
    }

private:
    const charport::StrViews& input_;
    const std::vector<cetype_ext_t>& encodings_;
    io::ParallelOutputBuilder& output_;
};

/*
 * The warnings one chunk of the task range produced, tagged with the first
 * task of that chunk.
 *
 * A worker no longer holds one ascending run of tasks: it draws chunks from a
 * shared ascending cursor, so a queue per worker records the elements in an
 * order that has nothing to do with the order they appear in the input. A
 * queue per chunk does record it, because a chunk is contiguous and the
 * converter pushes as the loop walks it, so putting the chunks back in task
 * order puts the warnings back in element order.
 *
 * Only a chunk that produced a warning gets a record. An empty one carries
 * nothing: where emission stops is decided by a WarningCutoff, not by which
 * chunks left a record behind.
 */
struct CHARR_OWNER_TYPE ChunkWarnings {
    R_xlen_t begin;
    shared::DeferredWarnings warnings;
};

/*
 * One worker's records, and the cursor the merge reads them with. The worker
 * only ever appends to its own row. `emitted` is touched on the main thread
 * after the join and lives beside the row so that ordering the rows costs the
 * emitting R helper no allocation.
 */
struct CHARR_OWNER_TYPE WorkerChunkWarnings {
    std::size_t emitted;
    std::vector<ChunkWarnings> chunks;
};

/*
 * Where warning emission stops after a failure. `chunk_begin` is the first
 * task of the chunk holding the first failing task, and `prefix` is how many
 * of that chunk's warnings a serial run would have emitted before it reached
 * the same failure.
 */
struct WarningCutoff {
    bool active;
    R_xlen_t chunk_begin;
    std::size_t prefix;
};

CHARR_CXX_HELPER void close_chunk_warnings(
    std::vector<ChunkWarnings>& chunks,
    R_xlen_t begin,
    shared::DeferredWarnings& queue
) {
    if (queue.size() == 0)
        return;
    chunks.emplace_back();
    ChunkWarnings& chunk = chunks.back();
    chunk.begin = begin;
    chunk.warnings = queue;
    // The queue holds one chunk at a time, so each message is copied once,
    // into the record for the chunk that produced it.
    queue = shared::DeferredWarnings();
}

/*
 * The failure a serial run would have reached first. Cutoffs are ordered by
 * the chunk they stop at, because chunk order is task order. A worker that
 * failed before claiming a chunk stops at task 0 with an empty prefix, which
 * is why an equal chunk is settled by the shorter prefix.
 */
CHARR_NEUTRAL_HELPER WarningCutoff first_warning_cutoff(
    const std::vector<WarningCutoff>& worker_cutoffs,
    const WarningCutoff& output_cutoff
) noexcept {
    WarningCutoff first = output_cutoff;
    for (std::size_t i = 0; i < worker_cutoffs.size(); ++i) {
        const WarningCutoff& candidate = worker_cutoffs[i];
        if (!candidate.active)
            continue;
        if (!first.active ||
                candidate.chunk_begin < first.chunk_begin ||
                (candidate.chunk_begin == first.chunk_begin &&
                    candidate.prefix < first.prefix)) {
            first = candidate;
        }
    }
    return first;
}

class ConversionBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER ConversionBody(
        const char* selected_from,
        const char* selected_to,
        bool marked_input,
        bool character_input,
        bool stage_output,
        cetype_ext_t target_mark,
        const charport::StrViews& character_views,
        const std::vector<ByteRecord>& explicit_records,
        const std::vector<icu::UnicodeString>& marked_records,
        std::vector<RawResult>& staged_results,
        io::ParallelOutputBuilder& output,
        std::vector<WorkerChunkWarnings>& warnings,
        std::vector<WarningCutoff>& cutoffs,
        std::vector<std::size_t>& task_prefix,
        std::vector<R_xlen_t>& task_chunk
    ) noexcept
        : selected_from_(selected_from), selected_to_(selected_to),
          marked_input_(marked_input), character_input_(character_input),
          stage_output_(stage_output), target_mark_(target_mark),
          character_views_(character_views),
          explicit_records_(explicit_records),
          marked_records_(marked_records),
          staged_results_(staged_results), output_(output),
          warnings_(warnings), cutoffs_(cutoffs),
          task_prefix_(task_prefix), task_chunk_(task_chunk)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        try {
            run_worker(context);
        }
        catch (...) {
            // Failing with no chunk held -- opening a converter, sizing the
            // buffer -- happens before this worker's first task, and so
            // before every task a serial run would have reached.
            WarningCutoff& cutoff = cutoffs_[
                static_cast<std::size_t>(context.worker)
            ];
            if (!cutoff.active) {
                cutoff.active = true;
                cutoff.chunk_begin = 0;
                cutoff.prefix = 0;
            }
            throw;
        }
    }

private:
    CHARR_CXX_HELPER void run_worker(
        shared::WorkerContext& context
    ) {
        // The queue both converters push into. It holds one chunk at a time:
        // closing a chunk moves what that chunk collected into this worker's
        // row and leaves the queue empty for the next one. Nothing in it
        // outlives the region, so it is a local of run(); it is declared
        // before the converters because it has to outlive them.
        shared::DeferredWarnings queue;
        StriUcnv source_converter(
            selected_from_ == nullptr ? "UTF-8" : selected_from_,
            queue
        );
        StriUcnv target_converter(
            selected_to_ == nullptr ? "UTF-8" : selected_to_,
            queue
        );
        String8buf buffer(0);

        UConverter* source_handle = nullptr;
        if (!marked_input_)
            source_handle = source_converter.getConverter();
        UConverter* target_handle = target_converter.getConverter();

        std::vector<ChunkWarnings>& chunks = warnings_[
            static_cast<std::size_t>(context.worker)
        ].chunks;

        while (context.next_chunk()) {
            const R_xlen_t begin = context.begin;
            try {
                convert_chunk(
                    context, source_handle, target_handle, buffer, queue
                );
            }
            catch (...) {
                // The chunk stopped at its first failing task, so what the
                // queue holds is already the prefix a serial run would have
                // emitted before reaching the same failure.
                record_failure(context.worker, begin, queue.size());
                close_chunk_warnings(chunks, begin, queue);
                throw;
            }
            close_chunk_warnings(chunks, begin, queue);
        }
    }

    // Const, because claiming a chunk belongs to run_worker's loop alone.
    CHARR_CXX_HELPER void convert_chunk(
        const shared::WorkerContext& context,
        UConverter* source_handle,
        UConverter* target_handle,
        String8buf& buffer,
        const shared::DeferredWarnings& queue
    ) {
        for (R_xlen_t task = context.begin;
                task < context.end; ++task) {
            const R_len_t i = static_cast<R_len_t>(task);
            if (marked_input_) {
                const icu::UnicodeString& input = marked_records_[
                    static_cast<std::size_t>(i)
                ];
                if (input.isBogus()) {
                    if (!stage_output_)
                        output_.set_na(context.worker, i);
                    record_task_prefix(context.begin, i, queue);
                    continue;
                }

                const std::size_t converted_size = transcode_utf16(
                    target_handle, input, buffer
                );
                store_parallel_converted(
                    i, stage_output_, buffer.data(), converted_size,
                    target_mark_, staged_results_, output_,
                    context.worker
                );
                record_task_prefix(context.begin, i, queue);
                continue;
            }

            const ByteRecord input = character_input_
                ? reader_record(character_views_[i])
                : explicit_records_[static_cast<std::size_t>(i)];
            if (input.missing) {
                if (!stage_output_)
                    output_.set_na(context.worker, i);
                record_task_prefix(context.begin, i, queue);
                continue;
            }

            const std::size_t converted_size = transcode_direct(
                source_handle, target_handle,
                input.data, input.length, buffer
            );
            store_parallel_converted(
                i, stage_output_, buffer.data(), converted_size,
                target_mark_, staged_results_, output_, context.worker
            );
            record_task_prefix(context.begin, i, queue);
        }
    }

    /*
     * Where a serial run would have stopped had it failed just after this
     * task: the chunk that ran it, and how much that chunk's queue held once
     * the task was finished. Only the staged native path records it, because
     * only that path can still fail on the main thread once the region has
     * joined.
     */
    CHARR_NEUTRAL_HELPER void record_task_prefix(
        R_xlen_t chunk_begin,
        R_len_t index,
        const shared::DeferredWarnings& queue
    ) noexcept {
        if (task_prefix_.empty())
            return;
        const std::size_t task = static_cast<std::size_t>(index);
        task_prefix_[task] = queue.size();
        task_chunk_[task] = chunk_begin;
    }

    CHARR_NEUTRAL_HELPER void record_failure(
        unsigned worker,
        R_xlen_t chunk_begin,
        std::size_t prefix
    ) noexcept {
        WarningCutoff& cutoff = cutoffs_[static_cast<std::size_t>(worker)];
        cutoff.active = true;
        cutoff.chunk_begin = chunk_begin;
        cutoff.prefix = prefix;
    }

    const char* selected_from_;
    const char* selected_to_;
    bool marked_input_;
    bool character_input_;
    bool stage_output_;
    cetype_ext_t target_mark_;
    const charport::StrViews& character_views_;
    const std::vector<ByteRecord>& explicit_records_;
    const std::vector<icu::UnicodeString>& marked_records_;
    std::vector<RawResult>& staged_results_;
    io::ParallelOutputBuilder& output_;
    std::vector<WorkerChunkWarnings>& warnings_;
    std::vector<WarningCutoff>& cutoffs_;
    std::vector<std::size_t>& task_prefix_;
    std::vector<R_xlen_t>& task_chunk_;
};

CHARR_CXX_HELPER void finish_native_output(
    bool raw_output,
    cetype_ext_t target_mark,
    const std::vector<RawResult>& staged_results,
    shared::NativeToUtf8& native_converter,
    std::vector<RawResult>& raw_results,
    io::OutputBuilder& output,
    R_len_t& attempted_index
) {
    const R_len_t size = static_cast<R_len_t>(staged_results.size());
    for (R_len_t i = 0; i < size; ++i) {
        attempted_index = i;
        const RawResult& staged = staged_results[
            static_cast<std::size_t>(i)
        ];
        if (staged.missing) {
            if (!raw_output)
                output.set_na(i);
            continue;
        }
        const char* data = staged.data.empty()
            ? ""
            : reinterpret_cast<const char*>(staged.data.data());
        const shared::ByteView converted = native_converter.utf8_to_native(
            data, static_cast<int>(staged.data.size())
        );
        store_converted(
            i, raw_output, converted.ptr,
            static_cast<std::size_t>(converted.len), target_mark,
            raw_results, output
        );
    }
}

CHARR_CXX_HELPER void transcode_records(
    const shared::ParallelPlan& plan,
    const char* selected_from,
    const char* selected_to,
    bool marked_input,
    bool character_input,
    bool raw_output,
    R_len_t input_size,
    const charport::StrViews& character_views,
    std::vector<ByteRecord>& explicit_records,
    const std::vector<icu::UnicodeString>& marked_records,
    StriUcnv& source_converter,
    StriUcnv& target_converter,
    shared::NativeToUtf8& native_converter,
    String8buf& buffer,
    shared::SliceArena& native_storage,
    std::vector<RawResult>& raw_results,
    std::vector<RawResult>& staged_results,
    io::OutputBuilder& output,
    io::ParallelOutputBuilder& parallel_output,
    std::vector<WorkerChunkWarnings>& worker_chunks,
    std::vector<WarningCutoff>& worker_cutoffs,
    std::vector<std::size_t>& task_warning_prefix,
    std::vector<R_xlen_t>& task_chunk_begin,
    WarningCutoff& output_cutoff,
    std::exception_ptr& native_input_error,
    bool& use_parallel_output
) {
    UConverter* source_handle = nullptr;
    if (!marked_input)
        source_handle = source_converter.getConverter();
    UConverter* target_handle = target_converter.getConverter();
    cetype_ext_t target_mark = extended_encoding(
        raw_output
            ? CE_BYTES
            : (selected_to == nullptr
                ? CE_NATIVE
                : target_converter.getCE())
    );
    if (target_mark == CETYPE_EXT_UTF8)
        target_mark = CETYPE_EXT_ASCII_OR_UTF8;

    bool native_input = false;
    bool native_output = false;
    if ((!marked_input && selected_from == nullptr) ||
            selected_to == nullptr) {
        const bool native_is_utf8 = native_converter.native_is_utf8();
        native_input = !marked_input &&
            selected_from == nullptr && !native_is_utf8;
        native_output = selected_to == nullptr && !native_is_utf8;
    }

    if (plan.workers == 1) {
        if (!raw_output)
            output.reset(input_size);

        for (R_len_t i = 0; i < input_size; ++i) {
            if (marked_input) {
                const icu::UnicodeString& input = marked_records[
                    static_cast<std::size_t>(i)
                ];
                if (input.isBogus()) {
                    if (!raw_output)
                        output.set_na(i);
                    continue;
                }

                const std::size_t converted_size = transcode_utf16(
                    target_handle, input, buffer
                );
                const char* output_data = buffer.data();
                std::size_t output_size = converted_size;
                if (native_output) {
                    const shared::ByteView converted =
                        native_converter.utf8_to_native(
                            output_data,
                            static_cast<int>(output_size)
                        );
                    output_data = converted.ptr;
                    output_size = static_cast<std::size_t>(converted.len);
                }
                store_converted(
                    i, raw_output, output_data, output_size,
                    target_mark, raw_results, output
                );
                continue;
            }

            const ByteRecord input = character_input
                ? reader_record(character_views[i])
                : explicit_records[static_cast<std::size_t>(i)];
            if (input.missing) {
                if (!raw_output)
                    output.set_na(i);
                continue;
            }

            const char* input_data = input.data;
            R_len_t input_length = input.length;
            if (native_input) {
                const shared::ByteView converted = native_converter.native(
                    input_data, input_length
                );
                input_data = converted.ptr;
                input_length = converted.len;
            }

            const std::size_t converted_size = transcode_direct(
                source_handle, target_handle,
                input_data, input_length, buffer
            );
            const char* output_data = buffer.data();
            std::size_t output_size = converted_size;
            if (native_output) {
                const shared::ByteView converted =
                    native_converter.utf8_to_native(
                        output_data,
                        static_cast<int>(output_size)
                    );
                output_data = converted.ptr;
                output_size = static_cast<std::size_t>(converted.len);
            }
            store_converted(
                i, raw_output, output_data, output_size,
                target_mark, raw_results, output
            );
        }
        return;
    }

    R_len_t work_size = input_size;
    if (native_input) {
        if (character_input) {
            explicit_records.resize(static_cast<std::size_t>(input_size));
            for (R_len_t i = 0; i < input_size; ++i) {
                explicit_records[static_cast<std::size_t>(i)] =
                    reader_record(character_views[i]);
            }
            character_input = false;
        }
        work_size = preconvert_native_records(
            explicit_records, native_converter, native_storage,
            native_input_error
        );
    }
    // The native prepass can shorten the range, so the plan is re-derived
    // here. The planner reads only native settings, so a helper that may not
    // touch R can still call it.
    const shared::ParallelPlan work_plan = shared::parallel_plan(
        true, work_size
    );

    const bool stage_output = raw_output || native_output;
    std::vector<RawResult>& worker_results = native_output
        ? staged_results
        : raw_results;
    if (native_output)
        staged_results.resize(static_cast<std::size_t>(work_size));
    if (!raw_output && native_output)
        output.reset(input_size);
    if (!stage_output) {
        parallel_output.reset(work_size, work_plan.workers);
        use_parallel_output = true;
    }

    worker_chunks.resize(work_plan.workers);
    worker_cutoffs.resize(work_plan.workers);
    if (native_output) {
        task_warning_prefix.resize(static_cast<std::size_t>(work_size));
        task_chunk_begin.resize(static_cast<std::size_t>(work_size));
    }
    ConversionBody body(
        selected_from, selected_to, marked_input, character_input,
        stage_output, target_mark, character_views, explicit_records,
        marked_records, worker_results, parallel_output,
        worker_chunks, worker_cutoffs,
        task_warning_prefix, task_chunk_begin
    );
    shared::run_parallel(work_plan, work_size, body);

    if (native_input_error && !native_output)
        std::rethrow_exception(native_input_error);

    if (native_output) {
        R_len_t attempted_index = -1;
        try {
            finish_native_output(
                raw_output, target_mark, staged_results, native_converter,
                raw_results, output, attempted_index
            );
        }
        catch (...) {
            if (attempted_index >= 0) {
                const std::size_t task = static_cast<std::size_t>(
                    attempted_index
                );
                // This task's own conversion succeeded on the worker and its
                // warnings belong to the serial prefix; converting its result
                // to the native encoding, here, did not. Stop at the chunk
                // that ran it, where its queue stood when the task finished.
                output_cutoff.active = true;
                output_cutoff.chunk_begin = task_chunk_begin[task];
                output_cutoff.prefix = task_warning_prefix[task];
            }
            throw;
        }
    }

    if (native_input_error)
        std::rethrow_exception(native_input_error);
}

/*
 * Warnings staged natively during the operation, emitted in the order of the
 * elements that produced them.
 *
 * Everything the main thread converted is in one queue and comes first. The
 * parallel region's queues are keyed by chunk, and each worker's row is
 * already ascending, because a worker claims chunks in the order the cursor
 * hands them out; recovering task order is therefore a merge of ordered rows
 * and never a sort. The merge cursor lives in the row, so this helper owns
 * nothing and cannot throw.
 *
 * A failure stops the walk. Chunks below the cutoff emit in full, the chunk
 * holding the first failing task emits only the prefix a serial run would
 * have reached, and chunks above it emit nothing.
 */
CHARR_R_HELPER void emit_conversion_warnings_r(
    const shared::DeferredWarnings& warnings,
    std::vector<WorkerChunkWarnings>& worker_chunks,
    const std::vector<WarningCutoff>& worker_cutoffs,
    const WarningCutoff& output_cutoff
) noexcept {
    warnings.emit_r();

    const WarningCutoff cutoff = first_warning_cutoff(
        worker_cutoffs, output_cutoff
    );
    const std::size_t rows = worker_chunks.size();
    for (;;) {
        std::size_t next = rows;
        R_xlen_t lowest = 0;
        // An R helper may not hold a cleanup-bearing local: an R error here
        // longjmps past every destructor. So the merge walks by index and
        // binds nothing that owns anything.
        for (std::size_t row = 0; row < rows; ++row) {
            if (worker_chunks[row].emitted >=
                    worker_chunks[row].chunks.size()) {
                continue;
            }
            const R_xlen_t begin = worker_chunks[row].chunks[
                worker_chunks[row].emitted
            ].begin;
            if (next == rows || begin < lowest) {
                next = row;
                lowest = begin;
            }
        }
        if (next == rows)
            return;

        const std::size_t at = worker_chunks[next].emitted;
        const R_xlen_t begin = worker_chunks[next].chunks[at].begin;
        ++worker_chunks[next].emitted;
        if (cutoff.active && begin >= cutoff.chunk_begin) {
            // The cutoff's own chunk may have left no record, so stopping is
            // keyed on reaching it rather than on matching it.
            if (begin == cutoff.chunk_begin) {
                worker_chunks[next].chunks[at].warnings.emit_prefix_r(
                    cutoff.prefix
                );
            }
            return;
        }
        worker_chunks[next].chunks[at].warnings.emit_r();
    }
}

CHARR_CXX_HELPER void release_conversion_state(
    charport::Reader& reader,
    StriUcnv& source,
    StriUcnv& target,
    shared::NativeToUtf8& native
) {
    reader = charport::Reader();
    source = StriUcnv();
    target = StriUcnv();
    native.reset();
}

CHARR_R_HELPER SEXP assemble_raw_output_r(
    const std::vector<RawResult>& values,
    shared::ProtHelper& protections
) noexcept {
    const R_xlen_t size = static_cast<R_xlen_t>(values.size());
    SEXP output = protections.protect_one(Rf_allocVector(VECSXP, size));
    for (R_xlen_t i = 0; i < size; ++i) {
        const std::size_t index = static_cast<std::size_t>(i);
        if (values[index].missing)
            continue;

        SEXP raw = protections.protect_one(Rf_allocVector(
            RAWSXP, static_cast<R_xlen_t>(values[index].data.size())
        ));
        if (!values[index].data.empty()) {
            std::memcpy(
                RAW(raw), values[index].data.data(),
                values[index].data.size()
            );
        }
        SET_VECTOR_ELT(output, i, raw);
        protections.release(1);
    }
    return output;
}

} // namespace encoding_conversion

using namespace encoding_conversion;

CHARR_ENTRYPOINT SEXP ci_encode_string(
    SEXP str,
    SEXP from,
    SEXP to,
    SEXP to_raw
) noexcept {
    CHARR_ENTRYPOINT_BEGIN();

    const char* selected_from = ci__prepare_arg_enc_r(
        from, "from", true
    );
    const bool marked_input = selected_from == nullptr &&
        Rf_isVectorAtomic(str) && !isRaw(str);

    PROTECT_INDEX str_index;
    entry_protections.protect_with_index(str, &str_index);

    const char* selected_to = nullptr;
    bool raw_output = false;
    if (marked_input) {
        SEXP prepared = ci__prepare_arg_string_r(str, "str");
        str = entry_protections.reprotect_one(prepared, str_index);
        selected_to = ci__prepare_arg_enc_r(to, "to", true);
        raw_output = ci__prepare_arg_logical_1_notNA_r(
            to_raw, "to_raw"
        );
    }
    else {
        selected_to = ci__prepare_arg_enc_r(to, "to", true);
        raw_output = ci__prepare_arg_logical_1_notNA_r(
            to_raw, "to_raw"
        );
        SEXP prepared = ci__prepare_arg_list_raw_r(str, "str");
        str = entry_protections.reprotect_one(prepared, str_index);
    }

    InputKind input_kind = InputKind::character;
    R_len_t input_size = LENGTH(str);
    if (!marked_input) {
        if (Rf_isNull(str)) {
            input_kind = InputKind::null_value;
            input_size = 1;
        }
        else if (isRaw(str)) {
            input_kind = InputKind::raw_vector;
            input_size = 1;
        }
        else if (Rf_isVectorList(str)) {
            input_kind = InputKind::raw_list;
        }
    }
    const bool character_input = input_kind == InputKind::character;
    const bool identity_candidate = !raw_output && character_input &&
        utf8_encoding_name(selected_to) &&
        (marked_input || utf8_encoding_name(selected_from));
    const shared::ParallelPlan plan = shared::parallel_plan(true, input_size);

    try {
        shared::DeferredWarnings warnings;
        StriUcnv source_converter(
            selected_from == nullptr ? "UTF-8" : selected_from,
            warnings
        );
        StriUcnv target_converter(
            selected_to == nullptr ? "UTF-8" : selected_to,
            warnings
        );
        shared::NativeToUtf8 native_converter;
        String8buf buffer(0);
        charport::Reader reader;
        charport::StrViews character_views(
            character_input ? input_size : 0
        );
        std::vector<cetype_ext_t> identity_encodings(
            identity_candidate ? static_cast<std::size_t>(input_size) : 0U
        );
        std::vector<ByteRecord> explicit_records(
            !marked_input && !character_input
                ? static_cast<std::size_t>(input_size)
                : 0U
        );
        std::vector<icu::UnicodeString> marked_records(
            marked_input ? static_cast<std::size_t>(input_size) : 0U
        );
        std::vector<RawResult> raw_results(
            raw_output ? static_cast<std::size_t>(input_size) : 0U
        );
        std::vector<RawResult> staged_results;
        io::OutputBuilder output(0);
        io::ParallelOutputBuilder parallel_output;
        shared::SliceArena native_storage;
        std::vector<WorkerChunkWarnings> worker_chunks;
        std::vector<WarningCutoff> worker_cutoffs;
        std::vector<std::size_t> task_warning_prefix;
        std::vector<R_xlen_t> task_chunk_begin;
        WarningCutoff output_cutoff = {false, 0, 0};
        std::exception_ptr native_input_error;
        std::exception_ptr pending_error;
        bool use_parallel_output = false;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                try {
                    if (character_input) {
                        reader.reset(str);
                        if (reader.size() != input_size) {
                            throw std::runtime_error(
                                "Reader length changed during encoding conversion"
                            );
                        }
                        if (input_size > 0) {
                            reader.views(
                                0, input_size,
                                character_views.ptrs(),
                                character_views.lengths(),
                                character_views.encodings()
                            );
                        }
                    }

                    if (identity_candidate &&
                            validate_identity_views(
                            character_views, marked_input,
                            identity_encodings
                            )) {
                        if (plan.workers > 1) {
                            parallel_output.reset(
                                input_size, plan.workers
                            );
                            use_parallel_output = true;
                            IdentityBody body(
                                character_views, identity_encodings,
                                parallel_output
                            );
                            shared::run_parallel(
                                plan, input_size, body
                            );
                        }
                        else {
                            copy_identity_views(
                                character_views, identity_encodings, output
                            );
                        }
                    }
                    else if (input_size <= 0) {
                        if (!raw_output)
                            output.reset(0);
                    }
                    else {
                        if (marked_input) {
                            for (R_len_t i = 0; i < input_size; ++i) {
                                decode_marked(
                                    io::as_shared_view(character_views[i]),
                                    native_converter,
                                    marked_records[
                                        static_cast<std::size_t>(i)
                                    ]
                                );
                            }
                        }
                        else if (!character_input) {
                            for (R_len_t i = 0; i < input_size; ++i) {
                                explicit_records[
                                    static_cast<std::size_t>(i)
                                ] = explicit_record_r(str, input_kind, i);
                            }
                        }
                        transcode_records(
                            plan, selected_from, selected_to,
                            marked_input, character_input, raw_output,
                            input_size, character_views, explicit_records,
                            marked_records, source_converter,
                            target_converter, native_converter, buffer,
                            native_storage, raw_results, staged_results,
                            output, parallel_output, worker_chunks,
                            worker_cutoffs, task_warning_prefix,
                            task_chunk_begin, output_cutoff,
                            native_input_error, use_parallel_output
                        );
                    }
                }
                catch (...) {
                    pending_error = std::current_exception();
                }

                release_conversion_state(
                    reader, source_converter,
                    target_converter, native_converter
                );
                emit_conversion_warnings_r(
                    warnings, worker_chunks, worker_cutoffs, output_cutoff
                );
                if (pending_error)
                    std::rethrow_exception(pending_error);

                if (raw_output) {
                    result = entry_protections.reprotect_one(
                        assemble_raw_output_r(
                            raw_results, callback_protections
                        ),
                        result_index
                    );
                }
                else {
                    result = entry_protections.reprotect_one(
                        use_parallel_output
                            ? parallel_output.to_sexp()
                            : output.to_sexp(),
                        result_index
                    );
                }
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}

CHARR_ENTRYPOINT SEXP ci_encode_raw(
    SEXP str,
    SEXP from,
    SEXP to,
    SEXP to_raw
) noexcept {
    CHARR_ENTRYPOINT_BEGIN();

    const char* selected_from = ci__prepare_arg_enc_r(
        from, "from", true
    );
    const bool marked_input = selected_from == nullptr &&
        Rf_isVectorAtomic(str) && !isRaw(str);

    PROTECT_INDEX str_index;
    entry_protections.protect_with_index(str, &str_index);

    const char* selected_to = nullptr;
    bool raw_output = false;
    if (marked_input) {
        SEXP prepared = ci__prepare_arg_string_r(str, "str");
        str = entry_protections.reprotect_one(prepared, str_index);
        selected_to = ci__prepare_arg_enc_r(to, "to", true);
        raw_output = ci__prepare_arg_logical_1_notNA_r(
            to_raw, "to_raw"
        );
    }
    else {
        selected_to = ci__prepare_arg_enc_r(to, "to", true);
        raw_output = ci__prepare_arg_logical_1_notNA_r(
            to_raw, "to_raw"
        );
        SEXP prepared = ci__prepare_arg_list_raw_r(str, "str");
        str = entry_protections.reprotect_one(prepared, str_index);
    }

    InputKind input_kind = InputKind::character;
    R_len_t input_size = LENGTH(str);
    if (!marked_input) {
        if (Rf_isNull(str)) {
            input_kind = InputKind::null_value;
            input_size = 1;
        }
        else if (isRaw(str)) {
            input_kind = InputKind::raw_vector;
            input_size = 1;
        }
        else if (Rf_isVectorList(str)) {
            input_kind = InputKind::raw_list;
        }
    }
    const bool character_input = input_kind == InputKind::character;
    const bool identity_candidate = !raw_output && character_input &&
        utf8_encoding_name(selected_to) &&
        (marked_input || utf8_encoding_name(selected_from));
    const shared::ParallelPlan plan = shared::parallel_plan(true, input_size);

    try {
        shared::DeferredWarnings warnings;
        StriUcnv source_converter(
            selected_from == nullptr ? "UTF-8" : selected_from,
            warnings
        );
        StriUcnv target_converter(
            selected_to == nullptr ? "UTF-8" : selected_to,
            warnings
        );
        shared::NativeToUtf8 native_converter;
        String8buf buffer(0);
        charport::Reader reader;
        charport::StrViews character_views(
            character_input ? input_size : 0
        );
        std::vector<cetype_ext_t> identity_encodings(
            identity_candidate ? static_cast<std::size_t>(input_size) : 0U
        );
        std::vector<ByteRecord> explicit_records(
            !marked_input && !character_input
                ? static_cast<std::size_t>(input_size)
                : 0U
        );
        std::vector<icu::UnicodeString> marked_records(
            marked_input ? static_cast<std::size_t>(input_size) : 0U
        );
        std::vector<RawResult> raw_results(
            raw_output ? static_cast<std::size_t>(input_size) : 0U
        );
        std::vector<RawResult> staged_results;
        io::OutputBuilder output(0);
        io::ParallelOutputBuilder parallel_output;
        shared::SliceArena native_storage;
        std::vector<WorkerChunkWarnings> worker_chunks;
        std::vector<WarningCutoff> worker_cutoffs;
        std::vector<std::size_t> task_warning_prefix;
        std::vector<R_xlen_t> task_chunk_begin;
        WarningCutoff output_cutoff = {false, 0, 0};
        std::exception_ptr native_input_error;
        std::exception_ptr pending_error;
        bool use_parallel_output = false;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                try {
                    if (character_input) {
                        reader.reset(str);
                        if (reader.size() != input_size) {
                            throw std::runtime_error(
                                "Reader length changed during encoding conversion"
                            );
                        }
                        if (input_size > 0) {
                            reader.views(
                                0, input_size,
                                character_views.ptrs(),
                                character_views.lengths(),
                                character_views.encodings()
                            );
                        }
                    }

                    if (identity_candidate &&
                            validate_identity_views(
                                character_views, marked_input,
                                identity_encodings
                            )) {
                        if (plan.workers > 1) {
                            parallel_output.reset(
                                input_size, plan.workers
                            );
                            use_parallel_output = true;
                            IdentityBody body(
                                character_views, identity_encodings,
                                parallel_output
                            );
                            shared::run_parallel(
                                plan, input_size, body
                            );
                        }
                        else {
                            copy_identity_views(
                                character_views, identity_encodings, output
                            );
                        }
                    }
                    else if (input_size <= 0) {
                        if (!raw_output)
                            output.reset(0);
                    }
                    else {
                        if (marked_input) {
                            for (R_len_t i = 0; i < input_size; ++i) {
                                decode_marked(
                                    io::as_shared_view(character_views[i]),
                                    native_converter,
                                    marked_records[
                                        static_cast<std::size_t>(i)
                                    ]
                                );
                            }
                        }
                        else if (!character_input) {
                            for (R_len_t i = 0; i < input_size; ++i) {
                                explicit_records[
                                    static_cast<std::size_t>(i)
                                ] = explicit_record_r(str, input_kind, i);
                            }
                        }
                        transcode_records(
                            plan, selected_from, selected_to,
                            marked_input, character_input, raw_output,
                            input_size, character_views, explicit_records,
                            marked_records, source_converter,
                            target_converter, native_converter, buffer,
                            native_storage, raw_results, staged_results,
                            output, parallel_output, worker_chunks,
                            worker_cutoffs, task_warning_prefix,
                            task_chunk_begin, output_cutoff,
                            native_input_error, use_parallel_output
                        );
                    }
                }
                catch (...) {
                    pending_error = std::current_exception();
                }

                release_conversion_state(
                    reader, source_converter,
                    target_converter, native_converter
                );
                emit_conversion_warnings_r(
                    warnings, worker_chunks, worker_cutoffs, output_cutoff
                );
                if (pending_error)
                    std::rethrow_exception(pending_error);

                if (raw_output) {
                    result = entry_protections.reprotect_one(
                        assemble_raw_output_r(
                            raw_results, callback_protections
                        ),
                        result_index
                    );
                }
                else {
                    result = entry_protections.reprotect_one(
                        use_parallel_output
                            ? parallel_output.to_sexp()
                            : output.to_sexp(),
                        result_index
                    );
                }
                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}

} } // namespace charr::altrep_backend
