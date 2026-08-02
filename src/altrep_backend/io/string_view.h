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
    switch (value.enc.value) {
    case CETYPE_EXT_NATIVE.value:
        encoding = shared::StringEncoding::native;
        break;
    case CETYPE_EXT_UTF8.value:
        encoding = shared::StringEncoding::utf8;
        break;
    case CETYPE_EXT_LATIN1.value:
        encoding = shared::StringEncoding::latin1;
        break;
    case CETYPE_EXT_BYTES.value:
        encoding = shared::StringEncoding::bytes;
        break;
    case CETYPE_EXT_ASCII.value:
        encoding = shared::StringEncoding::ascii;
        break;
    case CETYPE_EXT_ASCII_OR_UTF8.value:
        encoding = shared::StringEncoding::ascii_or_utf8;
        break;
    case CETYPE_EXT_NA.value:
        encoding = shared::StringEncoding::missing;
        break;
    }
    return shared::StringView{value.ptr, value.len, encoding};
}


CHARR_NEUTRAL_HELPER inline charport::StrView as_charport_view(
    const shared::StringView& value
) noexcept {
    cetype_ext_t encoding = CETYPE_EXT_NA;
    switch (value.enc) {
    case shared::StringEncoding::native:
        encoding = CETYPE_EXT_NATIVE;
        break;
    case shared::StringEncoding::utf8:
        encoding = CETYPE_EXT_UTF8;
        break;
    case shared::StringEncoding::latin1:
        encoding = CETYPE_EXT_LATIN1;
        break;
    case shared::StringEncoding::bytes:
        encoding = CETYPE_EXT_BYTES;
        break;
    case shared::StringEncoding::ascii:
        encoding = CETYPE_EXT_ASCII;
        break;
    case shared::StringEncoding::ascii_or_utf8:
        encoding = CETYPE_EXT_ASCII_OR_UTF8;
        break;
    case shared::StringEncoding::missing:
    case shared::StringEncoding::unknown:
        encoding = CETYPE_EXT_NA;
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
