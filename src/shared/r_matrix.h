#ifndef CHARR_SHARED_R_MATRIX_H
#define CHARR_SHARED_R_MATRIX_H

#include "lint.h"

#include <Rinternals.h>

namespace charr {
namespace shared {

CHARR_R_HELPER SEXP filled_integer_matrix_r(
    R_len_t rows, R_len_t columns, int value = NA_INTEGER
) noexcept;

} // namespace shared
} // namespace charr

#endif
