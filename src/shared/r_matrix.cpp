#include "r_matrix.h"

namespace charr {
namespace shared {

SEXP filled_integer_matrix_r(
    R_len_t rows, R_len_t columns, int value
) noexcept
{
    SEXP result = PROTECT(Rf_allocMatrix(INTSXP, rows, columns));
    int* output = INTEGER(result);
    const R_xlen_t size = XLENGTH(result);
    for (R_xlen_t i = 0; i < size; ++i)
        output[i] = value;
    UNPROTECT(1);
    return result;
}

} // namespace shared
} // namespace charr
