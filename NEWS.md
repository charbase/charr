# charr 0.2.0

* Requires charport 0.2.0 or later.

* Under the `"altrep"` backend, short results (at most 64 strings and 64 KiB
  of text) are now returned as charport charpacks: a single raw vector with
  no external pointer or finalizer, which is cheaper to create, copy and
  serialize. Larger results are still returned as charvecs. Values are
  unchanged; only the ALTREP class differs.

* `str_trunc()` and `str_flatten(na.rm = TRUE)` (without `last`) no longer
  call `is.na()` on plain character input under the `"base"` and `"altrep"`
  backends, so packed ALTREP input stays packed. Other input types keep
  stringr's behavior.

* Native-encoding detection now uses charport's check, which reads R's own
  UTF-8 locale flag, instead of an iconv probe.

* Under the `"altrep"` backend, `str_conv()` output that is pure ASCII is now
  marked as ASCII rather than as the target encoding, so it serializes in
  packed form in any locale.

# charr 0.1.0

* Initial CRAN release.
