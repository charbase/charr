# charr


<img src="man/figures/logo.svg" alt="charr logo" align="right" width="160"/>

<a href="https://github.com/charbase/charr/actions"><img src="https://github.com/charbase/charr/actions/workflows/R-CMD-check.yaml/badge.svg" alt="R-CMD-check status"/></a>

## charr: string processing reimagined for ALTREP strings

`charr` is an experimental fork of `stringr` reimagined for ALTREP
strings. The functions and semantics are the same as `stringr` but
everything is optimized around ALTREP.

ALTREP is R’s mechanism for letting a vector define its own layout, and
many packages already use it internally to store data more efficiently.
For string data, the right layout is worth a lot: order-of-magnitude
performance improvements are possible.

`charr` reimplements `stringr`’s entire API and provides three different
backends: the `stringi` reference, an optimized `base` implementation
that returns ordinary character vectors, and the default `altrep`
implementation that returns ALTREP strings. The three backends are
semantically interchangeable but on average the ALTREP backend can be
much faster.

*This work is supported by the R Consortium Infrastructure Steering
Committee, under the grant Universal ALTREP Interoperability for
Strings.*

## Licensing

Charr’s original work and the material derived from stringr are
distributed under the MIT License. Code copied or adapted from stringi
remains under the BSD 3-Clause License. Bundled ICU4C source and data
retain the Unicode License v3 and ICU’s additional component licenses.

The repository’s [licensing and copyright notice](LICENSE.note) contains
the complete MIT license and explains the component boundaries. The
[installed aggregate notice](inst/COPYRIGHTS) supplies the complete
stringr and stringi terms and points to ICU’s full notices. No Tatoeba
benchmark data is included in the repository or package.

## Installation

`charr` is built on [charport](https://github.com/charbase/charport),
which is not on CRAN yet, so install it first:

``` r
# install.packages("remotes")
remotes::install_github("charbase/charport")
remotes::install_github("charbase/charr")
```

`charr` bundles ICU4C and builds it from source when no allowlisted
system ICU is present, so the first install takes a while.

## Benchmark

The figure below shows thirteen representative operations measured on a
multilingual [Tatoeba](https://tatoeba.org/) corpus, from 1,000,000
records for the cheapest operations down to 10,000 for the most
expensive. Each bar is the median of five runs, each in a fresh R
process, and its length is how many times faster `charr` is than the
reference.

![](man/figures/bench-summary.png)

`str_read_lines()` leads at 13.8×: a second of work becomes 72 ms. Below
it the ordering follows how much of each operation’s time goes into
producing strings rather than inspecting them. `str_detect()` with
collation sits at the bottom at 1.1×; it returns `TRUE`/`FALSE`, so
there is no string output to improve on.

This is a sample rather than the whole surface. [Under the
hood](https://charbase.github.io/charr/articles/under-the-hood.html) has
the complete record: all 67 operations, grouped by family.

## Choosing a backend

`charr` picks a backend before a public call starts. The default is
`altrep`:

``` r
charr_backend()                 # returns current value, default "altrep"
prev <- charr_backend("base")    # switch to ordinary character vectors, store previous value
charr_backend("stringi")        # switch to the original stringr implementation
```

Under `altrep`, passing one `charr` call’s output into the next keeps
the data in ALTREP form the whole way; nothing materializes until
something outside `charr` asks for ordinary strings.

`base` runs the same optimized code but writes ordinary character
vectors. `stringi` is the reference, also used directly by `stringr`.

The `charr_backend` selection is stored as an option so you can retrieve
it with `getOption("charr_backend")`.

## Additional functions

`charr` includes a few functions `stringr` does not have, and more may
be added over time.

- `str_reverse()` reverses each string by Unicode code point
- `str_read_lines()` reads a file, converts it to UTF-8, and splits it
  at Unicode line boundaries. It is the fastest way to get text into
  `charr`

## See also

- [Under the
  hood](https://charbase.github.io/charr/articles/under-the-hood.html):
  the three backends, the ICU and C++ choices, and the full
  per-operation benchmark.
- [Code map](https://charbase.github.io/charr/code-map/): an interactive
  view of the native source graph, generated from Clang’s semantic
  model.
- [charport](https://charbase.github.io/charport/): the ALTREP string
  interoperability layer `charr` is built on.
