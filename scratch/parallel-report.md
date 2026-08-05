# ALTREP parallelism implementation report

This is the implementation handoff for `scratch/parallel-work-order.md`. It
records what landed, what was measured, and where the implementation differs
from the work order.

## Decisions and provenance

- The branch was first moved to main commit `6bf27c8`. Claude's work was
  uncommitted, so I carried it through a named stash across a fast-forward
  merge; there was nothing that Git could cherry-pick. The sibling
  `../charr-main` worktree remained clean and untouched.
- The foundation is `c9caac2`, the operation conversion is `78e8c82`, and the
  defensive option-parser fix is `ded0b66`.
- At the user's request, there are no `parallel_min_*`, element-count,
  payload-byte, or measured-crossover thresholds. When more than one thread is
  requested and an operation has more than one independent semantic task, the
  scheduler uses parallel execution. It caps workers only by the task count
  and the 256-worker safety limit. This deliberately accepts regressions on
  kernels too cheap to amortize thread startup.
- The only runtime gates are semantic: one task, one requested thread, a
  finite `max_count` in detect operations, and regex locate-first with capture
  allocation. These cases cannot be split without changing results or R
  allocation rules.
- The user replaced the work order's per-operation threading-test matrix with
  the complete ALTREP suite at one and four threads. I removed the proposed
  `test-parallel-plan.R` test. A focused `test-charr-threads.R` remains because
  it tests the public accessor and malformed direct option values, not
  operation-by-operation threading.
- Final native method counts are 70 for base and 71 for ALTREP. The work-order
  totals do not match its rows: the rows contain 16 A, 21 B, 9 C, 11 D, 7 E,
  and 7 F entries. I followed the row assignments rather than the printed
  totals.
- Base leaves are cloned from ALTREP functions at namespace load; they are not
  independent source leaves as the work order assumed. Converted ALTREP
  leaves append `getOption("charr_threads", 1L)`. A narrow load-time rewrite
  removes that exact final argument only from registered base aliases. Base
  native signatures and arities did not change.

## How to read the measurements

The table's serial delta is
`100 * (current ALTREP median at one thread / main ALTREP median - 1)`.
Parentheses show main-to-current median milliseconds. Most public rows compare
five fresh processes from `pre-parallel-main-times.csv` and
`post-parallel-final-times.csv`. Ten small or apparently regressed rows were
repeated nine times in `serial-audit-{main,current}-times.csv`; those rows use
the nine-repetition medians. Supplement-only shapes use the paired main/current
runner and `parallel-supplement-clean-supplement-times.csv`.

The post-change serial file is complete: 67 operations, three backends, and
five repetitions give 1,005 data rows. It combines two disjoint clean shards,
the first 24 operations and the remaining 43. Each operation and repetition
already runs in its own process, and both shards have the same installed
package, fixture, corpus, and harness hashes, so joining the rows does not
change the measurement unit.

The untouched base controls in that complete run range from -6.4% to +17.6%
(median 0.0%); ten of 67 move by more than 5% in absolute value. The complete
run is therefore descriptive rather than a formal within-noise sign-off under
section 10. The targeted nine-repetition audit narrowed its base controls to
-3.1% through +3.7%. Seven of the ten apparent regressions returned to exactly
0.0%; `ci_length` moved with its base control, while pad-left and pad-both each
remained one millisecond slower. The five measured unchanged Group F rows move
by +0.9% to +2.8%. Supplement base controls range from 0.0% to +5.0%.

Speedups are current ALTREP medians at 1/2/4/8 threads on the largest prepared
scale. Scaling is a separate fresh-process run, so its one-thread denominator
can differ from the serial median shown beside it. No crossover is reported or
used as a gate because the user explicitly rejected threshold-based
scheduling.

The `parallel speedup` column carries two vintages. Seventeen rows come from
the 2026-08-04 `parallel-fixes-20260804` re-run described in *Follow-up: what
the regressions actually were*; the rest are the original
`parallel-final-clean` numbers. Both used this harness with identical corpus,
input, ops, worker, prepare and fixture hashes, so the two sets are
comparable. The `serial delta` column has not been re-measured and still
describes `78e8c82` against main.

## Operation table

| # | entrypoint | planned grp | as-built grp | entrypoint shape | serial delta | parallel speedup | deviations |
|---:|---|:---:|:---:|---|---:|---:|---|
| 1 | `ci_detect_fixed` | A | A | combined | 0.0% (24→24 ms) | 1/1.60/2.40/3.00x | Finite `max_count` is a semantic serial gate. Direct kernel now runs on workers. |
| 2 | `ci_startswith_fixed` | A | A | combined | 0.0% (17→17) | 1/1.50/2.25/2.25x | Direct kernel now runs on workers; the four shape gates moved into the eligibility phase. |
| 3 | `ci_endswith_fixed` | A | A | combined | -5.6% (18→17) | 1/1.58/2.38/2.38x | Direct kernel now runs on workers; the four shape gates moved into the eligibility phase. |
| 4 | `ci_count_fixed` | A | A | combined | 0.0% (28→28) | 1/1.71/2.42/3.22x | Direct kernel now runs on workers; each classifies its own records. |
| 5 | `ci_locate_first_fixed` | A | A | combined | 0.0% (33→33) | 1/1.79/2.62/3.40x | Direct kernel now runs on workers. Matrix columns i and i+n are disjoint per element. |
| 6 | `ci_locate_all_fixed` | E | E | combined | 0.0% (30→30) | 1/0.85/1.00/1.04x | Not converted. One `Rf_allocMatrix` per row is ~235 ns of serial R work, so the ceiling is ~1.12x. |
| 7 | `ci_extract_first_fixed` | B | B | combined | -3.4% (59→57) | 1/1.57/2.07/2.52x | Direct kernel now runs on workers through `io::WorkerSink`; a refusal discards the attempt. |
| 8 | `ci_extract_all_fixed` | E | E | combined | 0.0% (9→9) | 1/1.29/1.50/1.80x | Worker plans are rebased in worker order before serial materialization. |
| 9 | `ci_replace_first_fixed` | B | B | combined | 0.0% (6→6) | 1/1.20/2.00/3.00x | Direct kernel now runs on workers through `io::WorkerSink`; a refusal discards the attempt. |
| 10 | `ci_replace_all_fixed` | B | B | combined | 0.0% (18→18) | 1/1.80/3.00/4.50x | Sequential mode partitions subjects. Direct kernel now runs on workers. |
| 11 | `ci_split_fixed` | D | D | combined | -2.4% (41→40) | 1/1.32/1.64/1.78x | Per-row stores and the VECSXP materialization tail remain serial. |
| 12 | `ci_sub` | C | C | combined | 0.0% (3→3) | 1/0.75/1.00/1.00x | Millisecond quantization dominates; sparse conversion prepass added. |
| 13 | `ci_sub_replacement` | C | C | combined | 0.0% (6→6) | 1/1.00/1.20/1.50x | Measured through `ci_sub<-`; source and replacement use sparse prepasses. |
| 14 | `ci_sub_all` | D | D/C | combined | 0.0% (30→30) | 1/1.07/1.24/1.29x | Main-thread list/index preparation plus sparse conversion; serial assembly caps scaling. |
| 15 | `ci_sub_replacement_all` | D | D/C | combined | 0.0% (7→7) | 1/1.20/2.00/1.50x | Scalar replacement preparation was hoisted out of the per-element loop, which no longer calls into R once per element. |
| 16 | `ci_length` | C | C | combined | +2.0% (50→51) | 1/1.79/3.06/4.33x | Preflight now runs only when the native encoding is not UTF-8; the discarded validation scans are gone. |
| 17 | `ci_join` | B | B | combined | 0.0% (4→4) | 1/0.80/1.33/1.33x | Measured through `ci_c`; collapse and scalar allocation remain serial. |
| 18 | `ci_flatten` | F | F | unchanged | n/a | n/a | Reduction remains serial and takes no thread argument. |
| 19 | `ci_dup` | B | B | combined | 0.0% (5→5) | 1/1.20/1.50/2.00x | Nine-repetition audit cleared the one-millisecond shift; existing sparse slots are the Group C reference shape. |
| 20 | `ci_reverse` | B | B/C | combined | 0.0% (9→9) | 1/0.82/1.00/1.29x | Unlike the plan, it needs sparse native/Latin-1 conversion and validation before workers. |
| 21 | `ci_trim_left` | B | B | combined | -2.0% (51→50) | 1/1.43/1.92/2.17x | The parallel body now reuses the serial `trim_records` template and per-worker `Store`s. |
| 22 | `ci_trim_right` | B | B | combined | 0.0% (53→53) | 1/1.44/2.00/2.36x | The parallel body now reuses the serial `trim_records` template and per-worker `Store`s. |
| 23 | `ci_trim_both` | B | B | combined | +1.9% (54→55) | 1/1.41/2.00/2.26x | The parallel body now reuses the serial `trim_records` template and per-worker `Store`s. |
| 24 | `ci_replace_all_charclass` | B | B | combined | 0.0% (39→39) | 1/1.77/3.25/4.33x | Supplement shape; sequential mode partitions subjects. |
| 25 | `ci_replace_na` | B | B | combined | 0.0% (33→33) | 1/1.14/1.45/1.52x | Zero-copy no-op is now independent of the worker count. Uses per-worker builders joined by `io::concat_stores()` to avoid charport's shard false sharing. |
| 26 | `ci_detect_regex` | A | A | combined | -2.6% (39→38) | 1/1.95/3.25/3.25x | Finite `max_count` stays serial; vectorized patterns split by lane. |
| 27 | `ci_count_regex` | A | A | combined | 0.0% (76→76) | 1/1.87/3.48/3.32x | Vectorized patterns split by lane. Unchanged; re-measured as a control. |
| 28 | `ci_locate_first_regex` | A | A | combined | 0.0% (43→43) | 1/1.87/3.31/3.31x | Capture mode stays serial; failure-aware warning-prefix reduction was added. |
| 29 | `ci_locate_all_regex` | E | E | combined | -1.8% (113→111) | 1/1.45/2.09/2.05x | Per-row ranges/captures, followed by serial matrix materialization. |
| 30 | `ci_extract_first_regex` | B | B | combined | -2.4% (41→40) | 1/1.78/2.93/3.15x | Worker-local matcher and output shard. |
| 31 | `ci_extract_all_regex` | D | D | combined | -0.8% (118→117) | 1/1.68/2.52/2.64x | Per-row stores; simplify/materialization tail stays serial. |
| 32 | `ci_replace_first_regex` | B | B | combined | +1.6% (62→63) | 1/1.82/3.26/3.44x | Worker-local matcher and output shard. |
| 33 | `ci_replace_all_regex` | B | B | combined | 0.0% (111→111) | 1/1.92/3.53/3.53x | Sequential mode partitions subjects and can change matcher/error traversal. |
| 34 | `ci_split_regex` | D | D | combined | 0.0% (81→81) | 1/1.56/2.25/2.38x | Per-row stores and serial VECSXP materialization. |
| 35 | `ci_match_first_regex` | B | B | combined | 0.0% (27→27) | 1/1.50/2.25/2.70x | Capture columns are planned before disjoint matrix writes. |
| 36 | `ci_match_all_regex` | E | E | combined | +0.8% (133→134) | 1/1.68/2.52/2.62x | The work order incorrectly said staging already existed; it was added. |
| 37 | `ci_detect_coll` | A | A | combined | +1.0% (105→106) | 1/1.91/3.45/3.57x | Finite `max_count` stays serial; collators are worker-local. |
| 38 | `ci_startswith_coll` | A | A | combined | +1.8% (110→112) | 1/1.90/3.50/3.61x | Collators open after normalized input is available. |
| 39 | `ci_endswith_coll` | A | A | combined | 0.0% (184→184) | 1/1.86/3.47/3.41x | Collators open after normalized input is available. |
| 40 | `ci_count_coll` | A | A | combined | 0.0% (16→16) | 1/1.70/2.43/2.43x | Nine-repetition audit cleared the one-millisecond shift. |
| 41 | `ci_locate_first_coll` | A | A | combined | +0.9% (107→108) | 1/1.89/3.38/3.48x | Original main-thread collator initialization check is retained before worker setup. |
| 42 | `ci_locate_all_coll` | E | E | combined | -5.3% (19→18) | 1/1.67/2.50/2.22x | Per-row native ranges, then serial matrix materialization. |
| 43 | `ci_extract_first_coll` | B | B | combined | -1.8% (111→109) | 1/1.88/3.30/3.52x | Worker-local collator/matcher and output shard. |
| 44 | `ci_extract_all_coll` | D | D | combined | 0.0% (19→19) | 1/1.58/2.11/2.11x | Per-row stores; simplify/materialization tail stays serial. |
| 45 | `ci_replace_first_coll` | B | B | combined | 0.0% (119→119) | 1/1.80/3.25/3.55x | Worker-local collator/matcher and output shard. |
| 46 | `ci_replace_all_coll` | B | B | combined | 0.0% (18→18) | 1/1.73/2.38/2.71x | Sequential mode partitions subjects and can change resource/error traversal. |
| 47 | `ci_split_coll` | D | D | combined | 0.0% (20→20) | 1/1.54/2.22/2.22x | Per-row stores and serial VECSXP materialization. |
| 48 | `ci_order` | F | F | unchanged | n/a | n/a | Whole-vector sort remains serial. |
| 49 | `ci_rank` | F | F | unchanged | n/a | n/a | Whole-vector sort remains serial. |
| 50 | `ci_cmp_equiv` | A | A | combined | +1.8% (55→56) | 1/1.23/1.42/1.42x | Per-worker collator startup limits scaling. |
| 51 | `ci_duplicated` | F | F | unchanged | n/a | n/a | Whole-vector hash remains serial. |
| 52 | `ci_trans_tolower` | C | C | combined | 0.0% (14→14) | 1/1.56/2.33/2.80x | Sparse prepass; one case mapper per worker. |
| 53 | `ci_trans_toupper` | C | C | combined | 0.0% (25→25) | 1/1.67/3.12/3.57x | Sparse prepass; one case mapper per worker. Unchanged; re-measured as a control. |
| 54 | `ci_trans_totitle` | C | C | combined | 0.0% (100→100) | 1/1.91/3.61/4.21x | Sparse prepass; one title engine per worker. |
| 55 | `ci_trans_nfc` | C | C | combined | 0.0% (30→30) | 1/1.67/3.00/3.33x | Supplement shape; BOM behavior retained. |
| 56 | `ci_count_boundaries` | A | A | combined | 0.0% (78→78) | 1/1.95/3.71/4.11x | One break iterator per worker. |
| 57 | `ci_locate_first_boundaries` | A | A | combined | 0.0% (28→28) | 1/1.87/3.11/3.50x | Encoding shortcuts retained; worker iterators open after normalization. |
| 58 | `ci_locate_all_boundaries` | E | E | combined | -0.9% (117→116) | 1/1.43/2.00/2.23x | Per-row ranges and serial matrix materialization. |
| 59 | `ci_extract_first_boundaries` | B | B | combined | 0.0% (29→29) | 1/1.71/2.90/3.22x | One break iterator per worker and sharded output. |
| 60 | `ci_extract_all_boundaries` | D | D | combined | +2.6% (155→159) | 1/1.75/2.88/3.16x | Per-row stores; simplify/materialization tail stays serial. |
| 61 | `ci_split_boundaries` | D | D | combined | -0.7% (134→133) | 1/1.66/2.61/3.02x | Per-row stores and serial VECSXP materialization. |
| 62 | `ci_wrap` | D | D/E | combined | flat 0.0% (18→18); list 0.0% (21→21); joined +5.6% (18→19) | flat 1/1.64/3.00/3.60x; list 1/1.62/3.00/3.50x; joined 1/1.90/3.17/3.80x | Public row is flat; supplement covers list/join. Prefix replay preserves earlier kernel errors after normalization failure. |
| 63 | `ci_pad` | B | B | combined | left +4.8% (21→22); right 0.0% (21→21); both +5.0% (20→21) | left 1/1.83/3.14/3.14x; right 1/1.83/2.75/3.14x; both 1/1.83/2.75/3.14x | Nine-repetition audit; left and both retain a one-millisecond shift. Three public modes share one native entrypoint. |
| 64 | `ci_width` | C | C | combined | 0.0% (16→16) | 1/1.89/3.40/3.40x | Nine-repetition audit cleared the earlier shift. Encoding gate removed; sparse main-thread conversion prepass added. |
| 65 | `ci_escape_unicode` | C | C | combined | 0.0% (31→31) | 1/1.82/3.44/4.43x | The discarded serial `escaped_size` prepass is gone; the preflight is now an encoding-tag walk plus Latin-1/native conversion. |
| 66 | `ci_encode_string` | B | B/E | combined | 0.0% (16→16) | 1/1.45/2.29/2.67x | Measured through `ci_conv`. Native coercion can still select either output shape. |
| 67 | `ci_encode_raw` | E | B/E | combined | 0.0% (30→30) | 1/1.43/1.88/2.00x | Supplement shape. Native coercion can still select either output shape. |
| 68 | `ci_enc_info` | F | F | unchanged | n/a | n/a | Scalar query remains serial. |
| 69 | `ci_read_lines` | F | F | unchanged | n/a | n/a | File I/O remains serial. |
| 70 | `ci_split_lines` | D | D | combined | -2.6% (39→38) | 1/1.31/1.81/2.00x | Supplement shape; per-row stores and serial VECSXP materialization. |
| 71 | `ci_split_lines1` | F | F | unchanged | n/a | n/a | Single-string operation remains serial. |

The no-threshold decision matters in both directions. Regex, collation,
boundary, case-mapping, wrap, and character-class work scale well. Cheap
fixed, trim, length, replace-NA, and replacement-assignment paths sometimes
regress because a request for parallelism is honored even when startup costs
more than the kernel.

> **Superseded 2026-08-04.** The paragraph above, and every "thread startup"
> or "does not amortize startup" note in the table and in *Performance and
> memory*, are wrong. Thread creation on the reference machine is 13.9, 29.6
> and 74.0 microseconds for 2, 4 and 8 workers, against deficits of 10 to 20
> **milliseconds** — three orders of magnitude apart. The regressions had
> three implementation causes, all since fixed. See *Follow-up: what the
> regressions actually were* below, and `scratch/parallel-scaling-analysis.md`
> for the full measurements. The `parallel speedup` column is superseded for
> every operation listed in that section.

## Follow-up: what the regressions actually were

Written 2026-08-04, after the table above. Reference machine: i5-11400, 6
physical cores / 12 threads, dual-channel DDR4, R 4.6.1, system ICU 78.2.

### Thread startup was never the cause

Measured `std::thread` spawn plus join in the shape `shared::run_parallel()`
uses, with worker zero inline and `workers - 1` threads created and joined:
13.9 us at 2 workers, 29.6 us at 4, 74.0 us at 8. `ci_length` cost 47 ms at
one thread and 69 ms at two, a 22 ms deficit; thread creation is 0.06% of it.
The scheduler is not implicated either. It allocates one 4104-byte
`WorkerReport` per worker, splits a contiguous interval, and joins.

### Three implementation causes

**1. Fast paths gated off by `plan.workers == 1`.** Ten entrypoints resolved
an ASCII or byte-level direct kernel and then refused to use it whenever more
than one thread was requested, falling into a serial `normalize_views()` pass
plus the general ICU kernel. Requesting two threads therefore halved the work
*and* swapped in a several-times-more-expensive element. Affected:
`ci_detect_fixed`, `ci_startswith_fixed`, `ci_endswith_fixed`,
`ci_count_fixed`, `ci_locate_first_fixed`, `ci_locate_all_fixed`,
`ci_extract_first_fixed`, `ci_replace_first_fixed`, `ci_replace_all_fixed`,
`ci_replace_na`.

**2. A redundant serial preflight on the parallel path only.** `ci_length` and
`ci_escape_unicode` ran `preflight_inputs()` whenever `plan.workers > 1`. For
ASCII and UTF-8 records that prepass ran `ci__length_utf8_fast` /
`escaped_size` over every string and discarded the result, and the workers
then repeated it. Fitting `t_n = S + W/n` to escape-unicode's 302/314/241/222
ms gives about 200 ms serial against a 302 ms total, an Amdahl ceiling of
1.5x; the measured best was 1.36x.

**3. Parallel bodies rewritten instead of reusing the serial kernel.**
`TrimBody` was a hand-written copy of `trim_records()` that dropped its
`ScalarPattern` / `RecycledSource` / `NormalizedSource` template
specialisation for runtime branches and wrote through the sharded builder
rather than a per-worker `Store`.

### The fix shape

Each `*_direct` helper was split into an O(1) pattern/options eligibility
phase and a per-element kernel, and the existing serial helper was rebuilt on
those two so serial behaviour is unchanged by construction. A `ParallelBody`
runs the same kernel; on an ineligible element it records the index in its own
slot of a Frame-owned `first_ineligible` vector and stops its chunk. After the
join the main thread takes the minimum into `general_start`. This is sound
because chunks are contiguous and ordered and the general `Body` already
resumes at `max(context.begin, general_start)`, so everything below the
minimum was written by the direct kernel and everything at or above it is
unwritten or overwritten.

`io::SerialSink` and `io::WorkerSink` were added to `io/utf8_output.h` so a
string-output kernel is written once and instantiated against either builder.

Two entrypoints cannot use the min-reduction and say so in comments. The
parallel general `Body` for `ci_replace_{first,all}_fixed` always rebuilds
`[0, output_length)` and takes no start index, and `ci_extract_first_fixed`
has no `general_start` at all. There a refusal discards the whole attempt.
Partially written shards cannot escape: the general branch's first statement
is `parallel_output.reset(...)`, which move-assigns a fresh `Store`, and
`to_sexp()` on the direct builder is reached only inside `if (direct)`.

### Measured result

Re-run through this same harness, three repetitions, one fresh process per
repetition, label `parallel-fixes-20260804`. The corpus, input, ops, worker,
prepare and fixture hashes all match `parallel-final-clean`, so these numbers
are directly substitutable and have been written into the operation table
above. All 17 operations passed the harness's exact-output preflight at
1/2/4/8 threads.

| operation | before | after | after, 1/2/4/8 ms | 1-thread |
|---|---|---|---|---|
| `ci_detect_fixed` | 1.00/1.15/1.15 | 1.60/2.40/3.00 | 24/15/10/8 | 23→24 |
| `ci_startswith_fixed` | 0.81/0.85/0.89 | 1.50/2.25/2.25 | 18/12/8/8 | 17→18 |
| `ci_endswith_fixed` | 0.86/0.95/0.95 | 1.58/2.38/2.38 | 19/12/8/8 | 18→19 |
| `ci_count_fixed` | 0.57/0.85/1.04 | 1.71/2.42/3.22 | 29/17/12/9 | 28→29 |
| `ci_locate_first_fixed` | 1.00/1.32/1.38 | 1.79/2.62/3.40 | 34/19/13/10 | 33→34 |
| `ci_extract_first_fixed` | 0.80/1.00/1.14 | 1.57/2.07/2.52 | 58/37/28/23 | 57→58 |
| `ci_replace_first_fixed` | 0.75/1.00/1.50 | 1.20/2.00/3.00 | 6/5/3/2 | 6→6 |
| `ci_replace_all_fixed` | 1.20/2.25/2.57 | 1.80/3.00/4.50 | 18/10/6/4 | 18→18 |
| `ci_replace_na` | 0.82/0.89/1.10 | 1.14/1.45/1.52 | 32/28/22/21 | 33→32 |
| `ci_length` | 0.70/0.83/0.87 | 1.79/3.06/4.33 | 52/29/17/12 | 53→52 |
| `ci_escape_unicode` | 0.97/1.28/1.39 | 1.82/3.44/4.43 | 31/17/9/7 | 32→31 |
| `ci_trim_left` | 0.81/0.98/1.34 | 1.43/1.92/2.17 | 50/35/26/23 | 51→50 |
| `ci_trim_right` | 0.84/1.00/1.39 | 1.44/2.00/2.36 | 52/36/26/22 | 53→52 |
| `ci_trim_both` | 0.87/1.02/1.36 | 1.41/2.00/2.26 | 52/37/26/23 | 53→52 |
| `ci_locate_all_fixed` (not converted) | 0.83/1.00/1.03 | 0.85/1.00/1.04 | 29/34/29/28 | 30→29 |
| `ci_count_regex` (control) | 1.92/3.57/3.41 | 1.87/3.48/3.32 | 73/39/21/22 | 75→73 |
| `ci_trans_toupper` (control) | 1.73/2.89/3.71 | 1.67/3.12/3.57 | 25/15/8/7 | 26→25 |

Not one operation is now slower on threads than serial under this protocol.
The two untouched ICU controls do not move, which is the noise check.

`ci_replace_na` and `ci_sub_replacement_all` were re-measured separately in
`parallel-fixes-20260804b` after the per-worker-builder change described
below: 0.82/0.89/1.10 becomes 1.14/1.45/1.52, and 0.30/0.37/0.41 becomes
1.20/2.00/1.50. `ci_count_regex` rode along as a control and did not move
(1.92/3.48/3.32 against 1.92/3.57/3.41).

### The fourth cause: false sharing in charport's shard array

An in-process protocol, which measures a warm R heap instead of a fresh
process, was harsher on `ci_replace_na` than the harness: 18/24/24/20 ms, or
0.75x at two threads. The parallel path cost about 8 ns per record more than
the serial one, dead linear in the record count and independent of payload
size.

That is false sharing of `charport::charvec::builder_detail::Shard`.
`sizeof(Shard) == 24`, so `ParallelBuilder::shards_` packs two or three shards
into every 64-byte line, and `allocate_bytes()` read-modify-writes
`shard.current_slice_used` and `shard.allocated_bytes` once per record. The
contended line therefore ping-pongs between cores once per record, which is
exactly why the penalty is per record rather than per byte, and why the
one-byte all-NA shape had the worst ratio.

The controlled experiment changed nothing but the byte spacing of the shard
array, on 1,000,000 one-byte records:

| shard stride | 1 thread | 2 | 4 | 6 |
|---|---:|---:|---:|---:|
| 24 (charport's `std::vector<Shard>`) | 4.20 ms | 6.85 | 8.32 | 7.59 |
| 64 | 4.24 | 2.62 | 1.58 | 1.22 |
| 128 | 4.25 | 2.65 | 1.57 | 1.88 |

A first attempt to reproduce this failed and briefly ruled false sharing out.
That attempt modelled only the shard read-modify-write; without the
interleaved record-table traffic the compiler keeps the shard fields in
registers and the contended line stays in L1. The amplifier is the 16 MB of
record-table writes, which evict the line so that nearly every
read-modify-write becomes a miss plus a coherence transfer. The shard traffic
alone costs about +2.4 ms at four threads; together with the record table it
costs +4 to +5 ms.

This also explains why the other `ParallelOutputBuilder` operations still
scaled. They pay the same ~8 ns, but their per-record work is 30-100 ns, so it
amortizes. `ci_replace_na` is a bare record copy, so the contended line is
essentially the whole cost.

`ci_replace_na` was fixed in charr by giving each worker its own
`charport::charvec::Builder`, sized to its chunk, and joining with
`io::concat_stores()` — the shape `ci_search_class_trim.cpp` already uses. Its
builder cost at four threads fell from 9.3 ms to 2.7 ms per 1M records on the
all-NA shape, and end-to-end it went from 18/24/24/20 ms to 18/19/13/13.

The root fix belongs in charport, not here: aligning `builder_detail::Shard`
to a cache line would fix every operation that uses `ParallelOutputBuilder` at
once, for 40 bytes per worker. charport was not modified.

### These are not benchmark-specific wins

The direct kernels already existed and were already used at one thread, so
the one-thread column barely moves (detect 19 to 21 ms, count 23 to 25,
length 48 to 48, escape 303 to 301). Nothing was tuned to the corpus; a gate
that disabled an existing optimisation was removed.

Shapes that are deliberately ineligible for every direct path still scale,
which is what shows the general kernels were never the problem:

| ineligible shape, 1M rows | 1/2/4/8 ms | speedup |
|---|---|---|
| `count_fixed`, 3-byte pattern | 43/26/18/15 | 1.65/2.39/2.87 |
| `count_fixed`, `ignore_case` | 293/153/83/63 | 1.92/3.53/4.65 |
| `count_fixed`, Latin-1 input | 106/77/63/56 | 1.38/1.68/1.89 |
| `detect_fixed`, 3-byte pattern | 44/27/18/16 | 1.63/2.44/2.75 |
| `detect_fixed`, `ignore_case` | 62/35/23/20 | 1.77/2.70/3.10 |
| `replace_all_fixed`, 3-byte pattern | 82/63/50/38 | 1.30/1.64/2.16 |
| `replace_all_fixed`, Latin-1 input | 298/177/117/97 | 1.68/2.55/3.07 |

Latin-1 input is the weakest because `normalize_views()` converts through R's
converter on the main thread, which is required by the threading scope rule.

### Known cost of the design

When the pattern qualifies but a subject refuses late, the threaded path
wastes one direct pass. For `ci_replace_all_fixed` on 1M rows with a single
Latin-1 record in the last position, serial keeps its prefix and takes 182 ms
while two threads take 232 ms (0.78x). The comparable pre-change threaded
number was about 224 ms, so this shape is roughly 3.5% worse than before and
the serial-versus-threaded inversion already existed. Operations that carry
`general_start` through to the general body (count, detect, locate-first) do
not have this cost; only replace and extract discard the prefix.

### Two limits that are not defects

**Per-element R allocation.** `ci_locate_all_fixed` returns one integer matrix
per row. `Rf_allocMatrix` is three R allocations per row — the data vector, a
length-2 `dim` vector, and the attribute pairlist cell `setAttrib` conses —
plus `SET_VECTOR_ELT`'s write barrier. Measured on the same corpus and the
same byte scan, `ci_locate_first_fixed` writes one preallocated 1e6-by-2
matrix in 28 ms, while `ci_locate_all_fixed` builds 1e6 small matrices in 263
ms with the R heap already grown and zero GC. That is 235 ns per row of pure
object construction. Cost is exactly linear in n (240, 236, 230, 230 ns/row at
125k, 250k, 500k, 1M), so it is not a GC blow-up.

GC is a second, separate term that appears whenever the result is large
relative to the heap. In a fresh process the same call costs 474 to 677 ms
with 43 to 54% of that in GC, because a 1M-row result is over 3M traceable
cells that every collection during the build must mark. It falls to zero once
the heap has grown and nothing else is live. Both regimes are real; the
harness measures the cold one and in-process loops measure the warm one, which
is why this operation's absolute numbers vary so widely between protocols.

Either way the materialisation is serial, so the Amdahl ceiling is 263/235,
about 1.12x, against 1.11x measured. `ci_locate_all_fixed` was deliberately
left alone. The same applies to the `extract_all` and `split` families.

**Serial input/output floor.** Reader prefetch and R output allocation stay on
the main thread by scope rule. Sweeping thread counts, `ci_count_fixed` and
`ci_length` both flatten against a floor that is linear in n: about 1 ms at
100,000 rows and 7 to 9 ms at 1,000,000, so roughly 7 to 9 ns per element. Any
kernel not well above that cannot scale far.

## Follow-up: chunked scheduling

Written 2026-08-04, after the fixes above.

`run_parallel` cut the task range into exactly one contiguous slice per
worker, which balances only when every element costs the same. It does not:
one long string outweighs a thousand short ones. On 200,000 rows where 1%
carry 200x the bytes, `str_count(v, regex("\\p{L}+"))` at four threads gives
3.59x shuffled and 1.34x on the same rows sorted by size.

Workers now draw chunks one at a time from a shared ascending cursor.
`ParallelBody::run()` is still called once per worker, so a worker's matcher,
collator or break iterator is still built once and reused across every chunk;
the body's loop is two deep, `while (context.next_chunk())` around the
existing task loop. All 69 bodies were converted.

Ascending order is load-bearing twice: every chunk below the lowest failing
one was necessarily claimed before it, so the failure rule stays exact; and
the chunks left unclaimed when a direct path abandons its attempt are always
the highest ones, so `general_start` still covers them. A body that abandons
must say so with `context.stop_early()`, and the driver throws if the queue is
undrained and nobody did -- that catches a body that forgot to loop.

| layout, 200k rows | static 1/2/4/8 ms | chunked 1/2/4/8 ms |
|---|---|---|
| uniform | 685/364/191/161 | 682/357/188/127 |
| sorted by size | 696/573/519/514 | 686/350/183/125 |
| tail-heavy | 683/580/523/525 | 687/400/217/157 |
| head-heavy | 682/569/512/515 | 685/348/186/125 |

Granularity is 128 chunks per worker with a 256-task floor, both exposed as R
options -- `charr_chunks_per_worker` and `charr_min_chunk`, with accessors
matching `charr_threads()` -- and read natively in `parallel_plan`. Both were measured: at 32 per
worker the skewed case reaches only 1.90x because one chunk holds half the
work, 512 adds nothing, and the cheap kernels do not move across that range.
The floor is clamped to at most one chunk per worker, so a short vector cannot
collapse onto one thread; 200 rows of 26 KB each still reach 3.40x at four
threads. Neither number decides whether threads are used, only how the work is
cut once they are.

Two ordering assumptions had to be rebuilt because worker order is no longer
task order. Four operations staged output into a worker-keyed shard joined by
`io::concat_stores` in worker order; they now key each store by the first task
of its chunk and merge the already-ascending rows, through the new shared
`io::ChunkStores` or a local equivalent. `ci_encoding_conversion` emitted
deferred warnings in worker order and truncated on the first failing worker
index; warnings are now keyed by chunk, merged in task order, and truncated at
the first failing task.

Running the whole suite with the chunk size forced to one element, so every
body claims many chunks even on small test inputs, is what caught the warning
regression -- one failure out of 11,889 assertions. Worth repeating whenever
the scheduler changes.

## Shared machinery as built

`shared::parallel_plan(bool eligible, R_xlen_t tasks, int threads)` has no R
access and no work-size parameter. It applies semantic eligibility and caps
the requested workers by the task count and 256. `resolve_threads()` is the
only native parser. It accepts a positive whole-number integer or double
scalar, rejects malformed direct option values to one worker, and caps valid
values at 256. The final parser checks integrality before capping, so a value
such as 256.5 cannot be mistaken for 256.

`shared::run_parallel()` divides the task interval into contiguous chunks.
Chunk zero runs on the calling thread, the other chunks use
`workers - 1` temporary `std::thread` objects, and a chunk whose thread cannot
be started runs inline. All started threads are joined. Each worker has a
fixed 4096-byte failure report; the lowest failing chunk is raised on the main
thread. `WorkerEntry` is the only callable passed to `std::thread`.

`io::ParallelOutputBuilder` mirrors `io::OutputBuilder` with a leading worker
index on `set()`, `set_validated()`, `set_na()`, and `reserve()`.
`reset(size, workers)` creates charport shards; `release_store()` and
`to_sexp()` keep finalization on the main thread. It uses
`charport::charvec::ParallelBuilder` directly. I did not modify charport or
add the optional `Slot` handle, so the parallel path still pays its shard
bounds check and indirection.

`io::concat_stores()` counts records, copies pointer/length/encoding triples
in worker order, and transfers each slice chain into the combined result.
Payload bytes do not move. Other additions are range-based fixed-search
overloads, sparse conversion records, per-row range/staging containers, and
prefix-limited `DeferredWarnings` emission. There is no second scheduler,
persistent pool, or shared-kernel signature replacement.

The work order allowed separate Group E entrypoints and required measuring
both shapes before choosing. I retained combined staged entrypoints and did
not build a separate-entrypoint A/B. This avoids an extra dispatch surface,
but it is a clear protocol deviation. Four locate-all operations also use one
native range vector per output row, adding O(output length) native vector
objects. Peak RSS for the retained row-range and worker-store shapes is
reported below. The alternative separate-entrypoint shape was not built, so
there is no A/B memory comparison against it.

## Semantics

No worker accesses R, a `charport::Reader`, an R allocator, or a warning API.
Inputs are prefetched and normalized on the main thread. Workers own matchers,
collators, break iterators, converters, and scratch state on their `run()`
stacks. State that must outlive the region, including output builders and
shards, is Frame-owned. R allocation and materialization stay on the main
thread: charvec and list outputs finalize after the join, while preallocated
numeric, logical, and matrix outputs receive disjoint native writes from
workers.

Lane-vectorized searches split by lane, preserving matcher reuse. Detect with
a finite `max_count` remains serial because its stopping counter is
loop-carried. Regex locate-first with captures remains serial because it
allocates per element. Those are correctness gates, not profitability
heuristics.

`ci_locate_first_regex` needed more warning machinery than the work order
anticipated. Empty patterns warn per visited output, while a later invalid
pattern can stop the serial loop. Workers now keep failure slots and warning
counts are replayed only through the lowest failing chunk. Wrap normalization,
substring replacement-all validation, and native encode preconversion use
the same successful-prefix idea so an earlier prefix condition still wins.

There are three remaining condition-order differences to know about:

1. Group C prepasses, plus `ci_reverse`, can discover a later native/Latin-1
   conversion failure before an earlier worker kernel would fail.
2. Sequential `replace_all_{fixed,regex,coll}` and character-class replacement
   split subjects. The serial code is pattern-major, so adversarial
   matcher/resource failures can be encountered in a different order.
3. Encoding completes worker transcodes before
   `finish_native_output()`. A later worker ICU transcode failure can therefore
   precede an earlier native-output conversion failure. The analogous
   native-input-prefix case was fixed, but this output-phase ordering remains.

Normal values, missingness, recycling, output order, shapes, encodings, and
attributes match the serial/stringi behavior in the full suite and benchmark
preflights. Worker resource-construction failures can occur at a different
setup point than the former single resource construction. One exception is
`ci_locate_first_coll`, which retains its original main-thread collator
initialization check before opening worker collators.

Encoding uses independent source and target `StriUcnv` objects on each worker
stack rather than cloning ICU handles. Both native entries keep the original
`to_raw` coercion, so numeric 1, strings, long vectors, and objects with
coercion methods retain compatibility. Consequently, the R routing is
nominal: both `ci_encode_string` and `ci_encode_raw` can return either the
Group B string shape or Group E raw-list shape. Raw-list assembly protects the
outer VECSXP while allocating children.

## Performance and memory

The strongest four-thread results include character-class replacement
(3.25x), regex count (3.57x), regex replace-all (3.53x), collation detect
(3.45x), title case (3.61x), boundary count (3.71x), width (3.40x), and flat
wrap (3.00x). Eight threads improve several heavier kernels further, but many
are already limited
by memory bandwidth, worker setup, or serial materialization.

The clearest forced-parallel regressions are fixed count at two threads
(0.57x), `ci_length` at four (0.83x), and
`ci_sub_replacement_all` at four (0.37x). The last operation pays a required
serial validation/warning pass before parallel staging. These results are
reported rather than hidden behind the removed thresholds.

> **Superseded 2026-08-04.** None of those three was forced-parallel overhead.
> Fixed count lost its ASCII byte path to a `plan.workers == 1` gate,
> `ci_length` ran its worker scan a second time serially, and
> `ci_sub_replacement_all` prepared its scalar replacement through R once per
> element. All three are fixed; see the follow-up section.

`measure-parallel-memory.R` was run under `/usr/bin/time` in three independent
processes per condition. The table reports median peak RSS. The Group C probe
uses 500,000 distinct, explicitly Latin-1-marked 96-byte strings in an
unmaterialized charvec. The two Group E probes use 300,000 rows with four fixed
matches per row.

| probe | main, 1 thread | current, 1 thread | current, 4 threads | four vs current one |
|---|---:|---:|---:|---:|
| Group C sparse prepass (`ci_width`) | 182,072 KiB | 182,044 KiB | 243,544 KiB | +61,500 KiB / +60.059 MiB / +33.78% |
| Group E row ranges (`ci_locate_all_fixed`) | 172,912 KiB | 173,116 KiB | 192,416 KiB | +19,300 KiB / +18.848 MiB / +11.15% |
| Group E worker stores (`ci_extract_all_fixed`) | 103,480 KiB | 103,640 KiB | 108,260 KiB | +4,620 KiB / +4.512 MiB / +4.46% |

At one thread, current-minus-main was -28 KiB for the Group C probe, +204 KiB
for row ranges, and +160 KiB for worker stores. The Latin-1 fixture SHA-256 is
`d1be8734054fed332e73e3c5fa83b222c26042e5921dd1534eab8a7263cec288`.
The 27 raw RSS observations and their summaries are retained in
`parallel-memory-{times,summary}.csv`.

The added R-level `getOption()` cost was not isolated with a microbenchmark.
It is present in every current ALTREP serial observation, but the harness's
millisecond resolution and kernel work do not separate it from native-path
changes. This is another explicit departure from section 10.

## Benchmark artifacts

The retained runs use R 4.6.1, seed 20260721, fixture format 2, main
`6bf27c8`, and timed kernel commit `78e8c82`. Charr used system ICU 78.2;
stringi used bundled ICU 74.1. The main baseline has 1,005 data rows, the
post-change serial run has 1,005, each targeted serial audit has 270, the
thread scaling run has 1,340, the supplement has 210, and the RSS run has 27.
All 67 public operations passed exact output preflight at 1/2/4/8 threads;
the six supplement shapes passed against main and stringi.

Retained files:

- `pre-parallel-main-times.csv` and `pre-parallel-main-summary.csv`
- `post-parallel-final-times.csv` and `post-parallel-final-summary.csv`
- `serial-audit-{main,current}-times.csv` and the matching summaries
- `parallel-final-clean-parallel-times.csv` and
  `parallel-final-clean-parallel-summary.csv`
- `parallel-supplement-clean-supplement-times.csv` and
  `parallel-supplement-clean-supplement-summary.csv`
- `parallel-memory-times.csv` and `parallel-memory-summary.csv`
- `parallel-final-clean-parallel-threads.png` and
  `parallel-final-clean-parallel-threads.pdf`
- `parallel-fixes-20260804-parallel-times.csv` and
  `parallel-fixes-20260804-parallel-summary.csv` (the 2026-08-04 re-run of the
  17 operations in the follow-up section)
- `parallel-fixes-20260804b-parallel-times.csv` and
  `parallel-fixes-20260804b-parallel-summary.csv` (`ci_replace_na` and
  `ci_sub_replacement_all` after the per-worker-builder change, with
  `ci_count_regex` as the control)

The plot uses the existing theme. Four threads are the requested red-ish
vermilion `#D55E00`; two and eight threads remain blue.

The timed install predates only `ded0b66`, the malformed-option parser fix.
Valid integer requests 1/2/4/8 take the same parser and scheduler path, so the
fix does not change the benchmarked hot paths.

## Linter and code map

The reviewed effect additions cover charport's stable
`ParallelBuilder`/`Store` surface, `SliceChain::prepend`, concrete STL
containers, ICU objects, and const accessors used by workers. Every key has a
stable qualified name and canonical type. There are no lambda/source-location
keys, dependent calls, `Rf_GetOption1` rows, or threshold helpers.

The final checks passed:

- `make lint-r-literals lint-fixtures lint-frontier`
- `make lint-converted` for all 138 converted translation units
- `make code-map` followed by validation: 138 units, 141 entrypoints,
  141 ABI shims, 145 registration references, and 0 recursive edges

## Tests

`make test TEST_ALTREP_THREADS=1,4` ran stringi and base once per locale and
the entire ALTREP suite at both requested thread counts. There were no
failures or warnings.

| startup locale / backend | imported assertions | common assertions | skips |
|---|---:|---:|---:|
| UTF-8 / stringi | 492 | 9,888 | 8 |
| UTF-8 / base | 492 | 9,941 | 5 |
| UTF-8 / ALTREP, 1 thread | 492 | 11,889 | 6 |
| UTF-8 / ALTREP, 4 threads | 492 | 11,889 | 6 |
| ISO-8859-1 / stringi | 492 | 9,795 | 19 |
| ISO-8859-1 / base | 492 | 9,854 | 15 |
| ISO-8859-1 / ALTREP, 1 thread | 492 | 11,802 | 16 |
| ISO-8859-1 / ALTREP, 4 threads | 492 | 11,802 | 16 |

The focused accessor test covers accepted writes and direct malformed option
values, including NA, NaN, infinity, fractional values below and above the
worker cap, vectors, empty values, and nonnumeric values. It is deliberately
not a matrix of multithreading tests.

Manual probes covered regex warning prefixes, failures in separate chunks,
positional substring replacement, noncanonical `to_raw` coercions, Latin-1
custom objects, forced GC during raw assembly, and Shift-JIS encode condition
precedence. Independent Group D/E, encoding, scheduler, and final source
reviews found no data race, worker-side R access, output-order defect, ABI
mismatch, or omitted converted entrypoint.

## Open items and second-opinion points

1. If a formal section-10 serial verdict is required, repeat the complete
   before/after comparison under quieter conditions. The retained run is
   complete, but its base controls are too mobile for that claim.
2. If Group E dispatch surface becomes important, build and measure the
   allowed separate-entrypoint shape. RSS is recorded for the retained
   combined shapes, but the alternative was not built for an A/B comparison.
3. The three condition-order differences in the semantics section are real.
   Removing them would require serializing conversion/planning or adding more
   phase-aware replay, which would substantially reduce the requested
   parallelism.
4. If the project later wants profitability policy, put it above this
   mechanism and make it explicit to the user. The implementation here
   intentionally contains no hidden threshold and honors an explicit request
   even for the measured regressions.

Added 2026-08-04, after the follow-up work:

5. `builder_detail::Shard` in charport is 24 bytes, so `ParallelBuilder`
   packs two or three shards per cache line and every operation using
   `io::ParallelOutputBuilder` pays about 8 ns per record of false sharing.
   `ci_replace_na` was worked around in charr by switching to per-worker
   builders, but the general fix is `alignas(64)` on that struct in charport,
   which would recover the same cost everywhere at 40 bytes per worker.
7. The `serial delta` column was not re-measured after the follow-up work. The
   one-thread medians in the re-run moved by at most one millisecond against
   `parallel-final-clean`, but that is a different comparison from the
   main-versus-current one the column describes.
8. `ci_replace_{first,all}_fixed` and `ci_extract_first_fixed` discard the
   whole direct attempt when a subject refuses, because their parallel general
   bodies rebuild `[0, n)` and take no start index. Giving those bodies a begin
   offset would let them keep the direct prefix the way count, detect and
   locate-first already do. Worth roughly 180 ms against 232 ms on a 1M-row
   input whose last record is Latin-1; irrelevant on clean input.
9. `ci_locate_all_fixed`, `ci_extract_all_fixed` and the split family are
   capped by one R allocation per output row on the main thread. Raising that
   ceiling means changing the output shape, not the threading.
10. The `parallel speedup` column predates the chunked scheduler. The numbers
   there are still the right ordering, but every row measured on the tatoeba
   corpus improves again at eight threads under chunking, so the column is
   conservative rather than wrong.
11. Three local chunk-ordering helpers survive in `ci_replace_na`,
   `ci_search_class_trim` and `ci_search_fixed_extract`, written before
   `io::ChunkStores` existed. They should be folded onto it.
