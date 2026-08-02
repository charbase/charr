// Derived from stringi.
// Copyright (c) 2013-2025, Marek Gagolewski. See inst/COPYRIGHTS.

#include "join.h"

#include <R_ext/Arith.h>

#include <climits>
#include <cstring>

namespace charr {
namespace shared {
namespace join {

CHARR_NEUTRAL_HELPER static bool included(
    const StringView& value,
    int na_empty,
    bool omit_empty
) noexcept {
    if (value.is_na() && na_empty == NA_LOGICAL)
        return false;
    if (omit_empty && (value.is_na() || value.len == 0))
        return false;
    return true;
}

CHARR_NEUTRAL_HELPER static bool is_ascii(
    const StringView& value
) noexcept {
    if (value.is_na() || value.enc == StringEncoding::ascii)
        return true;
    if (value.enc != StringEncoding::ascii_or_utf8)
        return false;

    for (int i = 0; i < value.len; ++i) {
        if (static_cast<unsigned char>(value.ptr[i]) > 0x7fU)
            return false;
    }
    return true;
}

CHARR_NEUTRAL_HELPER static void add_bytes(
    FlattenPlan& plan,
    std::size_t bytes
) noexcept {
    const std::size_t limit = static_cast<std::size_t>(INT_MAX);
    if (plan.too_large || bytes > limit-plan.bytes) {
        plan.too_large = true;
        return;
    }
    plan.bytes += bytes;
}

CHARR_NEUTRAL_HELPER static const StringView& value_at(
    const Column& column,
    std::size_t row,
    std::size_t row_count
) noexcept {
    if (column.size == 1)
        return column.values[0];
    if (column.size == row_count)
        return column.values[row];
    return column.values[row % column.size];
}

CHARR_NEUTRAL_HELPER static void copy_bytes(
    const StringView& value,
    char* output,
    std::size_t& cursor
) noexcept {
    if (value.len <= 0)
        return;
    const std::size_t length = static_cast<std::size_t>(value.len);
    std::memcpy(output+cursor, value.ptr, length);
    cursor += length;
}

void plan_flatten(
    const StringView* values,
    std::size_t size,
    const StringView* separator,
    int na_empty,
    bool omit_empty,
    FlattenPlan& plan
) noexcept {
    plan = FlattenPlan{0, 0, false, false, true};

    for (std::size_t i = 0; i < size; ++i) {
        const StringView& value = values[i];
        if (value.is_na() && na_empty != NA_LOGICAL && !na_empty) {
            plan.has_na = true;
            continue;
        }
        if (!included(value, na_empty, omit_empty))
            continue;

        if (!value.is_na()) {
            add_bytes(plan, static_cast<std::size_t>(value.len));
            plan.ascii = plan.ascii && is_ascii(value);
        }
        ++plan.pieces;
    }

    if (separator == nullptr || plan.pieces <= 1)
        return;

    const std::size_t separator_count = plan.pieces-1;
    const std::size_t separator_length =
        static_cast<std::size_t>(separator->len);
    const std::size_t limit = static_cast<std::size_t>(INT_MAX);
    if (separator_length > 0 &&
            separator_count > (limit-plan.bytes)/separator_length) {
        plan.too_large = true;
    }
    else {
        plan.bytes += separator_count*separator_length;
    }
    plan.ascii = plan.ascii && is_ascii(*separator);
}

void write_flatten(
    const StringView* values,
    std::size_t size,
    const StringView* separator,
    int na_empty,
    bool omit_empty,
    char* output
) noexcept {
    std::size_t cursor = 0;
    bool started = false;
    for (std::size_t i = 0; i < size; ++i) {
        const StringView& value = values[i];
        if (!included(value, na_empty, omit_empty))
            continue;

        if (started && separator != nullptr && separator->len > 0) {
            const std::size_t length =
                static_cast<std::size_t>(separator->len);
            std::memcpy(output+cursor, separator->ptr, length);
            cursor += length;
        }
        else if (!started) {
            started = true;
        }

        if (!value.is_na() && value.len > 0) {
            const std::size_t length = static_cast<std::size_t>(value.len);
            std::memcpy(output+cursor, value.ptr, length);
            cursor += length;
        }
    }
}

void plan_join_row(
    const Column* columns,
    std::size_t column_count,
    std::size_t row,
    std::size_t row_count,
    const StringView& separator,
    FlattenPlan& plan
) noexcept {
    plan = FlattenPlan{0, 0, false, false, true};
    for (std::size_t column = 0; column < column_count; ++column) {
        const StringView& value = value_at(
            columns[column], row, row_count
        );
        if (value.is_na()) {
            plan.has_na = true;
            return;
        }
        if (column > 0) {
            add_bytes(plan, static_cast<std::size_t>(separator.len));
            plan.ascii = plan.ascii && is_ascii(separator);
        }
        add_bytes(plan, static_cast<std::size_t>(value.len));
        plan.ascii = plan.ascii && is_ascii(value);
        ++plan.pieces;
    }
}

void write_join_row(
    const Column* columns,
    std::size_t column_count,
    std::size_t row,
    std::size_t row_count,
    const StringView& separator,
    char* output
) noexcept {
    std::size_t cursor = 0;
    for (std::size_t column = 0; column < column_count; ++column) {
        if (column > 0)
            copy_bytes(separator, output, cursor);
        copy_bytes(
            value_at(columns[column], row, row_count), output, cursor
        );
    }
}

void plan_join_all(
    const Column* columns,
    std::size_t column_count,
    std::size_t row_count,
    const StringView& separator,
    const StringView& collapse,
    FlattenPlan& plan
) noexcept {
    plan = FlattenPlan{0, 0, false, false, true};
    for (std::size_t row = 0; row < row_count; ++row) {
        if (row > 0) {
            add_bytes(plan, static_cast<std::size_t>(collapse.len));
            plan.ascii = plan.ascii && is_ascii(collapse);
        }
        for (std::size_t column = 0; column < column_count; ++column) {
            const StringView& value = value_at(
                columns[column], row, row_count
            );
            if (value.is_na()) {
                plan.has_na = true;
                return;
            }
            if (column > 0) {
                add_bytes(plan, static_cast<std::size_t>(separator.len));
                plan.ascii = plan.ascii && is_ascii(separator);
            }
            add_bytes(plan, static_cast<std::size_t>(value.len));
            plan.ascii = plan.ascii && is_ascii(value);
            ++plan.pieces;
        }
    }
}

void write_join_all(
    const Column* columns,
    std::size_t column_count,
    std::size_t row_count,
    const StringView& separator,
    const StringView& collapse,
    char* output
) noexcept {
    std::size_t cursor = 0;
    for (std::size_t row = 0; row < row_count; ++row) {
        if (row > 0)
            copy_bytes(collapse, output, cursor);
        for (std::size_t column = 0; column < column_count; ++column) {
            if (column > 0)
                copy_bytes(separator, output, cursor);
            copy_bytes(
                value_at(columns[column], row, row_count), output, cursor
            );
        }
    }
}

} // namespace join
} // namespace shared
} // namespace charr
