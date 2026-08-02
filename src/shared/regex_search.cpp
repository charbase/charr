// Derived from stringi.
// Copyright (c) 2013-2025, Marek Gagolewski. See inst/COPYRIGHTS.

#include "regex_search.h"
#include "utf8.h"

#include <unicode/ustring.h>
#include <unicode/utext.h>
#include <unicode/utf16.h>

#include <new>
#include <limits>
#include <stdexcept>

#include <unicode/utf8.h>

namespace charr {
namespace shared {

void regex_range_to_positions(
    const StringView& subject,
    RegexRange& range,
    bool return_length
) noexcept
{
    if (range.start < 0 || range.end < 0)
        return;

    Utf8PositionCursor positions(subject);
    const int start = positions.at_byte(range.start)+1;
    const int end = positions.at_byte(range.end);
    range = RegexRange{
        start,
        return_length ? end-start+1 : end
    };
}


void regex_ranges_to_positions(
    const StringView& subject,
    std::vector<RegexRange>& ranges,
    bool return_length
) noexcept
{
    Utf8PositionCursor positions(subject);
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        RegexRange& range = ranges[i];
        if (range.start < 0 || range.end < 0)
            continue;
        const int start = positions.at_byte(range.start)+1;
        const int end = positions.at_byte(range.end);
        range = RegexRange{
            start,
            return_length ? end-start+1 : end
        };
    }
}

namespace regex_search {

#if defined(__GNUC__) || defined(__clang__)
#define CHARR_REGEX_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define CHARR_REGEX_ALWAYS_INLINE inline
#endif

CHARR_CXX_HELPER CHARR_REGEX_ALWAYS_INLINE void set_utf16(
    icu::UnicodeString& output, const StringView& source
)
{
    if (source.len < 0 || (source.ptr == nullptr && source.len != 0))
        throw std::invalid_argument("invalid regex input string view");
    if (source.enc != StringEncoding::ascii &&
            source.enc != StringEncoding::utf8 &&
            source.enc != StringEncoding::ascii_or_utf8) {
        throw std::invalid_argument(
            "regex input must be normalized to UTF-8"
        );
    }

    if (source.len == 0) {
        output.remove();
        return;
    }

    UChar* destination = output.getBuffer(source.len);
    if (destination == nullptr)
        throw std::bad_alloc();

    int length = 0;
    if (source.enc == StringEncoding::ascii) {
        for (int i = 0; i < source.len; ++i) {
            destination[i] = static_cast<unsigned char>(source.ptr[i]);
        }
        length = source.len;
    }
    else {
        UErrorCode status = U_ZERO_ERROR;
        u_strFromUTF8WithSub(
            destination, source.len, &length,
            source.ptr, source.len, 0xfffd, nullptr, &status
        );
        output.releaseBuffer(length);
        if (U_FAILURE(status))
            throw std::runtime_error("regex UTF-8 conversion failed");
        return;
    }

    output.releaseBuffer(length);
}


CHARR_NEUTRAL_HELPER bool is_ascii(
    const StringView& source
) noexcept
{
    if (source.enc == StringEncoding::ascii)
        return true;
    if (source.enc != StringEncoding::ascii_or_utf8)
        return false;
    for (int i = 0; i < source.len; ++i) {
        if (static_cast<unsigned char>(source.ptr[i]) > 0x7fU)
            return false;
    }
    return true;
}


CHARR_CXX_HELPER void set_utf16_with_offsets(
    icu::UnicodeString& output,
    std::vector<int>& byte_offsets,
    bool& ascii,
    const StringView& source
)
{
    if (source.len < 0 || (source.ptr == nullptr && source.len != 0))
        throw std::invalid_argument("invalid regex input string view");
    if (source.enc != StringEncoding::ascii &&
            source.enc != StringEncoding::utf8 &&
            source.enc != StringEncoding::ascii_or_utf8) {
        throw std::invalid_argument(
            "regex input must be normalized to UTF-8"
        );
    }

    ascii = is_ascii(source);
    if (source.len == 0) {
        output.remove();
        byte_offsets.clear();
        byte_offsets.push_back(0);
        return;
    }

    if (!ascii) {
        byte_offsets.resize(static_cast<std::size_t>(source.len)+1);
        byte_offsets[0] = 0;
    }

    UChar* destination = output.getBuffer(source.len);
    if (destination == nullptr)
        throw std::bad_alloc();

    if (ascii) {
        for (int i = 0; i < source.len; ++i)
            destination[i] = static_cast<unsigned char>(source.ptr[i]);
        output.releaseBuffer(source.len);
        byte_offsets.clear();
        return;
    }

    int source_index = 0;
    int output_index = 0;
    while (source_index < source.len) {
        const int source_begin = source_index;
        UChar32 code_point;
        U8_NEXT_OR_FFFD(
            source.ptr, source_index, source.len, code_point
        );
        if (code_point <= 0xffff) {
            destination[output_index++] = static_cast<UChar>(code_point);
            byte_offsets[static_cast<std::size_t>(output_index)] =
                source_index;
        }
        else {
            destination[output_index++] = U16_LEAD(code_point);
            byte_offsets[static_cast<std::size_t>(output_index)] =
                source_begin;
            destination[output_index++] = U16_TRAIL(code_point);
            byte_offsets[static_cast<std::size_t>(output_index)] =
                source_index;
        }
    }
    output.releaseBuffer(output_index);
    byte_offsets.resize(static_cast<std::size_t>(output_index)+1);
}

#undef CHARR_REGEX_ALWAYS_INLINE

} // namespace regex_search


void set_regex_utf16(
    icu::UnicodeString& output,
    const StringView& source
)
{
    regex_search::set_utf16(output, source);
}


RegexPatterns::RegexPatterns() noexcept : values_(), missing_()
{
}


void RegexPatterns::resize(std::size_t size)
{
    values_.resize(size);
    missing_.resize(size);
}


void RegexPatterns::set(
    std::size_t index, const StringView& source
)
{
    if (index >= values_.size() || index >= missing_.size())
        throw std::out_of_range("regex pattern index out of bounds");

    if (source.is_na()) {
        missing_[index] = 1;
        return;
    }

    regex_search::set_utf16(values_[index], source);
    missing_[index] = 0;
}


std::size_t RegexPatterns::size() const noexcept
{
    return values_.size();
}


RegexInput RegexPatterns::get(std::size_t index) const
{
    if (index >= values_.size() || index >= missing_.size())
        throw std::out_of_range("regex pattern index out of bounds");

    const icu::UnicodeString& value = values_[index];
    return RegexInput{
        &value,
        &value,
        value.length(),
        missing_[index] != 0
    };
}


int RegexPatterns::empty_count() const noexcept
{
    int result = 0;
    for (std::size_t i = 0; i < values_.size(); ++i) {
        if (missing_[i] == 0 && values_[i].length() <= 0)
            ++result;
    }
    return result;
}


void RegexPatterns::context(
    std::size_t index, std::string& output
) const
{
    if (index >= values_.size() || index >= missing_.size())
        throw std::out_of_range("regex pattern index out of bounds");

    output.clear();
    if (missing_[index] == 0)
        values_[index].toUTF8String(output);
}


RegexMatcher::RegexMatcher(RegexOptions options) noexcept
    : matcher_(nullptr), pattern_identity_(nullptr), options_(options),
      subject_(), subject_identity_(nullptr), byte_offsets_(),
      subject_ascii_(true), byte_offsets_valid_(false)
{
}


RegexMatcher::~RegexMatcher() noexcept
{
    close();
}


void RegexMatcher::close() noexcept
{
    delete matcher_;
    matcher_ = nullptr;
    pattern_identity_ = nullptr;
}


void RegexMatcher::reset_options(RegexOptions options) noexcept
{
    close();
    options_ = options;
    subject_identity_ = nullptr;
    byte_offsets_valid_ = false;
}


bool RegexMatcher::bind(
    const RegexInput& pattern,
    UErrorCode& status,
    bool& pattern_compile_error
) noexcept {
    status = U_ZERO_ERROR;
    pattern_compile_error = false;
    if (pattern.missing || pattern.length <= 0 ||
            pattern.value == nullptr || pattern.identity == nullptr) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return false;
    }

    if (matcher_ == nullptr || pattern_identity_ != pattern.identity) {
        close();
        matcher_ = new icu::RegexMatcher(
            *pattern.value, options_.flags, status
        );
        if (U_FAILURE(status) || matcher_ == nullptr) {
            if (U_SUCCESS(status))
                status = U_MEMORY_ALLOCATION_ERROR;
            pattern_compile_error = true;
            close();
            return false;
        }

        if (options_.stack_limit > 0) {
            matcher_->setStackLimit(options_.stack_limit, status);
            if (U_FAILURE(status)) {
                close();
                return false;
            }
        }
        if (options_.time_limit > 0) {
            matcher_->setTimeLimit(options_.time_limit, status);
            if (U_FAILURE(status)) {
                close();
                return false;
            }
        }
        pattern_identity_ = pattern.identity;
    }
    return true;
}


bool RegexMatcher::contains(
    const StringView& subject,
    const void* subject_identity,
    UErrorCode& status
) {
    status = U_ZERO_ERROR;
    if (subject.is_na() || subject_identity == nullptr ||
            matcher_ == nullptr) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return false;
    }
    if (subject_identity_ != subject_identity) {
        subject_identity_ = nullptr;
        regex_search::set_utf16(subject_, subject);
        subject_identity_ = subject_identity;
        byte_offsets_valid_ = false;
    }

    matcher_->reset(subject_);
    const UBool found = matcher_->find(status);
    if (U_FAILURE(status))
        close();
    return found != 0;
}


int RegexMatcher::count(
    const StringView& subject,
    const void* subject_identity,
    UErrorCode& status
) {
    status = U_ZERO_ERROR;
    if (subject.is_na() || subject_identity == nullptr ||
            matcher_ == nullptr) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    if (subject_identity_ != subject_identity) {
        subject_identity_ = nullptr;
        regex_search::set_utf16(subject_, subject);
        subject_identity_ = subject_identity;
        byte_offsets_valid_ = false;
    }

    matcher_->reset(subject_);
    int result = 0;
    UBool found = matcher_->find(status);
    while (found != 0 && U_SUCCESS(status)) {
        ++result;
        found = matcher_->find(status);
    }
    if (U_FAILURE(status))
        close();
    return result;
}


bool RegexMatcher::find_first(
    const StringView& subject,
    const void* subject_identity,
    RegexRange& match,
    UErrorCode& status
)
{
    status = U_ZERO_ERROR;
    if (subject.is_na() || subject_identity == nullptr || matcher_ == nullptr) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return false;
    }

    if (subject_identity_ != subject_identity || !byte_offsets_valid_) {
        subject_identity_ = nullptr;
        byte_offsets_valid_ = false;
        regex_search::set_utf16_with_offsets(
            subject_, byte_offsets_, subject_ascii_, subject
        );
        subject_identity_ = subject_identity;
        byte_offsets_valid_ = true;
    }

    matcher_->reset(subject_);
    const UBool found = matcher_->find(status);
    if (found == 0 || U_FAILURE(status)) {
        if (U_FAILURE(status))
            close();
        return false;
    }

    const int start_utf16 = matcher_->start(status);
    if (U_FAILURE(status)) {
        close();
        return false;
    }
    const int start = byte_offset(start_utf16, status);
    if (U_FAILURE(status)) {
        close();
        return false;
    }
    const int end_utf16 = matcher_->end(status);
    if (U_FAILURE(status)) {
        close();
        return false;
    }
    const int end = byte_offset(end_utf16, status);
    if (U_FAILURE(status)) {
        close();
        return false;
    }

    match = RegexRange{start, end};
    return true;
}


void RegexMatcher::find_all(
    const StringView& subject,
    const void* subject_identity,
    std::vector<RegexRange>& matches,
    UErrorCode& status
)
{
    matches.clear();
    status = U_ZERO_ERROR;
    if (subject.is_na() || subject_identity == nullptr || matcher_ == nullptr) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    if (subject_identity_ != subject_identity || !byte_offsets_valid_) {
        subject_identity_ = nullptr;
        byte_offsets_valid_ = false;
        regex_search::set_utf16_with_offsets(
            subject_, byte_offsets_, subject_ascii_, subject
        );
        subject_identity_ = subject_identity;
        byte_offsets_valid_ = true;
    }

    matcher_->reset(subject_);
    for (;;) {
        const UBool found = matcher_->find(status);
        if (found == 0 || U_FAILURE(status))
            break;

        const int start_utf16 = matcher_->start(status);
        if (U_FAILURE(status))
            break;
        const int start = byte_offset(start_utf16, status);
        if (U_FAILURE(status))
            break;
        const int end_utf16 = matcher_->end(status);
        if (U_FAILURE(status))
            break;
        const int end = byte_offset(end_utf16, status);
        if (U_FAILURE(status))
            break;
        matches.push_back(RegexRange{start, end});
    }

    if (U_FAILURE(status))
        close();
}


bool RegexMatcher::find_first_with_captures(
    const StringView& subject,
    const void* subject_identity,
    RegexRange& match,
    std::vector<RegexRange>& captures,
    UErrorCode& status
)
{
    captures.clear();
    const bool found = find_first(
        subject, subject_identity, match, status
    );
    if (!found || U_FAILURE(status))
        return false;

    const int count = group_count();
    captures.resize(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const int start_utf16 = matcher_->start(i+1, status);
        if (U_FAILURE(status))
            break;
        const int end_utf16 = matcher_->end(i+1, status);
        if (U_FAILURE(status))
            break;
        if (start_utf16 < 0 || end_utf16 < 0) {
            captures[static_cast<std::size_t>(i)] = RegexRange{-1, -1};
            continue;
        }

        const int start = byte_offset(start_utf16, status);
        if (U_FAILURE(status))
            break;
        const int end = byte_offset(end_utf16, status);
        if (U_FAILURE(status))
            break;
        captures[static_cast<std::size_t>(i)] = RegexRange{start, end};
    }

    if (U_FAILURE(status)) {
        close();
        return false;
    }
    return true;
}


void RegexMatcher::find_all_with_captures(
    const StringView& subject,
    const void* subject_identity,
    std::vector<RegexRange>& matches,
    std::vector<std::vector<RegexRange> >& captures,
    UErrorCode& status
)
{
    matches.clear();
    captures.clear();
    status = U_ZERO_ERROR;
    if (subject.is_na() || subject_identity == nullptr || matcher_ == nullptr) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    if (subject_identity_ != subject_identity || !byte_offsets_valid_) {
        subject_identity_ = nullptr;
        byte_offsets_valid_ = false;
        regex_search::set_utf16_with_offsets(
            subject_, byte_offsets_, subject_ascii_, subject
        );
        subject_identity_ = subject_identity;
        byte_offsets_valid_ = true;
    }

    const int count = group_count();
    captures.resize(static_cast<std::size_t>(count));
    matcher_->reset(subject_);
    for (;;) {
        const UBool found = matcher_->find(status);
        if (found == 0 || U_FAILURE(status))
            break;

        const int start_utf16 = matcher_->start(status);
        if (U_FAILURE(status))
            break;
        const int start = byte_offset(start_utf16, status);
        if (U_FAILURE(status))
            break;
        const int end_utf16 = matcher_->end(status);
        if (U_FAILURE(status))
            break;
        const int end = byte_offset(end_utf16, status);
        if (U_FAILURE(status))
            break;
        matches.push_back(RegexRange{start, end});

        for (int i = 0; i < count; ++i) {
            const int capture_start_utf16 = matcher_->start(i+1, status);
            if (U_FAILURE(status))
                break;
            const int capture_end_utf16 = matcher_->end(i+1, status);
            if (U_FAILURE(status))
                break;

            RegexRange capture{-1, -1};
            if (capture_start_utf16 >= 0 && capture_end_utf16 >= 0) {
                capture.start = byte_offset(
                    capture_start_utf16, status
                );
                if (U_FAILURE(status))
                    break;
                capture.end = byte_offset(capture_end_utf16, status);
                if (U_FAILURE(status))
                    break;
            }
            captures[static_cast<std::size_t>(i)].push_back(capture);
        }
        if (U_FAILURE(status))
            break;
    }

    if (U_FAILURE(status))
        close();
}


RegexReplaceResult RegexMatcher::replace(
    const icu::UnicodeString& subject,
    const icu::UnicodeString* replacement,
    RegexReplaceMode mode,
    icu::UnicodeString& output,
    UErrorCode& status
)
{
    status = U_ZERO_ERROR;
    if (matcher_ == nullptr) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return RegexReplaceResult::value;
    }

    matcher_->reset(subject);
    if (replacement == nullptr) {
        const UBool found = matcher_->find(status);
        if (U_FAILURE(status)) {
            close();
            return RegexReplaceResult::value;
        }
        if (found != 0)
            return RegexReplaceResult::missing;
        output.setTo(subject);
        return RegexReplaceResult::value;
    }

    if (mode == RegexReplaceMode::all) {
        output = matcher_->replaceAll(*replacement, status);
        if (U_FAILURE(status))
            close();
        return RegexReplaceResult::value;
    }

    const UBool found = matcher_->find(status);
    if (U_FAILURE(status)) {
        close();
        return RegexReplaceResult::value;
    }
    if (found == 0) {
        output.setTo(subject);
        return RegexReplaceResult::value;
    }

    output.remove();
    matcher_->appendReplacement(output, *replacement, status);
    if (U_FAILURE(status)) {
        close();
        return RegexReplaceResult::value;
    }
    matcher_->appendTail(output);
    return RegexReplaceResult::value;
}


RegexReplaceResult RegexMatcher::replace(
    const StringView& subject,
    const void* subject_identity,
    const icu::UnicodeString* replacement,
    RegexReplaceMode mode,
    icu::UnicodeString& output,
    UErrorCode& status
)
{
    status = U_ZERO_ERROR;
    if (subject.is_na() || subject_identity == nullptr) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return RegexReplaceResult::value;
    }

    if (subject_identity_ != subject_identity) {
        subject_identity_ = nullptr;
        regex_search::set_utf16(subject_, subject);
        subject_identity_ = subject_identity;
        byte_offsets_valid_ = false;
    }

    return replace(subject_, replacement, mode, output, status);
}


int RegexMatcher::group_count() const noexcept
{
    return matcher_ == nullptr ? 0 : matcher_->groupCount();
}


void RegexMatcher::capture_names(
    std::vector<std::string>& names,
    UErrorCode& status
) const
{
    status = U_ZERO_ERROR;
    if (matcher_ == nullptr) {
        names.clear();
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    const int count = group_count();
    names.assign(static_cast<std::size_t>(count), std::string());
    if (count <= 0)
        return;

#if U_ICU_VERSION_MAJOR_NUM >= 55
    UText* text = matcher_->pattern().patternText(status);
    if (U_FAILURE(status))
        return;

    UChar32 current = utext_next32From(text, 0);
    while (current >= 0) {
        if (current == '\\') {
            current = utext_next32(text);
            current = utext_next32(text);
        }
        else if (current == '[') {
            for (current = utext_next32(text);
                    current >= 0 && current != ']';
                    current = utext_next32(text)) {
                if (current == '\\')
                    current = utext_next32(text);
            }
            current = utext_next32(text);
        }
        else if (current == '(') {
            current = utext_next32(text);
            if (current != '?') {
                current = utext_next32(text);
                continue;
            }
            current = utext_next32(text);
            if (current != '<') {
                current = utext_next32(text);
                continue;
            }

            std::string name;
            for (current = utext_next32(text);
                    (current >= 'A' && current <= 'Z') ||
                    (current >= 'a' && current <= 'z') ||
                    (current >= '0' && current <= '9');
                    current = utext_next32(text)) {
                name.push_back(static_cast<char>(current));
            }
            if (current == '>') {
                UErrorCode name_status = U_ZERO_ERROR;
                int group = matcher_->pattern().groupNumberFromName(
                    name.data(), static_cast<int>(name.size()), name_status
                );
                if (U_SUCCESS(name_status)) {
                    --group;
                    if (group >= 0 && group < count)
                        names[static_cast<std::size_t>(group)] = name;
                }
            }
            current = utext_next32(text);
        }
        else {
            current = utext_next32(text);
        }
    }
#endif
}


void RegexMatcher::split_default(
    const StringView& subject,
    const void* subject_identity,
    std::vector<RegexRange>& fields,
    UErrorCode& status
)
{
    fields.clear();
    status = U_ZERO_ERROR;
    if (subject.is_na() || subject_identity == nullptr || matcher_ == nullptr) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    if (subject_identity_ != subject_identity || !byte_offsets_valid_) {
        subject_identity_ = nullptr;
        byte_offsets_valid_ = false;
        regex_search::set_utf16_with_offsets(
            subject_, byte_offsets_, subject_ascii_, subject
        );
        subject_identity_ = subject_identity;
        byte_offsets_valid_ = true;
    }

    matcher_->reset(subject_);
    int field_start = 0;
    for (;;) {
        const UBool found = matcher_->find(status);
        if (found == 0 || U_FAILURE(status))
            break;

        const int match_start_utf16 = matcher_->start(status);
        if (U_FAILURE(status))
            break;
        const int match_start = byte_offset(match_start_utf16, status);
        if (U_FAILURE(status))
            break;
        const int match_end_utf16 = matcher_->end(status);
        if (U_FAILURE(status))
            break;
        const int match_end = byte_offset(match_end_utf16, status);
        if (U_FAILURE(status))
            break;

        fields.push_back(RegexRange{field_start, match_start});
        field_start = match_end;
    }

    if (U_FAILURE(status)) {
        close();
        return;
    }
    fields.push_back(RegexRange{field_start, subject.len});
}


RegexSplitResult RegexMatcher::split(
    const StringView& subject,
    const void* subject_identity,
    int n,
    bool omit_empty,
    bool tokens_only,
    std::vector<RegexRange>& fields,
    UErrorCode& status
)
{
    fields.clear();
    status = U_ZERO_ERROR;
    if (subject.is_na() || subject_identity == nullptr || matcher_ == nullptr) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return RegexSplitResult::ok;
    }
    if (n >= std::numeric_limits<int>::max()-1)
        return RegexSplitResult::limit_too_large;
    if (n == 0)
        return RegexSplitResult::ok;
    if (n < 0 && !omit_empty && !tokens_only) {
        split_default(subject, subject_identity, fields, status);
        return RegexSplitResult::ok;
    }

    if (subject_identity_ != subject_identity || !byte_offsets_valid_) {
        subject_identity_ = nullptr;
        byte_offsets_valid_ = false;
        regex_search::set_utf16_with_offsets(
            subject_, byte_offsets_, subject_ascii_, subject
        );
        subject_identity_ = subject_identity;
        byte_offsets_valid_ = true;
    }

    matcher_->reset(subject_);
    const int field_limit = n < 0
        ? std::numeric_limits<int>::max()
        : n;
    int search_limit = field_limit;
    if (tokens_only && search_limit < std::numeric_limits<int>::max())
        ++search_limit;

    fields.push_back(RegexRange{0, 0});
    int field_count = 1;
    while (field_count < search_limit && U_SUCCESS(status)) {
        const UBool found = matcher_->find(status);
        if (found == 0 || U_FAILURE(status))
            break;

        const int match_start_utf16 = matcher_->start(status);
        if (U_FAILURE(status))
            break;
        const int match_start = byte_offset(match_start_utf16, status);
        if (U_FAILURE(status))
            break;
        const int match_end_utf16 = matcher_->end(status);
        if (U_FAILURE(status))
            break;
        const int match_end = byte_offset(match_end_utf16, status);
        if (U_FAILURE(status))
            break;

        RegexRange& current = fields.back();
        if (omit_empty && current.start == match_start) {
            current.start = match_end;
        }
        else {
            current.end = match_start;
            fields.push_back(RegexRange{match_end, match_end});
            ++field_count;
        }
    }

    if (U_FAILURE(status)) {
        close();
        return RegexSplitResult::ok;
    }

    fields.back().end = subject.len;
    if (omit_empty && fields.back().start == fields.back().end)
        fields.pop_back();

    if (tokens_only && field_limit < std::numeric_limits<int>::max()) {
        while (fields.size() > static_cast<std::size_t>(field_limit))
            fields.pop_back();
    }
    return RegexSplitResult::ok;
}


bool RegexMatcher::subject_is_ascii() const noexcept
{
    return subject_ascii_;
}


int RegexMatcher::byte_offset(
    int utf16_offset, UErrorCode& status
) const noexcept
{
    if (subject_ascii_)
        return utf16_offset;
    if (utf16_offset < 0 ||
            static_cast<std::size_t>(utf16_offset) >= byte_offsets_.size()) {
        status = U_INDEX_OUTOFBOUNDS_ERROR;
        return 0;
    }
    return byte_offsets_[static_cast<std::size_t>(utf16_offset)];
}

} // namespace shared
} // namespace charr
