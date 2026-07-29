#include "utf8_input.h"

#include "ci_messages.h"
#include "ci_reader.h"
#include "ci_stringi.h"

#include <unicode/utf8.h>
#include <unicode/uchar.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace charr {
namespace altrep_backend {
namespace io {

namespace utf8_input {

const char empty_record[] = "";

bool has_utf8_bom(const char* data, R_len_t length) noexcept
{
    return length >= 3 &&
        static_cast<unsigned char>(data[0]) == 0xef &&
        static_cast<unsigned char>(data[1]) == 0xbb &&
        static_cast<unsigned char>(data[2]) == 0xbf;
}

R_xlen_t checked_recycle_size(R_xlen_t source_size, R_xlen_t recycle_size)
{
    if (source_size < 0)
        throw std::logic_error("negative Reader length");
    if (recycle_size < 0)
        throw std::invalid_argument("negative recycle length");
    if (source_size > R_LEN_T_MAX || recycle_size > R_LEN_T_MAX)
        throw std::length_error("long UTF-8 inputs are not supported");
    return source_size == 0 || recycle_size == 0 ? 0 : recycle_size;
}

} // namespace utf8_input

using namespace utf8_input;

Utf8Record::Utf8Record() noexcept
    : data_(nullptr), length_(0), state_(Utf8RecordState::missing)
{
}

Utf8Record::Utf8Record(
    const char* data, R_len_t length, Utf8RecordState state
) noexcept : data_(data), length_(length), state_(state)
{
}

bool Utf8Record::isASCII() const noexcept
{
    if (state_ == Utf8RecordState::ascii_or_utf8) {
        state_ = ci::is_ascii(data_, static_cast<std::size_t>(length_))
            ? Utf8RecordState::ascii
            : Utf8RecordState::utf8;
    }
    return state_ == Utf8RecordState::ascii;
}
bool Utf8Record::isUTF8() const noexcept
{
    if (state_ == Utf8RecordState::ascii_or_utf8)
        isASCII();
    return state_ == Utf8RecordState::utf8;
}
bool Utf8Record::isBytes() const noexcept { return state_ == Utf8RecordState::bytes; }

const char* Utf8Record::data() const
{
    if (isNA())
        throw StriException("missing UTF-8 record has no payload");
    return data_;
}

R_len_t Utf8Record::length() const
{
    if (isNA())
        throw StriException("missing UTF-8 record has no length");
    return length_;
}

R_len_t Utf8Record::countCodePoints() const
{
    if (isNA())
        throw StriException("cannot count a missing UTF-8 record");
    return isASCII() ? length_ : ci__length_string(data_, length_);
}

bool Utf8Record::endsWith(
    R_len_t byteindex, const char* pattern, R_len_t pattern_length,
    bool case_insensitive
) const
{
    if (case_insensitive) {
        R_len_t pattern_index = pattern_length;
        UChar32 source_codepoint;
        UChar32 pattern_codepoint;
        while (pattern_index > 0) {
            if (byteindex <= 0)
                return false;
            U8_PREV(data_, 0, byteindex, source_codepoint);
            U8_PREV(pattern, 0, pattern_index, pattern_codepoint);
            if (u_toupper(source_codepoint) != u_toupper(pattern_codepoint))
                return false;
        }
        return true;
    }
    return byteindex >= pattern_length &&
        std::memcmp(
            data_ + byteindex - pattern_length,
            pattern, static_cast<std::size_t>(pattern_length)
        ) == 0;
}

bool Utf8Record::startsWith(
    R_len_t byteindex, const char* pattern, R_len_t pattern_length,
    bool case_insensitive
) const
{
    if (case_insensitive) {
        R_len_t pattern_index = 0;
        UChar32 source_codepoint;
        UChar32 pattern_codepoint;
        while (pattern_index < pattern_length) {
            if (byteindex >= length_)
                return false;
            U8_NEXT(data_, byteindex, length_, source_codepoint);
            U8_NEXT(pattern, pattern_index, pattern_length, pattern_codepoint);
            if (u_toupper(source_codepoint) != u_toupper(pattern_codepoint))
                return false;
        }
        return true;
    }
    return byteindex <= length_ - pattern_length &&
        std::memcmp(
            data_ + byteindex, pattern,
            static_cast<std::size_t>(pattern_length)
        ) == 0;
}

const char* ByteView::data() const
{
    if (missing_)
        throw StriException("missing byte view has no payload");
    return data_;
}

R_len_t ByteView::length() const
{
    if (missing_)
        throw StriException("missing byte view has no length");
    return length_;
}

struct Utf8Input::Storage {
    std::shared_ptr<ci::ReaderBorrow> borrow;
    std::vector<Utf8Record> records;
    shared::SliceArena converted;
    shared::NativeToUtf8 converter;

    explicit Storage(const std::shared_ptr<ci::ReaderBorrow>& source)
        : borrow(source), records(), converted(), converter()
    {
    }

    Utf8Record normalize(
        const charport::StrView& source, Utf8BomPolicy bom_policy
    )
    {
        if (source.is_na())
            return Utf8Record();
        if (source.ptr == nullptr || source.len < 0)
            throw std::runtime_error("Reader returned an invalid string view");
        if (source.enc == cetype_ext_t::CE_BYTES)
            throw StriException(MSG__BYTESENC);

        const char* data = source.ptr;
        R_len_t length = source.len;
        Utf8RecordState state = Utf8RecordState::utf8;
        bool strip_bom = false;

        switch (source.enc) {
        case cetype_ext_t::CE_ASCII:
            state = Utf8RecordState::ascii;
            break;
        case cetype_ext_t::CE_UTF8:
            strip_bom = bom_policy == Utf8BomPolicy::strip &&
                has_utf8_bom(data, length);
            break;
        case cetype_ext_t::CE_ASCII_OR_UTF8: {
            strip_bom = bom_policy == Utf8BomPolicy::strip &&
                has_utf8_bom(data, length);
            state = strip_bom
                ? Utf8RecordState::utf8
                : Utf8RecordState::ascii_or_utf8;
            break;
        }
        case cetype_ext_t::CE_LATIN1:
        case cetype_ext_t::CE_NATIVE: {
            // stringi strips a BOM from a CE_NATIVE string only when the
            // native encoding is UTF-8, and never from one it had to transcode
            // (CiUtf8Normalizer passes killbom=false on the conversion path).
            // Ask the converter directly rather than inferring it from the
            // byte pattern surviving conversion.
            const bool native_bom =
                bom_policy == Utf8BomPolicy::strip &&
                source.enc == cetype_ext_t::CE_NATIVE &&
                converter.native_is_utf8();
            const shared::ByteView bytes =
                source.enc == cetype_ext_t::CE_LATIN1
                    ? converter.latin1(source.ptr, source.len)
                    : converter.native(source.ptr, source.len);
            if (bytes.len < 0 || (bytes.ptr == nullptr && bytes.len > 0))
                throw std::runtime_error("encoding conversion returned invalid bytes");
            length = bytes.len;
            data = empty_record;
            if (length > 0) {
                char* destination = converted.allocate(
                    static_cast<std::size_t>(length)
                );
                std::memcpy(
                    destination, bytes.ptr,
                    static_cast<std::size_t>(length)
                );
                data = destination;
            }
            strip_bom = native_bom && has_utf8_bom(data, length);
            break;
        }
        case cetype_ext_t::CE_NA:
            throw std::logic_error("non-missing Reader record has NA encoding");
        default:
            throw std::runtime_error("Reader returned an unknown string encoding");
        }

        if (strip_bom) {
            data += 3;
            length -= 3;
        }
        if (length == 0 && data == nullptr)
            data = empty_record;
        return Utf8Record(data, length, state);
    }
};

Utf8Input::Utf8Input() noexcept
    : source_size_(0), recycle_size_(0), storage_()
{
}

Utf8Input::Utf8Input(
    ci::ReaderContext& context, SEXP source, R_xlen_t recycle_size,
    bool, Utf8BomPolicy bom_policy
) : Utf8Input()
{
    // A zero recycled result never observes this input. Avoid opening a
    // foreign Reader (and therefore avoid validating otherwise unreachable
    // records) just as stringi's empty-vector paths do.
    if (recycle_size <= 0) {
        checked_recycle_size(0, recycle_size);
        return;
    }
    *this = Utf8Input(context.acquire(source), recycle_size, bom_policy);
}

Utf8Input::Utf8Input(
    const std::shared_ptr<ci::ReaderBorrow>& borrow, R_xlen_t recycle_size,
    Utf8BomPolicy bom_policy
) : source_size_(borrow ? borrow->size() : 0),
    recycle_size_(checked_recycle_size(source_size_, recycle_size)),
    storage_(std::make_shared<Storage>(borrow))
{
    if (!borrow)
        throw std::invalid_argument("UTF-8 input requires a Reader borrow");
    const charport::StrViews& views = borrow->views();
    if (views.size() != source_size_)
        throw std::logic_error("Reader view length changed during input setup");
    storage_->records.reserve(static_cast<std::size_t>(source_size_));
    for (R_xlen_t i = 0; i < source_size_; ++i)
        storage_->records.push_back(storage_->normalize(views[i], bom_policy));
}

Utf8Input::Utf8Input(
    const std::shared_ptr<ci::ReaderBorrow>& borrow,
    const charport::StrView& value, R_xlen_t recycle_size, bool,
    Utf8BomPolicy bom_policy
) : source_size_(1),
    recycle_size_(checked_recycle_size(1, recycle_size)),
    storage_(std::make_shared<Storage>(borrow))
{
    if (!borrow)
        throw std::invalid_argument("UTF-8 input requires a Reader borrow");
    storage_->records.push_back(storage_->normalize(value, bom_policy));
}

R_xlen_t Utf8Input::source_size() const noexcept { return source_size_; }
R_xlen_t Utf8Input::size() const noexcept { return recycle_size_; }

const Utf8Record* Utf8Input::source_data() const noexcept
{
    if (!storage_ || storage_->records.empty())
        return nullptr;
    return storage_->records.data();
}

R_len_t Utf8Input::get_n() const noexcept
{
    return static_cast<R_len_t>(source_size_);
}

R_len_t Utf8Input::get_nrecycle() const noexcept
{
    return static_cast<R_len_t>(recycle_size_);
}

void Utf8Input::set_nrecycle(R_len_t value)
{
    recycle_size_ = checked_recycle_size(source_size_, value);
}

R_len_t Utf8Input::vectorize_init() const noexcept
{
    return source_size_ <= 0 ? static_cast<R_len_t>(recycle_size_) : 0;
}

R_len_t Utf8Input::vectorize_end() const noexcept
{
    return static_cast<R_len_t>(recycle_size_);
}

R_len_t Utf8Input::vectorize_next(R_len_t index) const noexcept
{
    const R_len_t n = static_cast<R_len_t>(source_size_);
    const R_len_t recycled = static_cast<R_len_t>(recycle_size_);
    if (n <= 0)
        return recycled;
    if (index == recycled - 1 - (recycled % n))
        return recycled;
    index += n;
    return index >= recycled ? (index % n) + 1 : index;
}

R_xlen_t Utf8Input::source_index(R_xlen_t index) const
{
    if (index < 0 || index >= recycle_size_)
        throw std::out_of_range("UTF-8 input index out of bounds");
    if (source_size_ == 0 || !storage_)
        throw std::logic_error("cannot index an empty UTF-8 input");
    return index % source_size_;
}

const Utf8Record& Utf8Input::record(R_xlen_t index) const
{
    return storage_->records[static_cast<std::size_t>(source_index(index))];
}

charport::StrView Utf8Input::text(R_xlen_t index) const
{
    const Utf8Record& value = record(index);
    if (value.isBytes())
        throw StriException(MSG__BYTESENC);
    return value.view();
}

bool Utf8Input::is_na(R_xlen_t index) const { return record(index).isNA(); }
bool Utf8Input::is_bytes(R_xlen_t index) const { return record(index).isBytes(); }

const Utf8Record& Utf8Input::get(R_len_t index) const
{
    const Utf8Record& value = record(index);
    if (value.isNA())
        throw StriException("cannot get a missing UTF-8 record");
    return value;
}

const Utf8Record& Utf8Input::getNAble(R_len_t index) const
{
    return record(index);
}

R_len_t Utf8Input::getMaxNumBytes() const
{
    R_len_t result = 0;
    for (R_xlen_t i = 0; i < source_size_; ++i) {
        const Utf8Record& value = storage_->records[static_cast<std::size_t>(i)];
        if (!value.isNA())
            result = std::max(result, value.length());
    }
    return result;
}

R_len_t Utf8Input::getMaxLength() const
{
    R_len_t result = 0;
    for (R_xlen_t i = 0; i < source_size_; ++i) {
        const Utf8Record& value = storage_->records[static_cast<std::size_t>(i)];
        if (!value.isNA())
            result = std::max(result, value.countCodePoints());
    }
    return result;
}

Utf8Workspace::Utf8Workspace(
    ci::ReaderContext& context, SEXP source, R_xlen_t recycle_size
) : input_(context, source, recycle_size), records_(), replacements_()
{
    records_.reserve(static_cast<std::size_t>(input_.size()));
    for (R_xlen_t i = 0; i < input_.size(); ++i)
        records_.push_back(input_.record(i));
}

R_len_t Utf8Workspace::get_n() const noexcept
{
    return static_cast<R_len_t>(records_.size());
}

R_len_t Utf8Workspace::get_nrecycle() const noexcept { return get_n(); }

R_len_t Utf8Workspace::checked_index(R_len_t index) const
{
    if (index < 0 || static_cast<std::size_t>(index) >= records_.size())
        throw std::out_of_range("UTF-8 workspace index out of bounds");
    return index;
}

bool Utf8Workspace::isNA(R_len_t index) const
{
    return records_[static_cast<std::size_t>(checked_index(index))].isNA();
}

const Utf8Record& Utf8Workspace::get(R_len_t index) const
{
    const Utf8Record& value = getNAble(index);
    if (value.isNA())
        throw StriException("cannot get a missing UTF-8 workspace record");
    return value;
}

const Utf8Record& Utf8Workspace::getNAble(R_len_t index) const
{
    return records_[static_cast<std::size_t>(checked_index(index))];
}

void Utf8Workspace::setNA(R_len_t index)
{
    records_[static_cast<std::size_t>(checked_index(index))] = Utf8Record();
}

Utf8Record Utf8Workspace::copy_record(const Utf8Record& value)
{
    if (value.isNA())
        return Utf8Record();
    if (value.length() == 0)
        return Utf8Record(empty_record, 0, value.state());
    char* destination = replacements_.allocate(
        static_cast<std::size_t>(value.length())
    );
    std::memcpy(
        destination, value.data(), static_cast<std::size_t>(value.length())
    );
    return Utf8Record(
        destination, value.length(), value.state()
    );
}

void Utf8Workspace::set(R_len_t index, const Utf8Record& value)
{
    records_[static_cast<std::size_t>(checked_index(index))] =
        copy_record(value);
}

void Utf8Workspace::replaceAllAtPos(
    R_len_t index, R_len_t output_size,
    const char* replacement, R_len_t replacement_length,
    std::deque<std::pair<R_len_t, R_len_t> >& occurrences
)
{
    if (output_size < 0 || replacement_length < 0 ||
            (replacement_length > 0 && replacement == nullptr)) {
        throw std::invalid_argument("invalid UTF-8 replacement buffer");
    }
    const Utf8Record source = get(index);
    char* output = output_size > 0
        ? replacements_.allocate(static_cast<std::size_t>(output_size))
        : nullptr;
    R_len_t used = 0;
    R_len_t previous = 0;
    for (const std::pair<R_len_t, R_len_t>& match : occurrences) {
        const R_len_t prefix = match.first - previous;
        if (prefix > 0)
            std::memcpy(output + used, source.data() + previous, prefix);
        used += prefix;
        if (replacement_length > 0)
            std::memcpy(output + used, replacement, replacement_length);
        used += replacement_length;
        previous = match.second;
    }
    const R_len_t suffix = source.length() - previous;
    if (suffix > 0)
        std::memcpy(output + used, source.data() + previous, suffix);
    used += suffix;
    if (used != output_size)
        throw std::logic_error("replacement output size mismatch");
    const Utf8RecordState output_state = source.isASCII() &&
        ci::is_ascii(
            replacement, static_cast<std::size_t>(replacement_length)
        ) ? Utf8RecordState::ascii : Utf8RecordState::utf8;
    records_[static_cast<std::size_t>(checked_index(index))] = Utf8Record(
        output_size == 0 ? empty_record : output, output_size,
        output_state
    );
}

} // namespace io
} // namespace altrep_backend
} // namespace charr
