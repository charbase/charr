#ifndef CHARR_ALTREP_STABLE_SLICE_ARENA_H
#define CHARR_ALTREP_STABLE_SLICE_ARENA_H

#include <charport.h>

#include <algorithm>
#include <cassert>
#include <cstddef>

namespace charr {
namespace altrep {

class StableSliceArena {
public:
    StableSliceArena() noexcept
        : slices_(), current_(nullptr), current_capacity_(0), current_used_(0)
    {
    }

    StableSliceArena(const StableSliceArena&) = delete;
    StableSliceArena& operator=(const StableSliceArena&) = delete;
    StableSliceArena(StableSliceArena&&) = delete;
    StableSliceArena& operator=(StableSliceArena&&) = delete;

    char* allocate(std::size_t bytes)
    {
        assert(bytes > 0);
        if (current_ == nullptr || bytes > current_capacity_ - current_used_)
            start_slice(bytes);

        char* output = current_ + current_used_;
        current_used_ += bytes;
        assert(current_used_ <= current_capacity_);
        return output;
    }

    bool valid() const noexcept
    {
        return current_used_ <= current_capacity_ &&
            ((current_ == nullptr) == (current_capacity_ == 0));
    }

private:
    static constexpr std::size_t initial_slice_bytes = 64;
    static constexpr std::size_t maximum_regular_slice_bytes = 256U << 10;

    charport::charvec::components::SliceChain slices_;
    char* current_;
    std::size_t current_capacity_;
    std::size_t current_used_;

    void start_slice(std::size_t required)
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

        current_capacity_ = std::max(required, regular);
        current_ = slices_.push_front(current_capacity_);
        current_used_ = 0;
    }
};

} // namespace altrep
} // namespace charr

#endif
