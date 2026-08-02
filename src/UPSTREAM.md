# stringi provenance

Charr's two optimized backends, `base_backend/` and `altrep_backend/`, derive
from stringi's R and C++ implementation. The upstream package is stringi
1.8.9 at commit `68fe9b91cee2ca959d72a75660add028531d6af1`
(2026-07-30).

The original import was mechanical: every `stri_*` name became `ci_*` and the
operation bodies stayed recognizable. That translation is frozen on the
`mechanical-altrep-reference` branch and is the reference for what charr
started from.

The code here is no longer that translation. Operations have been rewritten
around a package-owned unwind boundary, backend-specific input and output
policy, and shared native kernels under `shared/`. Files still carry stringi's
BSD-3-Clause notices wherever copied code remains; see `inst/COPYRIGHTS`.

`base_backend/` and `altrep_backend/` are deliberately separate
implementations, because their input and output shapes differ. They are kept in
agreement by review rather than by sharing a kernel.
