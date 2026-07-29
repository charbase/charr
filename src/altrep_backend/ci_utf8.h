#ifndef CHARR_CI_UTF8_H
#define CHARR_CI_UTF8_H

#include "io/utf8_input.h"
#include "ci_reader.h"

#include <charport.h>

#include <vector>

namespace charr { namespace altrep_backend {

charport::charvec::Store ci__subset_by_logical(
    const io::Utf8Input& input, const std::vector<int>& which,
    int result_count
);


} } // namespace charr::altrep_backend

#endif
