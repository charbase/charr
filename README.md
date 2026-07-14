# charr

> **Status: experiment.** charr explores what stringr-style string
> manipulation looks like when ALTREP vectors are first-class. Expect the
> interface to be stable (it is stringr's) and everything underneath it to
> move.

charr is a hard fork of [stringr](https://github.com/tidyverse/stringr)
with an ALTREP-aware native string engine built on
[charport](https://github.com/traversc/charport).

- **Backend switch off (default):** every operation delegates to
  [stringi](https://stringi.gagolewski.com/), exactly as stringr does.
  Behavior is identical to stringr, verified by running stringr's own test
  suite.
- **Backend switch on:** eligible operations read ALTREP character vectors
  through the charport interface without materializing them, and return
  ALTREP `charvec` results. Anything the native engine cannot do with
  identical semantics falls back to stringi.

The function surface is stringr's — same names, same arguments, same
semantics. See [stringr's documentation](https://stringr.tidyverse.org)
for usage; charr's own documentation covers only what charr changes.

## Credit and license

charr is derived from stringr by Hadley Wickham and the stringr authors
(MIT). The stringi backend is by Marek Gagolewski. MIT licensed; see
`LICENSE.md` for the combined notices.

*The work in this package is funded by the R Consortium Infrastructure
Steering Committee.*
