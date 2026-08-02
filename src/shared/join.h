// Derived from stringi.
// Copyright (c) 2013-2025, Marek Gagolewski. See inst/COPYRIGHTS.

#ifndef CHARR_SHARED_JOIN_H
#define CHARR_SHARED_JOIN_H

#include "lint.h"
#include "string_view.h"

#include <cstddef>

namespace charr {
namespace shared {
namespace join {

struct FlattenPlan {
    std::size_t bytes;
    std::size_t pieces;
    bool has_na;
    bool too_large;
    bool ascii;
};

struct Column {
    const StringView* values;
    std::size_t size;
};

CHARR_NEUTRAL_HELPER void plan_flatten(
    const StringView* values,
    std::size_t size,
    const StringView* separator,
    int na_empty,
    bool omit_empty,
    FlattenPlan& plan
) noexcept;

CHARR_NEUTRAL_HELPER void write_flatten(
    const StringView* values,
    std::size_t size,
    const StringView* separator,
    int na_empty,
    bool omit_empty,
    char* output
) noexcept;

CHARR_NEUTRAL_HELPER void plan_join_row(
    const Column* columns,
    std::size_t column_count,
    std::size_t row,
    std::size_t row_count,
    const StringView& separator,
    FlattenPlan& plan
) noexcept;

CHARR_NEUTRAL_HELPER void write_join_row(
    const Column* columns,
    std::size_t column_count,
    std::size_t row,
    std::size_t row_count,
    const StringView& separator,
    char* output
) noexcept;

CHARR_NEUTRAL_HELPER void plan_join_all(
    const Column* columns,
    std::size_t column_count,
    std::size_t row_count,
    const StringView& separator,
    const StringView& collapse,
    FlattenPlan& plan
) noexcept;

CHARR_NEUTRAL_HELPER void write_join_all(
    const Column* columns,
    std::size_t column_count,
    std::size_t row_count,
    const StringView& separator,
    const StringView& collapse,
    char* output
) noexcept;

} // namespace join
} // namespace shared
} // namespace charr

#endif
