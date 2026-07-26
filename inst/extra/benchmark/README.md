# Backend benchmark

This directory holds the performance harness. It is tracked in git and excluded
from the package build by `.Rbuildignore`, so nothing here ships to users. It
measures the 67 operations in the current `.charr_leaf_map` and no extra stringi
functions, option constructors, representation probes, or noise controls.

`data/` holds the prepared corpus: roughly 200 MB, git-ignored, and rebuilt
deterministically by `prepare-corpus.R` from `local/sentences.tar.bz2` and the
recorded seed. `results/` holds the measurements those runs produced.
`make-vignette-figures.R` turns a recorded label into the figures and the table
used by the vignettes; it never runs a benchmark itself.

The three timed conditions are:

- the current stringi backend with ordinary character input;
- the current base backend with ordinary character input; and
- the current ALTREP backend with unmaterialized charvec input.

The frozen Claude ALTREP snapshot was retired as a timed condition once both
optimized backends beat it on every operation. Its last measurements remain in
the archived `*-times.csv` files, which still carry a `claude_altrep`
condition; `compare-targets.R` now scores both candidates against stringi.

Current-backend leaves are resolved from `.charr_backend_environments` through
the inverse of `.charr_leaf_map`. This is important: calling the namespace's
`ci_*` wrapper directly would bypass the new R-level backend selection.

Every condition gets timed repetitions in fresh R processes, three by default
and five in the recorded run. The worker excludes package startup, corpus
loading, auxiliary input construction, and garbage collection from the timed
region. There is no verification-only timing arm.

## Input scales

The scale classes are explicit in `benchmark-ops.R`:

- fast: 1,000,000 records;
- medium: 100,000 records; and
- slow: 10,000 records.

The fast set contains fixed count/detect/starts/ends, fixed first
extract/locate, comparison equivalence, flatten, length, read-lines,
replace-NA, and the three trims. The slow set contains collation
count/extract-all/locate-all/replace-all/split and wrap. All other operations
use the medium size.

`prepare-corpus.R` streams `sentences.csv` from
`local/sentences.tar.bz2`. With seed `20260721`, it reservoir-samples the full
archive and writes deterministic, nested prefixes. Text, Tatoeba language ID,
and sentence ID remain aligned at every scale. Each scale has ordinary UTF-8
input and an unmaterialized charvec counterpart. `ci_read_lines` reads the text
file for its scale and does not load an unused vector input.

The same script prepares every derived query argument. Fixed extraction gets
an aligned vector containing the most common suitable surface word for each
language; fixed splitting still uses whitespace. Collation rows use four
natural language partitions, frequent words, and case-equivalent patterns
verified with charr's selected ICU. Starts-with and ends-with use separate
natural subsets when the general partition has too few positional matches.
Sentence contents are never edited to create positives.

The main conversion row decodes saved Windows-1252, ISO-8859-2, and Shift_JIS
byte strings. Base `iconv()` creates the legacy bytes before timing. A record is
kept only when base `iconv()`, charr's ICU, and stringi's ICU all decode it to
the exact source UTF-8. This excludes ambiguous mappings such as Shift_JIS
`0x8160`, which can map to either U+301C or U+FF5E.

Some operations need a small, deterministic setup to measure their working
path. Trim receives one space at each edge, and replace-NA receives a missing
value every eighth record. Setup happens before timing and restores the input
mode: plain vectors stay plain, while ALTREP conditions receive a fresh,
unmaterialized charvec.

Prepare the corpus and all derived inputs:

```sh
Rscript inst/extra/benchmark/prepare-corpus.R \
  local/sentences.tar.bz2 20260721
```

The cache key includes the source archive, seed, preparation script, benchmark
registry, selected ICU metadata, and artifact hashes. A matching rerun checks
the files and returns without rebuilding them.

Inspect the run plan without loading either package or starting a benchmark:

```sh
Rscript inst/extra/benchmark/run.R --dry-run
Rscript inst/extra/benchmark/run.R --dry-run '^ci_length$'
```

Install the snapshot under test into its own package library, then run:

```sh
Rscript inst/extra/benchmark/run.R \
  optimized-backends \
  /tmp/charr-main-lib <main-commit>
```

An optional operation regex limits a smoke run. `--reps=N` changes the default
three repetitions for a targeted comparison. `--resume` continues a run whose
raw CSV already exists:

```sh
Rscript inst/extra/benchmark/run.R \
  optimized-backends \
  /tmp/charr-main-lib <main-commit> \
  '^ci_(length|wrap)$' --reps=9 --resume
```

Before timing an operation, the runner evaluates its full input in all three
conditions. It compares payload bytes, missingness, encoding marks, type,
shape, and normalized attributes. Timing does not start unless every output
agrees. Collation order/rank, title case, and the six word-boundary rows are
narrow exceptions when charr and stringi link different ICU releases: base and
ALTREP must still equal each other. This records changes in ICU's collation,
Unicode case, and boundary data without accepting
a disagreement between two backends built against the same ICU. Tokenization,
query derivation, transcoding setup, charvec construction, package startup,
input loading, and preflight are outside the timed expression.

The raw CSV records the condition, branch commit, backend, repetition count,
input size and representation, corpus hash, actual ICU owner/version/mode, R
version, installed package path, fixture format and selected artifact hash,
and hashes of the benchmark and preparation scripts. Resume rejects a changed
fixture or script. The stringi condition records stringi's ICU; the other two
record the ICU linked by their charr package.

Plot a completed run:

```sh
Rscript inst/extra/benchmark/plot-relative-performance.R optimized-backends
```

For a targeted run with more repetitions, compare each candidate backend
against stringi directly:

```sh
Rscript inst/extra/benchmark/compare-targets.R optimized-backends
```

The target report pairs fresh-process repetitions, reports the median speedup,
and uses an exact one-sided sign test. Ties are excluded from that test. A row
passes only when its median speedup is above one and the sign-test p-value is
below 0.05. Three-repetition plot runs are therefore descriptive; use more
repetitions to close a work-order row.

The plot uses a raw, linear millisecond axis with an independent range in each
facet. Each operation has three grouped median bars, its raw repetition points,
and a min-max error bar. Most integration rows use three repetitions; a close
targeted row may retain a larger run when the timing CSV records that choice.
The retained
artifacts are `*-times.csv`, `*-summary.csv`, and the wide PNG/PDF pair.
