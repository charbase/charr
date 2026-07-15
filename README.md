# charr

> **Status: experiment.** charr explores what stringr-style string
> manipulation looks like when ALTREP vectors are first-class. Expect the
> interface to be stable (it is stringr's) and everything underneath it to
> move.

charr is a hard fork of [stringr](https://github.com/tidyverse/stringr). Its
private backend starts as a direct copy of stringi's R and C++ implementation
and will be adapted to read and return ALTREP character vectors through
[charport](https://github.com/traversc/charport).

- **Backend switch off (default):** every operation delegates to
  [stringi](https://stringi.gagolewski.com/), exactly as stringr does.
  Behavior is identical to stringr, verified by running stringr's own test
  suite.
- **Backend switch on:** all 63 string-processing entry points and four option
  constructors route to charr's private copy of stringi. This initial scaffold
  still materializes ordinary R character vectors; it establishes an exact
  semantic baseline before the containers are changed to charport/charvec.

The intended completed backend-altrep covers the entire public API with
stringi-equivalent semantics and works without stringi being installed.

The function surface is stringr's — same names, same arguments, same
semantics. See [stringr's documentation](https://stringr.tidyverse.org)
for usage; charr's own documentation covers only what charr changes.

## Credit and license

charr is derived from stringr by Hadley Wickham and the stringr authors
(MIT). Its private backend is copied from stringi by Marek Gagolewski
(BSD-3-Clause). See `inst/COPYRIGHTS` for the complete notices.

*The work in this package is funded by the R Consortium Infrastructure
Steering Committee.*
