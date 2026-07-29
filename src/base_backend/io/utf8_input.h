#ifndef CHARR_BASE_UTF8_INPUT_H
#define CHARR_BASE_UTF8_INPUT_H

#include "../../shared/native_to_utf8.h"
#include "../../shared/slice_arena.h"
#include "utf8_views.h"

#include <Rinternals.h>
#include <unicode/unistr.h>

#include <deque>
#include <utility>
#include <vector>

namespace charr {
namespace base_backend {
namespace io {

enum class Utf8BomPolicy : unsigned char {
    strip,
    preserve
};

class Utf8Input {
public:
    // Source is already protected by the public entry point. Its CHARSXP
    // payloads remain stable for this borrow, while converted records live in
    // converted_.
    Utf8Input(
        SEXP source, R_xlen_t recycle_size,
        Utf8BomPolicy bom_policy = Utf8BomPolicy::strip
    );

    Utf8Input(const Utf8Input&) = delete;
    Utf8Input& operator=(const Utf8Input&) = delete;
    Utf8Input(Utf8Input&&) = delete;
    Utf8Input& operator=(Utf8Input&&) = delete;

    R_xlen_t source_size() const noexcept;
    R_xlen_t size() const noexcept;
    // The source-order table is not recycled. Its address remains stable for
    // this immutable input's lifetime.
    const Utf8Record* source_data() const noexcept;
    R_len_t get_n() const noexcept;
    R_len_t get_nrecycle() const noexcept;

    R_len_t vectorize_init() const noexcept;
    R_len_t vectorize_end() const noexcept;
    R_len_t vectorize_next(R_len_t index) const noexcept;

    Utf8Record record(R_xlen_t index) const;
    Utf8Record text(R_xlen_t index) const;
    const Utf8Record& get(R_len_t index) const;
    const Utf8Record& getNAble(R_len_t index) const;
    bool is_na(R_xlen_t index) const;
    bool isNA(R_len_t index) const;
    bool is_borrowed(R_xlen_t index) const;

    SEXP to_sexp() const;

private:
    SEXP source_;
    R_xlen_t source_size_;
    R_xlen_t recycle_size_;
    Utf8BomPolicy bom_policy_;
    const SEXP* elements_;
    std::vector<Utf8Record> records_;
    std::vector<unsigned char> source_borrowed_;
    shared::SliceArena converted_;
    shared::NativeToUtf8 converter_;

    void initialize_records();
    void normalize_record(R_xlen_t index, SEXP charsxp);
    R_xlen_t source_index(R_xlen_t index) const;
    SEXP charsxp(R_xlen_t index) const;
    void assert_invariants() const;

    friend class Utf8Workspace;
};

class Utf8Workspace {
public:
    Utf8Workspace(SEXP source, R_xlen_t recycle_size);

    Utf8Workspace(const Utf8Workspace&) = delete;
    Utf8Workspace& operator=(const Utf8Workspace&) = delete;
    Utf8Workspace(Utf8Workspace&&) = delete;
    Utf8Workspace& operator=(Utf8Workspace&&) = delete;

    R_len_t get_n() const noexcept;
    R_len_t get_nrecycle() const noexcept;
    R_len_t vectorize_init() const noexcept;
    R_len_t vectorize_end() const noexcept;
    R_len_t vectorize_next(R_len_t index) const noexcept;

    const Utf8Record& get(R_len_t index) const;
    const Utf8Record& getNAble(R_len_t index) const;
    bool isNA(R_len_t index) const;

    void set_na(R_len_t index);
    void set(R_len_t index, const Utf8Record& value);
    void set(R_len_t index, const icu::UnicodeString& value);
    void replace_all_at_pos(
        R_len_t index, R_len_t output_size,
        const char* replacement, R_len_t replacement_size,
        const std::deque<std::pair<R_len_t, R_len_t>>& occurrences
    );

    SEXP to_sexp() const;

private:
    Utf8Input input_;
    std::vector<Utf8Record> records_;
    std::vector<unsigned char> changed_;
    shared::SliceArena replacements_;

    R_len_t checked_index(R_len_t index) const;
    void store(R_len_t index, const char* data, R_len_t length);
};

class IndexedUtf8Input {
public:
    IndexedUtf8Input(
        SEXP source, R_xlen_t recycle_size,
        Utf8BomPolicy bom_policy = Utf8BomPolicy::strip
    );

    R_len_t UChar32_to_UTF8_index_back(R_len_t index, R_len_t position);
    R_len_t UChar32_to_UTF8_index_fwd(R_len_t index, R_len_t position);
    void UTF8_to_UChar32_index(
        R_len_t index, int* first, int* second, int size,
        int first_adjustment, int second_adjustment
    );

    R_len_t get_n() const noexcept { return input_.get_n(); }
    R_len_t get_nrecycle() const noexcept { return input_.get_nrecycle(); }
    R_len_t vectorize_init() const noexcept {
        return input_.vectorize_init();
    }
    R_len_t vectorize_end() const noexcept {
        return input_.vectorize_end();
    }
    R_len_t vectorize_next(R_len_t index) const noexcept {
        return input_.vectorize_next(index);
    }
    const Utf8Record& get(R_len_t index) const {
        return input_.get(index);
    }
    const Utf8Record& getNAble(R_len_t index) const {
        return input_.getNAble(index);
    }
    bool isNA(R_len_t index) const { return input_.isNA(index); }

private:
    Utf8Input input_;
    R_len_t last_fwd_codepoint_;
    R_len_t last_fwd_utf8_;
    const char* last_fwd_record_;
    R_len_t last_back_codepoint_;
    R_len_t last_back_utf8_;
    const char* last_back_record_;
};

} // namespace io
} // namespace base_backend
} // namespace charr

#endif
