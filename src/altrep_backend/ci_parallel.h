// Copyright (c) 2026 charr authors
// SPDX-License-Identifier: MIT

#ifndef CHARR_CI_PARALLEL_H
#define CHARR_CI_PARALLEL_H

#include "ci_exception.h"
#include "../shared/parallel.h"

#include <cstdio>
#include <exception>

namespace charr { namespace altrep_backend {

/*
 * The backend's data-parallel body. Operations derive from this rather than
 * from shared::ParallelBody so that one override, here, names the backend's
 * own exception type. Kernels keep throwing StriException; the driver copies
 * the message out on the worker thread and re-raises it on the main thread.
 */
class ParallelBody : public shared::ParallelBody {
public:
    CHARR_CXX_HELPER void describe_error(
        char* message, std::size_t size
    ) noexcept override
    {
        // Called only from the driver's worker handler, with the failing
        // exception active. StriException is a plain class, not a
        // std::exception, so it needs its own clause.
        try {
            throw;
        }
        catch (const StriException& error) {
            std::snprintf(message, size, "%s", error.getMessage());
        }
        catch (const std::exception& error) {
            std::snprintf(message, size, "%s", error.what());
        }
        catch (...) {
            std::snprintf(message, size, "unknown C++ exception");
        }
    }

protected:
    CHARR_NEUTRAL_HELPER ParallelBody() noexcept = default;
    ~ParallelBody() = default;
};

} } // namespace charr::altrep_backend

#endif
