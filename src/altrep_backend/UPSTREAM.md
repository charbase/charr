# Copied stringi backend

Upstream commit: `19e9586ba39b3320df49355e32bd18d74ed6098f`.

The R and C++ sources in this backend are copied from stringi and
mechanically rename the `stri_*` implementation namespace to `ci_*`.
They retain stringi's BSD-3-Clause notices; see `inst/COPYRIGHTS`.

This is the semantic scaffold. It initially uses stringi's ordinary
materializing containers. Later work replaces those containers with
charport readers and charvec result builders without changing semantics.
