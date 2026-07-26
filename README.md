# charr


<img src="man/figures/logo.svg" alt="charr logo" align="right" width="160"/>

<a href="https://github.com/charport/charr/actions"><img src="https://github.com/charport/charr/actions/workflows/R-CMD-check.yaml/badge.svg" alt="R-CMD-check status"/></a>

## charr: string processing reimagined for ALTREP strings

`charr` is a fork of `stringr` reimagined for ALTREP strings. The
function and semantics are the same but everything is optimized around
ALTREP.

ALTREP is R’s mechanism for letting a vector define its own layout, and
many packages already use it internally to store data more efficiently.
For string data, the right layout is worth a lot: order-of-magnitude
performance improvements are possible.

`charr` reimplements `stringr`’s entire function surface and provides
three different backends: the `stringi` reference (used by `stringr`),
an optimized `base` implementation that returns ordinary character
vectors, and the default `altrep` implementation that returns ALTREP
strings. The three backends are semantically interchangeable but on
average the ALTREP backend can be much faster.

*This work is supported by the R Consortium Infrastructure Steering
Committee, under the grant Universal ALTREP Interoperability for
Strings.*

## Benchmark

The figure below shows thirteen representative operations measured on a
multilingual [Tatoeba](https://tatoeba.org/) corpus, from 1,000,000
records for the cheapest operations down to 10,000 for the most
expensive. Each bar is the median of five runs, each in a fresh R
process, and its length is how many times faster `charr` is than the
reference.

![Horizontal bar chart of thirteen representative operations. For each
one, a bar for charr’s base backend and a bar for its ALTREP backend
show how many times faster they are than the stringi reference.
str_read_lines leads at 13.6 times, followed by str_dup, str_c, str_sub
and str_trim between 8 and 11 times. str_detect with collation is last
at 1.1 times.](man/figures/bench-summary.png)

`str_read_lines()` leads at 13.6×: a second of work becomes 76 ms. Below
it the ordering follows how much of each operation’s time goes into
producing strings rather than inspecting them. `str_detect()` with
collation sits at the bottom at 1.1×; it returns `TRUE`/`FALSE`, so
there is no string output to improve on.

This is a sample rather than the whole surface. [Under the
hood](https://charport.github.io/charr/articles/under-the-hood.html) has
the complete record: all 67 operations, grouped by family.

## Choosing a backend

`charr` picks a backend before a public call starts. The default is
`altrep`:

``` r
charr_backend()                 # "altrep"
old <- charr_backend("base")    # ordinary character vectors
charr_backend("stringi")        # the original stringr implementation
charr_backend(old)
```

The selection lives in the `charr_backend` option, so
`getOption("charr_backend")` reads it and `withr::local_options()`
scopes it to a block. `charr_backend()` is the recommended way to set
it, since it validates the value on the spot and returns `"altrep"`
rather than `NULL` when the option has never been set.

`base` runs the same optimized native code but writes ordinary character
vectors. It is useful when a downstream package cannot read ALTREP
strings, and as a control in measurements: the gap between `base` and
`altrep` is the representation, not the algorithm.

Under `altrep`, passing one `charr` call’s output into the next keeps
the data in ALTREP form the whole way; nothing materializes until
something outside `charr` asks for ordinary strings.

## Additional functions

`charr` includes a few functions `stringr` does not have, and more may
be added over time.

- `str_reverse()` — reverses each string by Unicode code point, so
  combining sequences and astral characters survive the trip.
- `str_read_lines()` — reads a file, converts it to UTF-8, and splits it
  at Unicode line boundaries. It is the fastest way to get text into
  `charr`.

## See also

- [Under the
  hood](https://charport.github.io/charr/articles/under-the-hood.html):
  the three backends, the ICU and C++ choices, and the full
  per-operation benchmark.
- [charport](https://charport.github.io/charport/): the ALTREP string
  interoperability layer `charr` is built on.
