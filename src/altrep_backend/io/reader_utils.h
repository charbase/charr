// Copyright (c) 2026 charr authors
// SPDX-License-Identifier: MIT

#ifndef CHARR_ALTREP_IO_READER_UTILS_H
#define CHARR_ALTREP_IO_READER_UTILS_H

#include "charport.h"
#include "../../shared/ascii.h"
#include "../../shared/lint.h"

#include <cstddef>
#include <stdexcept>
#include <string>

namespace charr {
namespace altrep_backend {
namespace io {

CHARR_CXX_HELPER inline R_len_t checked_r_len(
    R_xlen_t size, const char* what
)
{
    if (size < 0 || size > R_LEN_T_MAX) {
        throw std::length_error(
            std::string("long ") + what + " are not supported"
        );
    }
    return static_cast<R_len_t>(size);
}


using shared::is_ascii;

} // namespace io
} // namespace altrep_backend
} // namespace charr

#endif
