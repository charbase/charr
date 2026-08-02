
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
#include "../shared/entrypoint.h"
#include "../shared/protect.h"
#include "../shared/read_lines.h"
#include "../shared/unwind.h"

#include <cstddef>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace charr { namespace base_backend {

namespace read_lines {

CHARR_R_HELPER void emit_file_warnings(
    const char* description,
    shared::read_lines::FileCondition condition,
    int error_number
) noexcept {
    if (condition == shared::read_lines::FileCondition::directory) {
        Rf_warning(
            "'raw = FALSE' but '%s' is not a regular file", description
        );
        Rf_warning(
            "cannot open file '%s': it is a directory", description
        );
        return;
    }

    Rf_warning(
        "cannot open file '%s': %s",
        description, std::strerror(error_number)
    );
}

CHARR_R_HELPER void emit_invalid_warning(
    const char* message
) noexcept {
    Rf_warning("%s", message);
}

} // namespace read_lines

using namespace read_lines;


CHARR_ENTRYPOINT SEXP ci_read_lines(
    SEXP path, SEXP encoding
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    path = entry_protections.protect_one(ci__prepare_arg_string_1_r(path, "con"));
    encoding = entry_protections.protect_one(ci__prepare_arg_string_1_r(
        encoding, "encoding"
    ));

    bool file_failed = false;
    shared::read_lines::FileCondition file_condition =
        shared::read_lines::FileCondition::open_failed;
    int file_errno = 0;
    std::size_t invalid_warning_count = 0;
    char invalid_warning[shared::read_lines::invalid_warning_size] = {};

    try {
        shared::read_lines::FileReader file;
        std::string description;
        std::string expanded_path;
        std::vector<char> bytes;
        std::vector<char> repaired;
        shared::line_split::ScanResult scan;

        result = shared::unwind_protect(
            unwind_token,
            [&]() -> SEXP {
                const SEXP path_string = STRING_ELT(path, 0);
                if (path_string == NA_STRING)
                    throw StriException("invalid 'description' argument");
                if (STRING_ELT(encoding, 0) == NA_STRING)
                    throw StriException("invalid 'encoding' value");

                description = Rf_translateChar(path_string);
                expanded_path = R_ExpandFileName(description.c_str());

                try {
                    file.reset(expanded_path.c_str());
                    const int file_length = file.size();
                    bytes.resize(static_cast<std::size_t>(file_length));
                    file.read(file_length == 0 ? nullptr : bytes.data());
                    file.close();
                }
                catch (const shared::read_lines::FileConditionError& error) {
                    file_failed = true;
                    file_condition = error.condition();
                    file_errno = error.error();
                }

                if (file_failed) {
                    emit_file_warnings(
                        description.c_str(), file_condition, file_errno
                    );
                    throw StriException("cannot open the connection");
                }

                const char* utf8 = bytes.empty() ? "" : bytes.data();
                int utf8_length = static_cast<int>(bytes.size());
                if (shared::read_lines::has_utf8_bom(
                        utf8, utf8_length
                    )) {
                    utf8 += 3;
                    utf8_length -= 3;
                }

                shared::line_split::scan_utf8(
                    utf8, utf8_length, false, false, scan
                );
                if (scan.embedded_nul)
                    throw StriException("embedded nul in string");

                if (!scan.invalid.empty()) {
                    shared::read_lines::format_invalid_warning(
                        utf8, scan.invalid[0],
                        invalid_warning, sizeof(invalid_warning)
                    );
                    invalid_warning_count = scan.invalid.size();
                    shared::read_lines::repair_utf8(
                        utf8, utf8_length, scan.invalid, repaired
                    );
                    utf8 = repaired.empty() ? "" : repaired.data();
                    utf8_length = static_cast<int>(repaired.size());
                    shared::line_split::scan_utf8(
                        utf8, utf8_length, false, false, scan
                    );
                }

                result = entry_protections.reprotect_one(
                    Rf_allocVector(
                        STRSXP,
                        static_cast<R_xlen_t>(scan.lines.size())
                    ),
                    result_index
                );
                for (std::size_t i = 0; i < scan.lines.size(); ++i) {
                    const shared::line_split::LineSlice& line = scan.lines[i];
                    SET_STRING_ELT(
                        result, static_cast<R_xlen_t>(i),
                        Rf_mkCharLenCE(
                            utf8 + line.begin, line.length, CE_UTF8
                        )
                    );
                }

                for (std::size_t i = 0;
                        i < invalid_warning_count; ++i) {
                    emit_invalid_warning(invalid_warning);
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}

} } // namespace charr::base_backend
