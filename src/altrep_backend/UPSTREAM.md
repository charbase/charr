# Copied stringi backend

Upstream package: stringi 1.8.7.9001 at commit
`19e9586ba39b3320df49355e32bd18d74ed6098f`.

The R and C++ sources in this backend are copied from stringi and
mechanically rename the `stri_*` implementation namespace to `ci_*`.
They retain stringi's BSD-3-Clause notices; see `inst/COPYRIGHTS`.

The mechanical reference keeps the copied operation bodies recognizable while
using charport readers for character input and charvec builders for character
output. Source comments mark the cases that require more than direct
substitution.
