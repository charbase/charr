
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
#include "io/utf8_output.h"
#include "../shared/entrypoint.h"
#include "../shared/protect.h"
#include "../shared/read_lines.h"
#include "../shared/unwind.h"

#include <charport.h>

#include <cstddef>
#include <cstring>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace charr { namespace altrep_backend {

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

CHARR_CXX_HELPER void build_store_records(
    const char* data,
    const shared::line_split::ScanResult& scan,
    io::OutputStore& output
)
{
    output.records = charport::charvec::components::RecordTable(
        static_cast<R_xlen_t>(scan.lines.size())
    );
    for (std::size_t i = 0; i < scan.lines.size(); ++i) {
        const shared::line_split::LineSlice& line = scan.lines[i];
        output.records.set(
            i,
            line.length == 0
                ? charport::charvec::components::empty_data()
                : data + line.begin,
            line.length,
            line.ascii
                ? CETYPE_EXT_ASCII
                : CETYPE_EXT_UTF8
        );
    }
}

} // namespace read_lines

using namespace read_lines;


CHARR_ENTRYPOINT SEXP ci_read_lines(
    SEXP path, SEXP encoding
) noexcept
{
    CHARR_ENTRYPOINT_BEGIN();

    path = entry_protections.protect_one(
        ci__prepare_arg_string_1_r(path, "con")
    );
    encoding = entry_protections.protect_one(
        ci__prepare_arg_string_1_r(
            encoding, "encoding"
        )
    );


    bool file_failed = false;
    shared::read_lines::FileCondition file_condition =
        shared::read_lines::FileCondition::open_failed;
    int file_errno = 0;
    int file_length = 0;

    try {
        shared::read_lines::FileReader file;
        std::string description;
        std::string expanded_path;
        io::OutputStore file_bytes;
        io::OutputStore output;
        std::vector<char> repaired;
        shared::line_split::ScanResult source_scan;
        shared::line_split::ScanResult output_scan;

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
                    file_length = file.size();
                    file_bytes = io::OutputStore(
                        0, static_cast<std::size_t>(file_length)
                    );
                    file.read(
                        file_length == 0
                            ? nullptr
                            : file_bytes.slices.front_data()
                    );
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

                const char* source = file_bytes.slices.empty()
                    ? ""
                    : file_bytes.slices.front_data();
                int source_length = file_length;
                if (shared::read_lines::has_utf8_bom(
                        source, source_length
                    )) {
                    source += 3;
                    source_length -= 3;
                }

                shared::line_split::scan_utf8(
                    source, source_length, false, false, source_scan
                );
                if (source_scan.embedded_nul)
                    throw StriException("embedded nul in string");

                if (source_scan.invalid.empty()) {
                    build_store_records(source, source_scan, file_bytes);
                    output = std::move(file_bytes);
                }
                else {
                    shared::read_lines::repair_utf8(
                        source, source_length, source_scan.invalid, repaired
                    );
                    const int output_length = static_cast<int>(
                        repaired.size()
                    );
                    output = io::OutputStore(
                        0, static_cast<std::size_t>(output_length)
                    );
                    char* destination = output_length == 0
                        ? nullptr
                        : output.slices.front_data();
                    if (output_length > 0) {
                        std::memcpy(
                            destination, repaired.data(),
                            static_cast<std::size_t>(output_length)
                        );
                    }
                    const char* repaired_data = output_length == 0
                        ? ""
                        : destination;
                    shared::line_split::scan_utf8(
                        repaired_data, output_length,
                        false, false, output_scan
                    );
                    build_store_records(repaired_data, output_scan, output);
                }

                result = entry_protections.reprotect_one(
                    io::finalize(std::move(output)), result_index
                );

                for (std::size_t i = 0;
                        i < source_scan.invalid.size(); ++i) {
                    char warning[
                        shared::read_lines::invalid_warning_size
                    ] = {};
                    shared::read_lines::format_invalid_warning(
                        source, source_scan.invalid[i],
                        warning, sizeof(warning)
                    );
                    emit_invalid_warning(warning);
                }

                CHARR_UNWIND_RETURN();
            }
        );
    }
    CHARR_ENTRYPOINT_END();
}

} } // namespace charr::altrep_backend
