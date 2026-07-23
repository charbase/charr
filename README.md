# charr

charr is a benchmark and record-keeping snapshot for an ALTREP experiment. It
translates the stringi implementation used by stringr to charport `Reader`s and
charvec builders while keeping the copied algorithms recognizable. This
snapshot is not intended to be a complete replacement for stringr or stringi.

`charr_altrep(FALSE)` routes calls to the installed stringi package.
`charr_altrep(TRUE)` routes the same calls to charr's separate copied backend.
The reference pass is serial and deliberately leaves general optimization for
a later branch. The ignored local file `scratch/altrep/work-order.md` records
the correctness and output-construction exceptions used during the pass.

The public surface is the imported stringr API, plus `charr_altrep()`,
`str_reverse()`, and `str_read_lines()`.

One representation limitation is kept on purpose. `str_wrap()` still uses
stringr's R-level `vapply()` join, so its final result is an ordinary character
vector even when the ALTREP backend is on. Replacing that join with one native
builder is an optimization task, not part of this reference pass.

## Source record

- stringr 1.6.0.9000 at commit
  `ae054b1d28f630fee22ddb3cb7525396e62af4fe`
- stringi 1.8.7.9001 at commit
  `19e9586ba39b3320df49355e32bd18d74ed6098f`
- ICU4C 74.1, using stringi's source layout and portability patches
- charr commit `3782b1fb6151a8469c7c727e097c6dda09f241d4` as the comparison
  point for the mechanical conversion

The imported stringr snapshot is preserved at charr commit `006509f`. Backend
provenance is also recorded in
[`src/altrep_backend/UPSTREAM.md`](src/altrep_backend/UPSTREAM.md) and
[`src/icu74/UPSTREAM.md`](src/icu74/UPSTREAM.md).

## Reproducing the reference

Use the repository Makefile for package work:

```sh
make install
make test
make test-altrep
```

In the working checkout, the seeded three-repetition benchmark and its saved
results live under the ignored local directory `scratch/benchmark/`. They are
not part of the package or the reference tag.

## Credit and license

charr is derived from stringr by Hadley Wickham and the stringr authors (MIT).
Its private backend is copied from stringi by Marek Gagolewski
(BSD-3-Clause). ICU4C is distributed under the Unicode License v3. See
[`inst/COPYRIGHTS`](inst/COPYRIGHTS) for the complete notices.

This work is funded by the R Consortium Infrastructure Steering Committee.
