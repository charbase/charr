#ifndef CHARR_ALTREP_IO_STRING_VIEW_H
#define CHARR_ALTREP_IO_STRING_VIEW_H

#include "../../shared/string_view.h"

#include <charport.h>

namespace charr {
namespace altrep_backend {
namespace io {

CHARR_NEUTRAL_HELPER inline shared::StringView as_shared_view(
    const charport::StrView& value
) noexcept {
    shared::StringEncoding encoding = shared::StringEncoding::unknown;
    switch (value.enc) {
    case cetype_ext_t::CE_NATIVE:
        encoding = shared::StringEncoding::native;
        break;
    case cetype_ext_t::CE_UTF8:
        encoding = shared::StringEncoding::utf8;
        break;
    case cetype_ext_t::CE_LATIN1:
        encoding = shared::StringEncoding::latin1;
        break;
    case cetype_ext_t::CE_BYTES:
        encoding = shared::StringEncoding::bytes;
        break;
    case cetype_ext_t::CE_ASCII:
        encoding = shared::StringEncoding::ascii;
        break;
    case cetype_ext_t::CE_ASCII_OR_UTF8:
        encoding = shared::StringEncoding::ascii_or_utf8;
        break;
    case cetype_ext_t::CE_NA:
        encoding = shared::StringEncoding::missing;
        break;
    }
    return shared::StringView{value.ptr, value.len, encoding};
}


CHARR_NEUTRAL_HELPER inline charport::StrView as_charport_view(
    const shared::StringView& value
) noexcept {
    cetype_ext_t encoding = cetype_ext_t::CE_NA;
    switch (value.enc) {
    case shared::StringEncoding::native:
        encoding = cetype_ext_t::CE_NATIVE;
        break;
    case shared::StringEncoding::utf8:
        encoding = cetype_ext_t::CE_UTF8;
        break;
    case shared::StringEncoding::latin1:
        encoding = cetype_ext_t::CE_LATIN1;
        break;
    case shared::StringEncoding::bytes:
        encoding = cetype_ext_t::CE_BYTES;
        break;
    case shared::StringEncoding::ascii:
        encoding = cetype_ext_t::CE_ASCII;
        break;
    case shared::StringEncoding::ascii_or_utf8:
        encoding = cetype_ext_t::CE_ASCII_OR_UTF8;
        break;
    case shared::StringEncoding::missing:
    case shared::StringEncoding::unknown:
        encoding = cetype_ext_t::CE_NA;
        break;
    }
    return charport::StrView{
        value.ptr,
        value.enc == shared::StringEncoding::missing
            ? NA_INTEGER
            : value.len,
        encoding
    };
}

} // namespace io
} // namespace altrep_backend
} // namespace charr

#endif
