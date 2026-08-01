// Adapted from stringi 19e9586ba39b3320df49355e32bd18d74ed6098f. See inst/COPYRIGHTS.
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

#ifndef CHARR_SHARED_BYTE_SEARCH_MATCHER_H
#define CHARR_SHARED_BYTE_SEARCH_MATCHER_H

#include "lint.h"

#include <unicode/uchar.h>
#include <unicode/utf8.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace charr { namespace shared {

class CHARR_OWNER_TYPE ByteSearchMatcher {
public:
    static constexpr int not_found = -1;

private:
    enum class Strategy : unsigned char {
        kmp,
        kmp_case_insensitive,
        single_byte,
        short_pattern
    };

    Strategy strategy_;
    bool overlap_;

    int search_pos_;
    int search_end_;
    const char* search_;
    int search_length_;

    int pattern_length_;
    const char* pattern_;

    std::unique_ptr<int[]> kmp_next_;
    int pattern_pos_;
    int folded_pattern_length_;
    std::unique_ptr<UChar32[]> folded_pattern_;

    CHARR_CXX_HELPER int find_from_pos_kmp(int start)
    {
#ifndef NDEBUG
        if (!search_)
            throw std::logic_error("ByteSearchMatcher: reset() has not been called");
#endif
        int current = start;
        pattern_pos_ = 0;

        while (current < search_length_) {
            while (pattern_pos_ >= 0 &&
                    pattern_[pattern_pos_] != search_[current]) {
                pattern_pos_ = kmp_next_[pattern_pos_];
            }
            ++pattern_pos_;
            ++current;
            if (pattern_pos_ == pattern_length_) {
                search_end_ = current;
                search_pos_ = current-pattern_length_;
                return search_pos_;
            }
        }

        search_pos_ = search_end_ = search_length_;
        return not_found;
    }

    CHARR_CXX_HELPER int find_from_pos_kmp_case_insensitive(int start)
    {
        int current = start;
        pattern_pos_ = 0;

        UChar32 code_point = 0;
        while (current < search_length_) {
            U8_NEXT(search_, current, search_length_, code_point);
            code_point = u_toupper(code_point);
            while (pattern_pos_ >= 0 &&
                    folded_pattern_[pattern_pos_] != code_point) {
                pattern_pos_ = kmp_next_[pattern_pos_];
            }
            ++pattern_pos_;
            if (pattern_pos_ == folded_pattern_length_) {
                search_end_ = current;

                int remaining = folded_pattern_length_;
                search_pos_ = current;
                while (remaining > 0) {
                    U8_BACK_1(
                        reinterpret_cast<const uint8_t*>(search_),
                        0, search_pos_
                    );
                    --remaining;
                }
                return search_pos_;
            }
        }

        search_pos_ = search_end_ = search_length_;
        return not_found;
    }

    CHARR_CXX_HELPER int find_from_pos_single_byte(int start)
    {
#ifndef NDEBUG
        if (!search_)
            throw std::logic_error("ByteSearchMatcher: reset() has not been called");
#endif
        if (start > search_length_-pattern_length_) {
            search_pos_ = search_end_ = search_length_;
            return not_found;
        }

        const char* result = static_cast<const char*>(std::memchr(
            search_+start,
            static_cast<unsigned char>(pattern_[0]),
            static_cast<std::size_t>(search_length_-start)
        ));
        if (!result) {
            search_pos_ = search_end_ = search_length_;
            return not_found;
        }

        search_pos_ = static_cast<int>(result-search_);
        search_end_ = search_pos_+1;
        return search_pos_;
    }

    CHARR_CXX_HELPER int find_from_pos_short(int start)
    {
#ifndef NDEBUG
        if (!search_)
            throw std::logic_error("ByteSearchMatcher: reset() has not been called");
#endif
        if (pattern_length_ <= 0 ||
                start > search_length_-pattern_length_) {
            search_pos_ = search_end_ = search_length_;
            return not_found;
        }

        const int last_position = search_length_-pattern_length_;
        const char* current = search_+start;
        const char* const last = search_+last_position;
        while (current <= last) {
            const char* candidate = static_cast<const char*>(std::memchr(
                current,
                static_cast<unsigned char>(pattern_[0]),
                static_cast<std::size_t>(last-current+1)
            ));
            if (!candidate)
                break;

            if (std::memcmp(
                    candidate+1, pattern_+1,
                    static_cast<std::size_t>(pattern_length_-1)
                ) == 0) {
                search_pos_ = static_cast<int>(candidate-search_);
                search_end_ = search_pos_+pattern_length_;
                return search_pos_;
            }
            current = candidate+1;
        }

        search_pos_ = search_end_ = search_length_;
        return not_found;
    }

    CHARR_CXX_HELPER int find_from_pos(int start)
    {
        switch (strategy_) {
        case Strategy::kmp:
            return find_from_pos_kmp(start);
        case Strategy::kmp_case_insensitive:
            return find_from_pos_kmp_case_insensitive(start);
        case Strategy::single_byte:
            return find_from_pos_single_byte(start);
        case Strategy::short_pattern:
            return find_from_pos_short(start);
        }
        throw std::logic_error("ByteSearchMatcher: invalid strategy");
    }

    CHARR_CXX_HELPER void prepare_kmp_forward(
        int length, const char* pattern
    )
    {
        if (kmp_next_[0] > -100)
            return;
        kmp_next_[0] = -1;
        for (int i = 0; i < length; ++i) {
            kmp_next_[i+1] = kmp_next_[i]+1;
            while (kmp_next_[i+1] > 0 &&
                    pattern[i] != pattern[kmp_next_[i+1]-1]) {
                kmp_next_[i+1] = kmp_next_[kmp_next_[i+1]-1]+1;
            }
        }
    }

    CHARR_CXX_HELPER void prepare_folded_kmp_forward()
    {
        if (kmp_next_[0] > -100)
            return;
        kmp_next_[0] = -1;
        for (int i = 0; i < folded_pattern_length_; ++i) {
            kmp_next_[i+1] = kmp_next_[i]+1;
            while (kmp_next_[i+1] > 0 &&
                    folded_pattern_[i] !=
                    folded_pattern_[kmp_next_[i+1]-1]) {
                kmp_next_[i+1] = kmp_next_[kmp_next_[i+1]-1]+1;
            }
        }
    }

    CHARR_CXX_HELPER int find_first_kmp()
    {
        prepare_kmp_forward(pattern_length_, pattern_);
        return find_from_pos_kmp(0);
    }

    CHARR_CXX_HELPER int find_last_kmp()
    {
        if (kmp_next_[0] <= -100) {
            kmp_next_[0] = -1;
            for (int i = 0; i < pattern_length_; ++i) {
                kmp_next_[i+1] = kmp_next_[i]+1;
                while (kmp_next_[i+1] > 0 &&
                        pattern_[pattern_length_-i-1] !=
                        pattern_[pattern_length_-(kmp_next_[i+1]-1)-1]) {
                    kmp_next_[i+1] = kmp_next_[kmp_next_[i+1]-1]+1;
                }
            }
        }

        int current = search_length_;
        pattern_pos_ = 0;
        while (current > 0) {
            --current;
            while (pattern_pos_ >= 0 &&
                    pattern_[pattern_length_-1-pattern_pos_] !=
                    search_[current]) {
                pattern_pos_ = kmp_next_[pattern_pos_];
            }
            ++pattern_pos_;
            if (pattern_pos_ == pattern_length_) {
                search_end_ = current+pattern_length_;
                search_pos_ = current;
                return search_pos_;
            }
        }
        search_pos_ = search_end_ = search_length_;
        return not_found;
    }

    CHARR_CXX_HELPER int find_first_kmp_case_insensitive()
    {
        prepare_folded_kmp_forward();
        return find_from_pos_kmp_case_insensitive(0);
    }

    CHARR_CXX_HELPER int find_last_kmp_case_insensitive()
    {
        if (kmp_next_[0] <= -100) {
            kmp_next_[0] = -1;
            for (int i = 0; i < folded_pattern_length_; ++i) {
                kmp_next_[i+1] = kmp_next_[i]+1;
                while (kmp_next_[i+1] > 0 &&
                        folded_pattern_[folded_pattern_length_-i-1] !=
                        folded_pattern_[
                            folded_pattern_length_-(kmp_next_[i+1]-1)-1
                        ]) {
                    kmp_next_[i+1] = kmp_next_[kmp_next_[i+1]-1]+1;
                }
            }
        }

        int current = search_length_;
        pattern_pos_ = 0;
        while (current > 0) {
            UChar32 code_point;
            U8_PREV(search_, 0, current, code_point);
            code_point = u_toupper(code_point);
            while (pattern_pos_ >= 0 &&
                    folded_pattern_[
                        folded_pattern_length_-1-pattern_pos_
                    ] != code_point) {
                pattern_pos_ = kmp_next_[pattern_pos_];
            }
            ++pattern_pos_;
            if (pattern_pos_ == folded_pattern_length_) {
                search_pos_ = current;

                int remaining = folded_pattern_length_;
                search_end_ = current;
                while (remaining > 0) {
                    U8_FWD_1(
                        reinterpret_cast<const uint8_t*>(search_),
                        search_end_, search_length_
                    );
                    --remaining;
                }
                return search_pos_;
            }
        }
        search_pos_ = search_end_ = search_length_;
        return not_found;
    }

    CHARR_CXX_HELPER int find_last_single_byte()
    {
        if (search_length_ < pattern_length_) {
            search_pos_ = search_end_ = search_length_;
            return not_found;
        }

        const unsigned char pattern =
            static_cast<unsigned char>(pattern_[0]);
        for (search_pos_ = search_length_-1; search_pos_ >= 0; --search_pos_) {
            if (pattern == static_cast<unsigned char>(search_[search_pos_])) {
                search_end_ = search_pos_+1;
                return search_pos_;
            }
        }

        search_pos_ = search_end_ = search_length_;
        return not_found;
    }

    CHARR_CXX_HELPER int find_last_short()
    {
        if (pattern_length_ <= 0) {
            search_pos_ = search_end_ = search_length_;
            return not_found;
        }

        for (search_pos_ = search_length_-pattern_length_;
                search_pos_ >= 0; --search_pos_) {
            if (std::memcmp(
                    search_+search_pos_, pattern_,
                    static_cast<std::size_t>(pattern_length_)
                ) == 0) {
                search_end_ = search_pos_+pattern_length_;
                return search_pos_;
            }
        }

        search_pos_ = search_end_ = search_length_;
        return not_found;
    }

public:
    CHARR_CXX_HELPER ByteSearchMatcher(
        const char* pattern, int pattern_length, bool overlap,
        bool case_insensitive
    ) :
        strategy_(case_insensitive
            ? Strategy::kmp_case_insensitive
            : pattern_length == 1
                ? Strategy::single_byte
                : pattern_length < 16
                    ? Strategy::short_pattern
                    : Strategy::kmp),
        overlap_(overlap), search_pos_(-1), search_end_(-1), search_(nullptr),
        search_length_(0), pattern_length_(pattern_length), pattern_(pattern),
        kmp_next_(), pattern_pos_(-1), folded_pattern_length_(0),
        folded_pattern_()
    {
        if (!pattern || pattern_length < 0)
            throw std::invalid_argument("ByteSearchMatcher: invalid pattern");

        if (strategy_ == Strategy::kmp ||
                strategy_ == Strategy::kmp_case_insensitive) {
            const std::size_t capacity =
                static_cast<std::size_t>(pattern_length)+1;
            kmp_next_.reset(new int[capacity]);
            kmp_next_[0] = -100;

            if (strategy_ == Strategy::kmp_case_insensitive) {
                folded_pattern_.reset(new UChar32[capacity]);
                UChar32 code_point = 0;
                int position = 0;
                while (position < pattern_length) {
                    U8_NEXT(pattern, position, pattern_length, code_point);
#ifndef NDEBUG
                    if (folded_pattern_length_ >=
                            static_cast<int>(capacity)) {
                        throw std::logic_error(
                            "ByteSearchMatcher: folded pattern overflow"
                        );
                    }
#endif
                    folded_pattern_[folded_pattern_length_++] =
                        u_toupper(code_point);
                }
            }
        }
    }

    ByteSearchMatcher(const ByteSearchMatcher&) = delete;
    ByteSearchMatcher& operator=(const ByteSearchMatcher&) = delete;

    CHARR_NEUTRAL_HELPER const char* pattern_data() const noexcept
    {
        return pattern_;
    }

    CHARR_CXX_HELPER void reset(const char* search, int search_length)
    {
        if (!search || search_length < 0)
            throw std::invalid_argument("ByteSearchMatcher: invalid subject");
        search_ = search;
        search_length_ = search_length;
        search_pos_ = -1;
        search_end_ = -1;
        pattern_pos_ = -1;
    }

    CHARR_CXX_HELPER int find_first()
    {
        switch (strategy_) {
        case Strategy::kmp:
            return find_first_kmp();
        case Strategy::kmp_case_insensitive:
            return find_first_kmp_case_insensitive();
        case Strategy::single_byte:
            return find_from_pos_single_byte(0);
        case Strategy::short_pattern:
            return find_from_pos_short(0);
        }
        throw std::logic_error("ByteSearchMatcher: invalid strategy");
    }

    CHARR_CXX_HELPER int find_last()
    {
        switch (strategy_) {
        case Strategy::kmp:
            return find_last_kmp();
        case Strategy::kmp_case_insensitive:
            return find_last_kmp_case_insensitive();
        case Strategy::single_byte:
            return find_last_single_byte();
        case Strategy::short_pattern:
            return find_last_short();
        }
        throw std::logic_error("ByteSearchMatcher: invalid strategy");
    }

    CHARR_CXX_HELPER int find_next()
    {
        if (search_pos_ < 0)
            return find_first();

        if (!overlap_)
            return find_from_pos(search_end_);

        int position = search_pos_;
        U8_FWD_1(search_, position, search_length_);
        return find_from_pos(position);
    }

    CHARR_CXX_HELPER int matched_start() const
    {
        if (!search_ || !pattern_)
            throw std::logic_error("ByteSearchMatcher: reset() has not been called");
        if (search_pos_ < 0 || search_end_ <= search_pos_ ||
                search_pos_ >= search_length_ ||
                search_end_ > search_length_) {
            throw std::logic_error(
                "ByteSearchMatcher: no match at the current position"
            );
        }
        return search_pos_;
    }

    CHARR_CXX_HELPER int matched_length() const
    {
        (void) matched_start();
        return search_end_-search_pos_;
    }
};

} } // namespace charr::shared

#endif
