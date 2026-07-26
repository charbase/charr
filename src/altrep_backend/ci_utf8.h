#ifndef CHARR_CI_UTF8_H
#define CHARR_CI_UTF8_H

#include "altrep/utf8_input.h"
#include "ci_reader.h"

#include <charport.h>

#include <vector>

using charr::altrep::ByteView;
using charr::altrep::IndexedUtf8Input;
using charr::altrep::Utf8BomPolicy;
using charr::altrep::Utf8Input;
using charr::altrep::Utf8Record;
using charr::altrep::Utf8Workspace;

charport::charvec::Store ci__subset_by_logical(
    const Utf8Input& input, const std::vector<int>& which,
    int result_count
);

#endif
