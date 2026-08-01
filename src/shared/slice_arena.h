#ifndef CHARR_SHARED_SLICE_ARENA_H
#define CHARR_SHARED_SLICE_ARENA_H

#include "lint.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace charr {
namespace shared {

class CHARR_OWNER_TYPE SliceArena {
public:
    CHARR_CXX_HELPER SliceArena() noexcept
        : slices_(), current_(nullptr), current_capacity_(0), current_used_(0)
    {
    }

    SliceArena(const SliceArena&) = delete;
    SliceArena& operator=(const SliceArena&) = delete;
    SliceArena(SliceArena&&) = delete;
    SliceArena& operator=(SliceArena&&) = delete;

    CHARR_CXX_HELPER char* allocate(std::size_t bytes)
    {
        assert(bytes > 0);
        if (current_ == nullptr || bytes > current_capacity_ - current_used_)
            start_slice(bytes);

        char* output = current_ + current_used_;
        current_used_ += bytes;
        assert(current_used_ <= current_capacity_);
        return output;
    }

    CHARR_NEUTRAL_HELPER bool valid() const noexcept
    {
        return current_used_ <= current_capacity_ &&
            ((current_ == nullptr) == (current_capacity_ == 0));
    }

private:
    static constexpr std::size_t initial_slice_bytes = 64;
    static constexpr std::size_t maximum_regular_slice_bytes = 256U << 10;

    std::vector<std::unique_ptr<char[]>> slices_;
    char* current_;
    std::size_t current_capacity_;
    std::size_t current_used_;

    CHARR_CXX_HELPER void start_slice(std::size_t required)
    {
        std::size_t regular;
        if (current_capacity_ == 0) {
            regular = initial_slice_bytes;
        }
        else if (current_capacity_ >= maximum_regular_slice_bytes / 2) {
            regular = maximum_regular_slice_bytes;
        }
        else {
            regular = current_capacity_ * 2;
        }

        const std::size_t capacity = std::max(required, regular);
        std::unique_ptr<char[]> slice = std::make_unique<char[]>(capacity);
        char* data = slice.get();
        slices_.push_back(std::move(slice));
        current_ = data;
        current_capacity_ = capacity;
        current_used_ = 0;
    }
};

} // namespace shared
} // namespace charr

#endif
