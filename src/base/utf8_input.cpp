#include "utf8_input.h"

#include <Rversion.h>

#include <unicode/utf8.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>
#include <string>

namespace charr {
namespace base {

namespace {

R_xlen_t checked_recycle_size(R_xlen_t source_size, R_xlen_t recycle_size)
{
    if (recycle_size < 0)
        throw StriException("negative recycle length");
    return source_size == 0 || recycle_size == 0 ? 0 : recycle_size;
}

SEXP checked_source(SEXP source)
{
    if (TYPEOF(source) != STRSXP)
        throw StriException("UTF-8 input requires a character vector");
    return source;
}

const SEXP* acquire_elements(SEXP source, R_xlen_t size)
{
    if (size == 0)
        return nullptr;
    const SEXP* elements = STRING_PTR_RO(source);
    if (elements == nullptr)
        throw StriException("R returned a null character-vector pointer");
    return elements;
}

std::size_t checked_vector_size(R_xlen_t size)
{
    if (size < 0)
        throw StriException("negative R character-vector length");
    const auto unsigned_size = static_cast<unsigned long long>(size);
    if (unsigned_size > std::numeric_limits<std::size_t>::max())
        throw StriException("R character vector is too large");
    return static_cast<std::size_t>(size);
}

R_len_t checked_short_size(R_xlen_t size)
{
    if (size < 0 || size > R_LEN_T_MAX)
        throw StriException("long character vectors are not supported");
    return static_cast<R_len_t>(size);
}

bool has_utf8_bom(const char* data, R_len_t length) noexcept
{
    return length >= 3 &&
        static_cast<unsigned char>(data[0]) == 0xefU &&
        static_cast<unsigned char>(data[1]) == 0xbbU &&
        static_cast<unsigned char>(data[2]) == 0xbfU;
}

bool native_charsxp_is_ascii(SEXP charsxp, const char* data, int length)
{
#if R_VERSION >= R_Version(4, 5, 0)
    (void)data;
    (void)length;
    return Rf_charIsASCII(charsxp) == TRUE;
#else
    (void)charsxp;
    for (int i = 0; i < length; ++i) {
        if (static_cast<unsigned char>(data[i]) >= 0x80U)
            return false;
    }
    return true;
#endif
}

#if R_VERSION >= R_Version(4, 5, 0)
bool latin1_charsxp_is_ascii(SEXP charsxp)
{
    return Rf_charIsASCII(charsxp) == TRUE;
}
#endif

Utf8Record missing_record() noexcept
{
    return Utf8Record{nullptr, NA_INTEGER, Utf8RecordState::missing};
}

} // namespace

Utf8Input::Utf8Input(
    SEXP source, R_xlen_t recycle_size, Utf8BomPolicy bom_policy
) : source_(checked_source(source)), source_size_(XLENGTH(source_)),
    recycle_size_(checked_recycle_size(source_size_, recycle_size)),
    bom_policy_(bom_policy),
    elements_(acquire_elements(source_, source_size_)),
    records_(checked_vector_size(source_size_)),
    source_borrowed_(checked_vector_size(source_size_), 1),
    converted_(), converter_()
{
    checked_short_size(source_size_);
    checked_short_size(recycle_size_);
    initialize_records();
    assert_invariants();
}

R_xlen_t Utf8Input::source_size() const noexcept
{
    return source_size_;
}

R_xlen_t Utf8Input::size() const noexcept
{
    return recycle_size_;
}

const Utf8Record* Utf8Input::source_data() const noexcept
{
    return records_.empty() ? nullptr : records_.data();
}

R_len_t Utf8Input::get_n() const noexcept
{
    return static_cast<R_len_t>(source_size_);
}

R_len_t Utf8Input::get_nrecycle() const noexcept
{
    return static_cast<R_len_t>(recycle_size_);
}

R_len_t Utf8Input::vectorize_init() const noexcept
{
    return source_size_ <= 0 ? get_nrecycle() : 0;
}

R_len_t Utf8Input::vectorize_end() const noexcept
{
    return get_nrecycle();
}

R_len_t Utf8Input::vectorize_next(R_len_t index) const noexcept
{
    const R_len_t source_length = get_n();
    const R_len_t output_length = get_nrecycle();
    if (source_length <= 0)
        return output_length;
    if (index == output_length-1-(output_length % source_length))
        return output_length;
    index += source_length;
    return index >= output_length ? (index % source_length)+1 : index;
}

void Utf8Input::initialize_records()
{
    for (R_xlen_t i = 0; i < source_size_; ++i) {
        const SEXP charsxp = elements_[i];
        if (charsxp == NA_STRING) {
            records_[static_cast<std::size_t>(i)] = missing_record();
            continue;
        }
        if (TYPEOF(charsxp) != CHARSXP)
            throw StriException("character vector contains a non-string");

        const R_len_t length = LENGTH(charsxp);
        const char* data = CHAR(charsxp);
        if (length < 0 || data == nullptr)
            throw StriException("R returned an invalid CHARSXP payload");

        switch (Rf_getCharCE(charsxp)) {
        case CE_UTF8:
            if (bom_policy_ == Utf8BomPolicy::strip &&
                    has_utf8_bom(data, length)) {
                data += 3;
                records_[static_cast<std::size_t>(i)] = Utf8Record{
                    data, length-3, Utf8RecordState::utf8
                };
            }
            else {
                records_[static_cast<std::size_t>(i)] = Utf8Record{
                    data, length, Utf8RecordState::utf8
                };
            }
            break;
        case CE_BYTES:
            throw StriException(MSG__BYTESENC);
        case CE_NATIVE:
            if (native_charsxp_is_ascii(charsxp, data, length)) {
                records_[static_cast<std::size_t>(i)] = Utf8Record{
                    data, length, Utf8RecordState::ascii
                };
            }
            else {
                records_[static_cast<std::size_t>(i)] = Utf8Record{
                    data, length, Utf8RecordState::utf8
                };
                normalize_record(i, charsxp);
            }
            break;
        case CE_LATIN1:
#if R_VERSION >= R_Version(4, 5, 0)
            if (latin1_charsxp_is_ascii(charsxp)) {
                records_[static_cast<std::size_t>(i)] = Utf8Record{
                    data, length, Utf8RecordState::ascii
                };
                break;
            }
#endif
            records_[static_cast<std::size_t>(i)] = Utf8Record{
                data, length, Utf8RecordState::utf8
            };
            normalize_record(i, charsxp);
            break;
        default:
            throw StriException("unsupported CHARSXP encoding mark");
        }
    }
}

void Utf8Input::normalize_record(R_xlen_t index, SEXP charsxp)
{
    Utf8Record& record = records_[static_cast<std::size_t>(index)];
    const bool is_latin1 = Rf_getCharCE(charsxp) == CE_LATIN1;

    try {
        // stringi strips a BOM from a CE_NATIVE string only when the native
        // encoding is UTF-8, and never from one it had to transcode
        // (CiUtf8Normalizer passes killbom=false on the conversion path).
        // Ask the converter directly rather than inferring it from the byte
        // pattern surviving conversion.
        const bool strip_bom = bom_policy_ == Utf8BomPolicy::strip &&
            !is_latin1 && converter_.native_is_utf8();

        ByteView converted = is_latin1
            ? converter_.latin1(record.ptr, record.len)
            : converter_.native(record.ptr, record.len);
        if (strip_bom && has_utf8_bom(converted.ptr, converted.len)) {
            converted.ptr += 3;
            converted.len -= 3;
        }

        const char* stable = converted.ptr;
        if (converted.len > 0) {
            char* destination = converted_.allocate(
                static_cast<std::size_t>(converted.len)
            );
            std::memcpy(
                destination, converted.ptr,
                static_cast<std::size_t>(converted.len)
            );
            stable = destination;
        }
        else {
            stable = "";
        }
        record = Utf8Record{stable, converted.len, Utf8RecordState::utf8};
        source_borrowed_[static_cast<std::size_t>(index)] = 0;
    }
    catch (const std::exception& error) {
        throw StriException("%s", error.what());
    }
}

R_xlen_t Utf8Input::source_index(R_xlen_t index) const
{
    if (index < 0 || index >= recycle_size_)
        throw StriException("UTF-8 input index out of bounds");
    if (source_size_ == 0)
        throw StriException("cannot index an empty UTF-8 input");
    return index % source_size_;
}

Utf8Record Utf8Input::record(R_xlen_t index) const
{
    return records_[static_cast<std::size_t>(source_index(index))];
}

Utf8Record Utf8Input::text(R_xlen_t index) const
{
    return record(index);
}

const Utf8Record& Utf8Input::get(R_len_t index) const
{
    const Utf8Record& value = records_[static_cast<std::size_t>(
        source_index(index)
    )];
#ifndef NDEBUG
    if (value.is_na())
        throw StriException("cannot get a missing UTF-8 record");
#endif
    return value;
}

const Utf8Record& Utf8Input::getNAble(R_len_t index) const
{
    const Utf8Record& value = records_[static_cast<std::size_t>(
        source_index(index)
    )];
    return value;
}

bool Utf8Input::is_na(R_xlen_t index) const
{
    return record(index).is_na();
}

bool Utf8Input::isNA(R_len_t index) const
{
    return is_na(index);
}

bool Utf8Input::is_borrowed(R_xlen_t index) const
{
    return source_borrowed_[static_cast<std::size_t>(source_index(index))] != 0;
}

SEXP Utf8Input::charsxp(R_xlen_t index) const
{
    const R_xlen_t source_position = source_index(index);
    const Utf8Record& value = records_[static_cast<std::size_t>(
        source_position
    )];
    if (value.is_na())
        return NA_STRING;

    SEXP original = elements_[source_position];
    if (value.ptr == CHAR(original) && value.len == LENGTH(original))
        return original;
    return Rf_mkCharLenCE(value.ptr, value.len, CE_UTF8);
}

SEXP Utf8Input::to_sexp() const
{
    SEXP output;
    PROTECT(output = Rf_allocVector(STRSXP, recycle_size_));
    for (R_xlen_t i = 0; i < recycle_size_; ++i)
        SET_STRING_ELT(output, i, charsxp(i));
    UNPROTECT(1);
    return output;
}

void Utf8Input::assert_invariants() const
{
#ifndef NDEBUG
    assert(TYPEOF(source_) == STRSXP);
    assert(source_size_ >= 0);
    assert(recycle_size_ >= 0);
    assert(records_.size() == checked_vector_size(source_size_));
    assert(source_borrowed_.size() == records_.size());
    assert(converted_.valid());
    assert((source_size_ == 0) == (elements_ == nullptr));
    for (const Utf8Record& value : records_) {
        if (value.is_na()) {
            assert(value.ptr == nullptr);
            assert(value.len == NA_INTEGER);
        }
        else {
            assert(value.ptr != nullptr);
            assert(value.len >= 0);
        }
    }
#endif
}

Utf8Workspace::Utf8Workspace(
    SEXP source, R_xlen_t recycle_size
) : input_(source, recycle_size),
    records_(checked_vector_size(input_.size())),
    changed_(checked_vector_size(input_.size()), 0), replacements_()
{
    for (R_xlen_t i = 0; i < input_.size(); ++i)
        records_[static_cast<std::size_t>(i)] = input_.record(i);
}

R_len_t Utf8Workspace::get_n() const noexcept
{
    return static_cast<R_len_t>(records_.size());
}

R_len_t Utf8Workspace::get_nrecycle() const noexcept
{
    return get_n();
}

R_len_t Utf8Workspace::vectorize_init() const noexcept
{
    return get_n() == 0 ? get_n() : 0;
}

R_len_t Utf8Workspace::vectorize_end() const noexcept
{
    return get_n();
}

R_len_t Utf8Workspace::vectorize_next(R_len_t index) const noexcept
{
    return index+1;
}

R_len_t Utf8Workspace::checked_index(R_len_t index) const
{
    if (index < 0 || index >= get_n())
        throw StriException("UTF-8 workspace index out of bounds");
    return index;
}

const Utf8Record& Utf8Workspace::get(R_len_t index) const
{
    const Utf8Record& value = records_[static_cast<std::size_t>(
        checked_index(index)
    )];
#ifndef NDEBUG
    if (value.is_na())
        throw StriException("cannot get a missing UTF-8 record");
#endif
    return value;
}

const Utf8Record& Utf8Workspace::getNAble(R_len_t index) const
{
    return records_[static_cast<std::size_t>(checked_index(index))];
}

bool Utf8Workspace::isNA(R_len_t index) const
{
    return getNAble(index).is_na();
}

void Utf8Workspace::set_na(R_len_t index)
{
    index = checked_index(index);
    records_[static_cast<std::size_t>(index)] = missing_record();
    changed_[static_cast<std::size_t>(index)] = 1;
}

void Utf8Workspace::store(
    R_len_t index, const char* data, R_len_t length
)
{
    index = checked_index(index);
    if (length < 0 || (length > 0 && data == nullptr))
        throw StriException("invalid UTF-8 workspace replacement");
    const char* stable = "";
    if (length > 0) {
        char* destination = replacements_.allocate(
            static_cast<std::size_t>(length)
        );
        std::memcpy(destination, data, static_cast<std::size_t>(length));
        stable = destination;
    }
    records_[static_cast<std::size_t>(index)] = Utf8Record{
        stable, length, Utf8RecordState::utf8
    };
    changed_[static_cast<std::size_t>(index)] = 1;
}

void Utf8Workspace::set(R_len_t index, const Utf8Record& value)
{
    if (value.is_na()) {
        set_na(index);
        return;
    }
    store(index, value.data(), value.length());
}

void Utf8Workspace::set(R_len_t index, const icu::UnicodeString& value)
{
    std::string encoded;
    value.toUTF8String(encoded);
    if (encoded.size() > static_cast<std::size_t>(R_LEN_T_MAX))
        throw StriException("UTF-8 workspace replacement is too large");
    store(index, encoded.data(), static_cast<R_len_t>(encoded.size()));
}

void Utf8Workspace::replace_all_at_pos(
    R_len_t index, R_len_t output_size,
    const char* replacement, R_len_t replacement_size,
    const std::deque<std::pair<R_len_t, R_len_t>>& occurrences
)
{
    const Utf8Record original = get(index);
    if (output_size < 0 || replacement_size < 0 ||
            (replacement_size > 0 && replacement == nullptr)) {
        throw StriException("invalid UTF-8 replacement buffer");
    }

    char* output = output_size > 0
        ? replacements_.allocate(static_cast<std::size_t>(output_size))
        : nullptr;
    R_len_t used = 0;
    R_len_t previous = 0;
    for (const auto& occurrence : occurrences) {
        const R_len_t prefix = occurrence.first-previous;
        if (prefix > 0)
            std::memcpy(output+used, original.ptr+previous, prefix);
        used += prefix;
        if (replacement_size > 0)
            std::memcpy(output+used, replacement, replacement_size);
        used += replacement_size;
        previous = occurrence.second;
    }
    const R_len_t suffix = original.len-previous;
    if (suffix > 0)
        std::memcpy(output+used, original.ptr+previous, suffix);
    used += suffix;
    if (used != output_size)
        throw StriException("replacement size mismatch");

    records_[static_cast<std::size_t>(checked_index(index))] = Utf8Record{
        output_size > 0 ? output : "", output_size, Utf8RecordState::utf8
    };
    changed_[static_cast<std::size_t>(index)] = 1;
}

SEXP Utf8Workspace::to_sexp() const
{
    SEXP output;
    PROTECT(output = Rf_allocVector(STRSXP, get_n()));
    for (R_len_t i = 0; i < get_n(); ++i) {
        const Utf8Record& value = records_[static_cast<std::size_t>(i)];
        if (value.is_na()) {
            SET_STRING_ELT(output, i, NA_STRING);
        }
        else if (!changed_[static_cast<std::size_t>(i)]) {
            SET_STRING_ELT(output, i, input_.charsxp(i));
        }
        else {
            SET_STRING_ELT(
                output, i, Rf_mkCharLenCE(value.ptr, value.len, CE_UTF8)
            );
        }
    }
    UNPROTECT(1);
    return output;
}

IndexedUtf8Input::IndexedUtf8Input(
    SEXP source, R_xlen_t recycle_size, Utf8BomPolicy bom_policy
) : Utf8Input(source, recycle_size, bom_policy), last_fwd_codepoint_(0),
    last_fwd_utf8_(0), last_fwd_record_(nullptr),
    last_back_codepoint_(0), last_back_utf8_(0),
    last_back_record_(nullptr)
{
}

} // namespace base
} // namespace charr
