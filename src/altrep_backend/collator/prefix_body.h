// Copyright (c) 2026 charr authors
// SPDX-License-Identifier: MIT

#ifndef CHARR_ALTREP_COLLATOR_PREFIX_BODY_H
#define CHARR_ALTREP_COLLATOR_PREFIX_BODY_H

#include "../ci_exception.h"
#include "../ci_parallel.h"
#include "../../shared/collation_ordering.h"
#include "../../shared/collator.h"
#include "../../shared/string_view.h"

#include <cstddef>

namespace charr {
namespace altrep_backend {
namespace collator {

/*
 * The ALTREP parallel shape of the abbreviated collation keys that
 * ordering, ranking and deduplication share: each element's prefix is written
 * at its own index. A serial plan uses the entry point's collator on the
 * calling thread; in a threaded plan every worker, including the calling
 * thread, uses its own clone, since ICU documents ucol_clone as thread safe
 * but promises nothing for sort-key generation on one shared UCollator.
 */
class PrefixBody final : public ParallelBody {
public:
    CHARR_CXX_HELPER PrefixBody(
        const shared::StringView* values,
        const shared::Collator& collator,
        shared::CollationPrefix* keys
    ) noexcept
        : values_(values), collator_(collator), keys_(keys)
    {
    }

    CHARR_CXX_HELPER void run(shared::WorkerContext& context) override
    {
        shared::Collator worker_collator;
        const UCollator* collator = collator_.get();
        if (context.workers > 1) {
            const UErrorCode status = worker_collator.clone_from(collator_);
            if (U_FAILURE(status))
                throw StriException(status);
            collator = worker_collator.get();
        }

        while (context.next_chunk()) {
            const UErrorCode status = shared::compute_collation_prefixes(
                values_, static_cast<std::size_t>(context.begin),
                static_cast<std::size_t>(context.end), collator, keys_
            );
            if (U_FAILURE(status))
                throw StriException(status);
        }
    }

private:
    const shared::StringView* values_;
    const shared::Collator& collator_;
    shared::CollationPrefix* keys_;
};

} // namespace collator
} // namespace altrep_backend
} // namespace charr

#endif
