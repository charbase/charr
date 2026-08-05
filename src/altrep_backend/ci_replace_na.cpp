
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
#include "../shared/entrypoint.h"
#include "../shared/native_to_utf8.h"
#include "../shared/protect.h"
#include "../shared/slice_arena.h"
#include "../shared/unwind.h"
#include "../shared/utf8.h"
#include "altrep_backend/io/string_view.h"
#include "altrep_backend/io/utf8_output.h"

#include <charport.h>

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

namespace charr { namespace altrep_backend {


namespace replace_na {

CHARR_NEUTRAL_HELPER bool has_utf8_bom(
    const char* ptr, int len
) noexcept
{
    return len >= 3 &&
        static_cast<unsigned char>(ptr[0]) == 0xef &&
        static_cast<unsigned char>(ptr[1]) == 0xbb &&
        static_cast<unsigned char>(ptr[2]) == 0xbf;
}


CHARR_NEUTRAL_HELPER bool is_direct_source(
    const charport::StrView& source
) noexcept
{
    if (source.is_na() || source.enc == CETYPE_EXT_ASCII)
        return true;
    return source.enc == CETYPE_EXT_UTF8 &&
        !has_utf8_bom(source.ptr, source.len);
}


CHARR_NEUTRAL_HELPER shared::StringView resolve_output_encoding(
    shared::StringView value
) noexcept
{
    if (!value.is_na() &&
            value.enc == shared::StringEncoding::ascii_or_utf8) {
        value.enc = io::is_ascii(
            value.ptr, static_cast<std::size_t>(value.len)
        ) ? shared::StringEncoding::ascii : shared::StringEncoding::utf8;
    }
    return value;
}


/*
 * One finished chunk. A worker draws several chunks and they are not adjacent
 * to each other, so a part carries the task index it starts at and the main
 * thread joins the parts in task order rather than in worker order.
 */
struct ChunkOutput {
    R_xlen_t begin;
    io::OutputStore store;
};


/*
 * Order the parts by task index. Every worker's own parts are already
 * ascending, because a worker claims chunks in the order the cursor hands
 * them out, so the merge only has to take the lowest remaining head. The
 * parts are moved out, which leaves `parts` holding empty stores.
 */
CHARR_CXX_HELPER void order_chunk_outputs(
    std::vector<std::vector<ChunkOutput> >& parts,
    std::vector<io::OutputStore>& ordered
)
{
    const std::size_t workers = parts.size();
    std::vector<std::size_t> taken(workers, 0);
    std::size_t total = 0;
    for (std::size_t worker = 0; worker < workers; ++worker)
        total += parts[worker].size();

    ordered.clear();
    ordered.reserve(total);
    for (std::size_t done = 0; done < total; ++done) {
        std::size_t next = workers;
        for (std::size_t worker = 0; worker < workers; ++worker) {
            if (taken[worker] >= parts[worker].size())
                continue;
            if (next == workers ||
                    parts[worker][taken[worker]].begin <
                        parts[next][taken[next]].begin) {
                next = worker;
            }
        }
        ordered.push_back(std::move(parts[next][taken[next]].store));
        ++taken[next];
    }
}


/*
 * Each worker fills a Store of its own chunk and the main thread joins them,
 * rather than sharing one io::ParallelOutputBuilder. The sharded builder
 * keeps each shard's payload cursor in a vector of 24-byte shards, so two or
 * three shards share a cache line, and every record read-modify-writes its
 * shard's cursor. This kernel is one record copy, the cheapest per-record
 * work in the package, so that contended line is the whole cost. Driving
 * charport's own headers over 1,000,000 one-byte records, the packed
 * spacing costs 4.2 ms at one worker and 8.3 ms at four, while the same
 * loop with nothing changed but the shards spaced 64 bytes apart costs
 * 4.2 ms and 1.6 ms. A worker-local Builder gets that isolation without
 * reaching into charport.
 */
template<bool Direct>
class Body final : public ParallelBody {
public:
    CHARR_CXX_HELPER Body(
        const charport::StrViews& source_views,
        const std::vector<shared::StringView>& source_inputs,
        const charport::StrView& replacement,
        std::vector<std::vector<ChunkOutput> >& outputs
    ) noexcept
        : source_views_(source_views), source_inputs_(source_inputs),
          replacement_(replacement), outputs_(outputs)
    {
    }

    CHARR_CXX_HELPER void run(
        shared::WorkerContext& context
    ) override
    {
        // Only this worker appends to its own list, so the parts it hands
        // back need no synchronisation.
        std::vector<ChunkOutput>& parts = outputs_[
            static_cast<std::size_t>(context.worker)
        ];
        while (context.next_chunk()) {
            // The builder and the payload slices it allocates must not be
            // shared between workers, so they are locals of run(). One
            // builder covers one chunk because its size is the chunk's and
            // its Store is released at the end of it; only that finished
            // Store outlives the chunk.
            charport::charvec::Builder builder(context.end - context.begin);
            for (R_xlen_t i = context.begin; i < context.end; ++i) {
                const R_xlen_t output_index = i - context.begin;
                if constexpr (Direct) {
                    const charport::StrView source = source_views_[i];
                    builder.set(
                        output_index,
                        source.is_na() ? replacement_ : source
                    );
                }
                else {
                    const shared::StringView& source = source_inputs_[
                        static_cast<std::size_t>(i)
                    ];
                    builder.set(
                        output_index,
                        source.is_na()
                            ? replacement_
                            : io::as_charport_view(source)
                    );
                }
            }
            parts.push_back(
                ChunkOutput{context.begin, builder.release_store()}
            );
        }
    }

private:
    const charport::StrViews& source_views_;
    const std::vector<shared::StringView>& source_inputs_;
    charport::StrView replacement_;
    std::vector<std::vector<ChunkOutput> >& outputs_;
};

} // namespace replace_na

using namespace replace_na;


/**
* Replace NAs with a given string
*
*
* @param str character vector
* @param replacement single string
* @return character vector
*
* @version 0.2-1 (Bartek Tartanus, 2014-03-15)
*
* @version 0.3-1 (Marek Gagolewski, 2014-11-05)
*    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
*/
CHARR_ENTRYPOINT SEXP ci_replace_na(
    SEXP str, SEXP replacement
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();


    str = entry_protections.protect_one(
        ci__prepare_arg_string_r(str, "str")
    );
    replacement = entry_protections.protect_one(
        ci__prepare_arg_string_1_r(
            replacement, "replacement"
        )
    );
    try {
        const bool source_is_result_shaped = NO_ATTRIB(str) != 0;
        charport::Reader source_reader;
        charport::Reader replacement_reader;
        charport::StrViews source_views;
        charport::StrViews replacement_views;
        shared::NativeToUtf8 source_converter;
        shared::NativeToUtf8 replacement_converter;
        shared::SliceArena source_storage;
        shared::SliceArena replacement_storage;
        std::vector<shared::StringView> source_inputs;
        charport::charvec::Builder builder(0);
        std::vector<std::vector<ChunkOutput> > parallel_outputs;
        std::vector<io::OutputStore> ordered_outputs;
        io::OutputStore output;
        charport::StrView replacement_input{
            nullptr, NA_INTEGER, CETYPE_EXT_NA
        };

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const R_len_t source_length = io::checked_r_len(
                    XLENGTH(str), "character vectors"
                );
                const shared::ParallelPlan plan = shared::parallel_plan(
                    true, source_length
                );
                source_reader.reset(str);
                if (source_reader.size() != source_length) {
                    throw std::runtime_error(
                        "Reader length changed during NA replacement"
                    );
                }
                source_views.resize(source_length);
                if (source_length > 0) {
                    source_reader.views(
                        0, source_length,
                        source_views.ptrs(), source_views.lengths(),
                        source_views.encodings()
                    );
                }

                bool direct = true;
                bool has_na = false;
                for (R_len_t i = 0; i < source_length; ++i) {
                    const charport::StrView source = source_views[i];
                    if (source.is_na()) {
                        has_na = true;
                        continue;
                    }
                    if (source.enc == CETYPE_EXT_BYTES) {
                        shared::normalize_utf8(
                            io::as_shared_view(source),
                            source_converter, source_storage
                        );
                    }
                    if (!is_direct_source(source)) {
                        direct = false;
                        break;
                    }
                }

                if (!direct) {
                    source_inputs.resize(
                        static_cast<std::size_t>(source_length)
                    );
                    for (R_len_t i = 0; i < source_length; ++i) {
                        source_inputs[static_cast<std::size_t>(i)] =
                            resolve_output_encoding(shared::normalize_utf8(
                                io::as_shared_view(source_views[i]),
                                source_converter, source_storage
                            ));
                    }
                }

                replacement_reader.reset(replacement);
                if (replacement_reader.size() != 1) {
                    throw std::runtime_error(
                        "replacement Reader length changed"
                    );
                }
                replacement_views.resize(1);
                replacement_reader.views(
                    0, 1,
                    replacement_views.ptrs(), replacement_views.lengths(),
                    replacement_views.encodings()
                );
                replacement_input = io::as_charport_view(
                    resolve_output_encoding(shared::normalize_utf8(
                        io::as_shared_view(replacement_views[0]),
                        replacement_converter, replacement_storage
                    ))
                );

                // Nothing to replace and nothing to reshape, so the result
                // is the input. This stays independent of the worker count:
                // asking for threads must not turn an O(1) answer into an
                // O(n) copy.
                if (direct && !has_na && source_is_result_shaped) {
                    result = entry_protections.reprotect_one(str, result_index);
                }
                else if (plan.workers > 1) {
                    parallel_outputs.clear();
                    parallel_outputs.resize(plan.workers);
                    if (direct) {
                        Body<true> body(
                            source_views, source_inputs,
                            replacement_input, parallel_outputs
                        );
                        shared::run_parallel(plan, source_length, body);
                    }
                    else {
                        Body<false> body(
                            source_views, source_inputs,
                            replacement_input, parallel_outputs
                        );
                        shared::run_parallel(plan, source_length, body);
                    }
                    // Chunks are contiguous and cover the range exactly, but
                    // a worker draws several of them and they interleave
                    // with the other workers', so the parts are joined in
                    // task order rather than in worker order. That
                    // reproduces the serial record sequence. The joined
                    // Store takes every payload slice, so the record
                    // pointers copied out of the parts stay valid.
                    order_chunk_outputs(parallel_outputs, ordered_outputs);
                    output = io::concat_stores(ordered_outputs);
                    result = entry_protections.reprotect_one(
                        io::finalize(std::move(output)), result_index
                    );
                }
                else {
                    builder.reset(source_length);
                    // This is charport's Builder, not io::OutputBuilder, so
                    // set() copies the record as the loops above resolved it
                    // and never re-runs output_record(). The worker body
                    // builds through the same call on its own Builder, so
                    // the two loops stay the same kernel.
                    for (R_len_t i = 0; i < source_length; ++i) {
                        if (direct) {
                            const charport::StrView source = source_views[i];
                            builder.set(
                                i,
                                source.is_na() ? replacement_input : source
                            );
                        }
                        else {
                            const shared::StringView& source = source_inputs[
                                static_cast<std::size_t>(i)
                            ];
                            builder.set(
                                i,
                                source.is_na()
                                    ? replacement_input
                                    : io::as_charport_view(source)
                            );
                        }
                    }
                    result = entry_protections.reprotect_one(
                        builder.to_sexp(), result_index
                    );
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}


} } // namespace charr::altrep_backend
