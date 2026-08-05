#ifndef CHARR_ALTREP_UTF8_OUTPUT_H
#define CHARR_ALTREP_UTF8_OUTPUT_H

#include "../../shared/lint.h"

#include <charport.h>

#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

namespace charr {
namespace altrep_backend {
namespace io {

using OutputRecord = charport::StrView;
using OutputStore = charport::charvec::Store;

CHARR_NEUTRAL_HELPER [[nodiscard]] OutputRecord
missing_output_record() noexcept;
CHARR_CXX_HELPER [[nodiscard]] OutputRecord output_record(
    const char* data, std::size_t length, cetype_ext_t encoding
);
CHARR_CXX_HELPER [[nodiscard]] OutputRecord output_record(
    std::string_view value, cetype_ext_t encoding
);
CHARR_CXX_HELPER [[nodiscard]] OutputRecord output_record(
    const charport::StrView& value
);

CHARR_CXX_HELPER [[nodiscard]] OutputStore scalar_store(
    const OutputRecord& value
);
CHARR_CXX_HELPER [[nodiscard]] OutputStore scalar_store(
    const char* data, std::size_t length, cetype_ext_t encoding
);
CHARR_CXX_HELPER [[nodiscard]] OutputStore scalar_store(
    std::string_view value, cetype_ext_t encoding
);

// Join staged stores without copying their payload bytes. Record pointers
// remain valid because the result takes ownership of every payload slice.
// The stores are joined in the order given, so the caller owns the ordering.
CHARR_CXX_HELPER [[nodiscard]] OutputStore concat_stores(
    std::vector<OutputStore>& stores
);

/*
 * Staging for an operation whose output cardinality is not known per element,
 * so its records cannot be written at a fixed index and have to be appended.
 *
 * A worker used to hold one contiguous ascending slice, which made "join the
 * shards in worker order" the same thing as "join them in task order". Workers
 * now draw chunks from a shared cursor, so worker order says nothing about
 * task order and joining by it silently interleaves the result. Each store is
 * therefore keyed by the first task of the chunk that produced it, and the
 * join sorts on that key.
 *
 * A worker only ever appends to its own row, so no worker sees another's, and
 * every row is already ascending because a worker's own claims ascend in time.
 * The join is a merge of sorted rows, not a sort.
 */
class CHARR_OWNER_TYPE ChunkStores {
public:
    CHARR_CXX_HELPER ChunkStores();

    ChunkStores(const ChunkStores&) = delete;
    ChunkStores& operator=(const ChunkStores&) = delete;
    ChunkStores(ChunkStores&&) = delete;
    ChunkStores& operator=(ChunkStores&&) = delete;

    // Called on the main thread before the parallel region.
    CHARR_CXX_HELPER void reset(unsigned workers);

    // Called by a worker, once per chunk it claims, before it appends any
    // record for that chunk. `begin` is the chunk's first task.
    CHARR_CXX_HELPER void open(unsigned worker, R_xlen_t begin);

    // The store the given worker is currently filling.
    CHARR_CXX_HELPER OutputStore& current(unsigned worker);

    // Called on the main thread after the join.
    CHARR_CXX_HELPER [[nodiscard]] OutputStore concatenate();

private:
    struct Entry {
        R_xlen_t begin;
        OutputStore store;
    };
    std::vector<std::vector<Entry> > rows_;
};

CHARR_R_HELPER inline SEXP finalize(OutputStore&& store) noexcept
{
    return charport::charvec::wrap(std::move(store));
}

class CHARR_OWNER_TYPE OutputBuilder {
public:
    CHARR_CXX_HELPER explicit OutputBuilder(R_xlen_t size);

    OutputBuilder(const OutputBuilder&) = delete;
    OutputBuilder& operator=(const OutputBuilder&) = delete;
    OutputBuilder(OutputBuilder&&) = delete;
    OutputBuilder& operator=(OutputBuilder&&) = delete;

    CHARR_CXX_HELPER void reset(R_xlen_t size)
    {
        builder_.reset(size);
        size_ = size;
    }
    CHARR_NEUTRAL_HELPER R_xlen_t size() const noexcept;

    CHARR_CXX_HELPER void set(
        R_xlen_t index, const OutputRecord& value
    );
    CHARR_CXX_HELPER void set(
        R_xlen_t index, const char* data, std::size_t length,
        cetype_ext_t encoding
    );
    CHARR_CXX_HELPER void set(
        R_xlen_t index, std::string_view value, cetype_ext_t encoding
    );
    // Hot paths may use this after validating the pointer, length, and
    // encoding while constructing the record. CETYPE_EXT_ASCII_OR_UTF8 is
    // valid: charport resolves that mark when R materializes the CHARSXP.
    CHARR_CXX_HELPER void set_validated(
        R_xlen_t index, const OutputRecord& value
    )
    {
        if (value.is_na()) {
            builder_.set_na(index);
            return;
        }
        builder_.set(index, value);
    }
    CHARR_CXX_HELPER void set_na(R_xlen_t index)
    {
        builder_.set_na(index);
    }
    CHARR_CXX_HELPER [[nodiscard]] char* reserve(
        R_xlen_t index, std::size_t length, cetype_ext_t encoding
    );

    CHARR_CXX_HELPER [[nodiscard]] OutputStore release_store() noexcept
    {
        OutputStore store = builder_.release_store();
        size_ = 0;
        return store;
    }
    CHARR_R_HELPER SEXP to_sexp() noexcept;

private:
    R_xlen_t size_;
    charport::charvec::Builder builder_;
};

class CHARR_OWNER_TYPE ParallelOutputBuilder {
public:
    CHARR_CXX_HELPER ParallelOutputBuilder();

    ParallelOutputBuilder(const ParallelOutputBuilder&) = delete;
    ParallelOutputBuilder& operator=(const ParallelOutputBuilder&) = delete;
    ParallelOutputBuilder(ParallelOutputBuilder&&) = delete;
    ParallelOutputBuilder& operator=(ParallelOutputBuilder&&) = delete;

    CHARR_CXX_HELPER void reset(R_xlen_t size, unsigned workers)
    {
        builder_.reset(size, static_cast<std::size_t>(workers));
        size_ = size;
    }
    CHARR_NEUTRAL_HELPER R_xlen_t size() const noexcept;

    CHARR_CXX_HELPER void set(
        unsigned worker, R_xlen_t index, const OutputRecord& value
    );
    CHARR_CXX_HELPER void set(
        unsigned worker, R_xlen_t index, const char* data,
        std::size_t length, cetype_ext_t encoding
    );
    CHARR_CXX_HELPER void set(
        unsigned worker, R_xlen_t index, std::string_view value,
        cetype_ext_t encoding
    );
    CHARR_CXX_HELPER void set_validated(
        unsigned worker, R_xlen_t index, const OutputRecord& value
    )
    {
        if (value.is_na()) {
            builder_.set_na(worker, index);
            return;
        }
        builder_.set(worker, index, value);
    }
    CHARR_CXX_HELPER void set_na(unsigned worker, R_xlen_t index)
    {
        builder_.set_na(worker, index);
    }
    CHARR_CXX_HELPER [[nodiscard]] char* reserve(
        unsigned worker, R_xlen_t index, std::size_t length,
        cetype_ext_t encoding
    );

    CHARR_CXX_HELPER [[nodiscard]] OutputStore release_store() noexcept
    {
        OutputStore store = builder_.release_store();
        size_ = 0;
        return store;
    }
    CHARR_R_HELPER SEXP to_sexp() noexcept;

private:
    R_xlen_t size_;
    charport::charvec::ParallelBuilder builder_;
};

/*
 * The serial and parallel builders differ only by a leading worker index.
 * This adapts both to one call shape so a direct kernel is written once and
 * runs under either, instead of being transcribed into a second loop that
 * then drifts from the serial one. Which builder a sink holds is decided at
 * construction and never changes, so the branch in each operation predicts
 * perfectly. Resolving it at run time is what keeps every call site in the
 * kernel a single named callee: a sink template makes the kernel's calls
 * dependent, and a virtual sink makes them indirect. A parallel sink binds
 * the worker for the lifetime of a chunk, so it is constructed inside run()
 * and never shared.
 */
class OutputSink {
public:
    CHARR_NEUTRAL_HELPER explicit OutputSink(
        OutputBuilder& builder
    ) noexcept
        : serial_(&builder), parallel_(nullptr), worker_(0)
    {
    }

    CHARR_NEUTRAL_HELPER OutputSink(
        ParallelOutputBuilder& builder, unsigned worker
    ) noexcept
        : serial_(nullptr), parallel_(&builder), worker_(worker)
    {
    }

    CHARR_CXX_HELPER void set(R_xlen_t index, const OutputRecord& value)
    {
        if (serial_ != nullptr) {
            serial_->set(index, value);
            return;
        }
        parallel_->set(worker_, index, value);
    }
    CHARR_CXX_HELPER void set(
        R_xlen_t index, const char* data, std::size_t length,
        cetype_ext_t encoding
    )
    {
        if (serial_ != nullptr) {
            serial_->set(index, data, length, encoding);
            return;
        }
        parallel_->set(worker_, index, data, length, encoding);
    }
    CHARR_CXX_HELPER void set_validated(
        R_xlen_t index, const OutputRecord& value
    )
    {
        if (serial_ != nullptr) {
            serial_->set_validated(index, value);
            return;
        }
        parallel_->set_validated(worker_, index, value);
    }
    CHARR_CXX_HELPER void set_na(R_xlen_t index)
    {
        if (serial_ != nullptr) {
            serial_->set_na(index);
            return;
        }
        parallel_->set_na(worker_, index);
    }
    CHARR_CXX_HELPER [[nodiscard]] char* reserve(
        R_xlen_t index, std::size_t length, cetype_ext_t encoding
    )
    {
        if (serial_ != nullptr)
            return serial_->reserve(index, length, encoding);
        return parallel_->reserve(worker_, index, length, encoding);
    }

private:
    OutputBuilder* serial_;
    ParallelOutputBuilder* parallel_;
    unsigned worker_;
};

class CHARR_OWNER_TYPE GrowableOutputBuilder {
public:
    CHARR_CXX_HELPER GrowableOutputBuilder();

    GrowableOutputBuilder(const GrowableOutputBuilder&) = delete;
    GrowableOutputBuilder& operator=(const GrowableOutputBuilder&) = delete;
    GrowableOutputBuilder(GrowableOutputBuilder&&) = delete;
    GrowableOutputBuilder& operator=(GrowableOutputBuilder&&) = delete;

    CHARR_CXX_HELPER void reset() noexcept;
    CHARR_NEUTRAL_HELPER std::size_t size() const noexcept;

    CHARR_CXX_HELPER void append(const OutputRecord& value);
    CHARR_CXX_HELPER void append(
        const char* data, std::size_t length, cetype_ext_t encoding
    );
    CHARR_CXX_HELPER void append(
        std::string_view value, cetype_ext_t encoding
    );
    CHARR_CXX_HELPER void append_na();
    CHARR_CXX_HELPER [[nodiscard]] char* append_reserve(
        std::size_t length, cetype_ext_t encoding
    );

    CHARR_CXX_HELPER [[nodiscard]] OutputStore release_store() noexcept;
    CHARR_R_HELPER SEXP to_sexp() noexcept;

private:
    charport::charvec::GrowableBuilder builder_;
};

} // namespace io
} // namespace altrep_backend
} // namespace charr

#endif
