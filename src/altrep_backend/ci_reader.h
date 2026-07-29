// Copyright (c) 2026 charr authors
// SPDX-License-Identifier: MIT

#ifndef CHARR_CI_READER_H
#define CHARR_CI_READER_H

#include "charport.h"
#include "ci_exception.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace charr { namespace altrep_backend {


namespace ci {


/** Resolve a Reader lease through charr's R unwind adapter.
 *
 * charport::Reader(SEXP) follows ordinary R error semantics. Charr may already
 * own other Reader leases and native containers when it opens another one, so
 * it protects acquisition explicitly and adopts the resolved C reader.
 */
inline charport_reader protected_reader_resolve(SEXP source)
{
    charport_reader resolved = {};
    ci::unwind_protect([&]() -> SEXP {
        resolved = charport::resolve(source);
        return R_NilValue;
    });
    return resolved;
}


/** Own one Reader borrow.
 *
 * The caller keeps the source protected until this object is destroyed. The
 * Reader is private. A borrow caches either one full-range views() call or one
 * indexed view() call and does not mix the two access modes. This is a local
 * cache policy, not a restriction on opening another Reader for the SEXP.
 */
class ReaderBorrow {
private:
    charport::Reader reader_;
    charport::StrViews views_;
    bool has_views_;
    charport::StrView view_;
    R_xlen_t view_index_;
    bool has_view_;

public:
    explicit ReaderBorrow(SEXP source)
        : reader_(protected_reader_resolve(source)), views_(),
          has_views_(false), view_(),
          view_index_(-1), has_view_(false)
    {
    }

    ReaderBorrow(const ReaderBorrow&) = delete;
    ReaderBorrow& operator=(const ReaderBorrow&) = delete;

    R_xlen_t size() const noexcept
    {
        return reader_.size();
    }

    const charport::StrViews& views()
    {
        if (has_view_)
            throw std::logic_error("cannot request full views after an indexed view");
        if (!has_views_) {
            reader_.views(0, reader_.size(), views_);
            has_views_ = true;
        }
        return views_;
    }

    charport::StrView view(R_xlen_t index)
    {
        if (index < 0 || index >= reader_.size())
            throw std::out_of_range("character vector index out of bounds");
        if (has_views_)
            throw std::logic_error("cannot request an indexed view after full views");
        if (has_view_) {
            if (index != view_index_)
                throw std::logic_error("Reader borrow already caches another index");
            return view_;
        }
        view_ = reader_.view(index);
        view_index_ = index;
        has_view_ = true;
        return view_;
    }
};


/** Reuse one active Reader borrow for each exact source SEXP in an operation.
 *
 * Multiple Readers may borrow the same SEXP. This cache is only an
 * optimization that avoids repeated Reader setup and access calls.
 * The context does not preserve its source SEXPs. Its caller owns that R
 * protection and must destroy every borrow before releasing it.
 */
class ReaderContext {
private:
    struct Entry {
        R_xlen_t size;
        std::weak_ptr<ReaderBorrow> borrow;
    };

    // Lists can contribute thousands of distinct character-vector children.
    // Pointer identity is stable while the caller protects those sources, so
    // use it directly instead of linearly rescanning every prior entry.
    std::unordered_map<SEXP, Entry> entries_;
    ci::DeferredWarnings& warnings_;

public:
    explicit ReaderContext(ci::DeferredWarnings& warnings)
        : entries_(), warnings_(warnings)
    {
    }
    ReaderContext(const ReaderContext&) = delete;
    ReaderContext& operator=(const ReaderContext&) = delete;

    R_xlen_t size(SEXP source)
    {
        std::unordered_map<SEXP, Entry>::const_iterator found =
            entries_.find(source);
        if (found != entries_.end())
            return found->second.size;

        R_xlen_t source_size = 0;
        ci::unwind_protect([&]() -> SEXP {
            source_size = XLENGTH(source);
            return R_NilValue;
        });
        Entry entry = {source_size, std::weak_ptr<ReaderBorrow>()};
        entries_.insert(std::make_pair(source, entry));
        return source_size;
    }

    std::shared_ptr<ReaderBorrow> acquire(SEXP source)
    {
        std::unordered_map<SEXP, Entry>::iterator found =
            entries_.find(source);
        if (found != entries_.end()) {
            std::shared_ptr<ReaderBorrow> active = found->second.borrow.lock();
            if (active)
                return active;

            active.reset(new ReaderBorrow(source));
            if (active->size() != found->second.size)
                throw std::runtime_error("character vector length changed during an operation");
            found->second.borrow = active;
            return active;
        }

        std::shared_ptr<ReaderBorrow> active(new ReaderBorrow(source));
        Entry entry = {active->size(), active};
        entries_.insert(std::make_pair(source, entry));
        return active;
    }

    void warn(const char* message)
    {
        warnings_.push(message);
    }

    void emitWarnings()
    {
        for (std::unordered_map<SEXP, Entry>::const_iterator it = entries_.begin();
                it != entries_.end(); ++it) {
            if (!it->second.borrow.expired())
                throw std::logic_error("cannot emit R warnings during an active Reader borrow");
        }
        warnings_.emit();
    }
};


inline R_len_t checked_r_len(R_xlen_t size, const char* what)
{
    // Deviation from stringi: reject long vectors before narrowing to the
    // copied containers' R_len_t fields.
    if (size < 0 || size > R_LEN_T_MAX)
        throw std::length_error(std::string("long ") + what + " are not supported");
    return static_cast<R_len_t>(size);
}


inline bool is_ascii(const char* data, size_t length) noexcept
{
    for (size_t i = 0; i < length; ++i) {
        if (static_cast<unsigned char>(data[i]) > 0x7f)
            return false;
    }
    return true;
}


} // namespace ci


} } // namespace charr::altrep_backend

#endif
