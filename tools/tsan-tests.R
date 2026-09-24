# Driver for `make test-tsan`. The Makefile starts R's binary directly with
# the ThreadSanitizer runtime preloaded, because R's shell launchers crash
# when that runtime is injected into /bin/sh. The preload is dropped here, as
# the first action, so the subprocesses the suite starts (locale probes,
# Rscript children) run uninstrumented instead of crashing.
#
# Usage, from tests/:
#   R -f ../tools/tsan-tests.R --args altrep <threads>
Sys.unsetenv("LD_PRELOAD")

# The smallest chunk makes every eligible operation cut its range as finely as
# the worker count allows, so short test vectors still interleave chunks
# across threads instead of running as one chunk per worker.
library(charr)
charr_min_chunk(1)
charr_chunks_per_worker(1000)

source("testthat/support/run-backend-tests.R")
