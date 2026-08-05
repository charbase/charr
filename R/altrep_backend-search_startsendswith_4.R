# Copied from stringi; stri_* renamed to ci_*.
# kate: default-dictionary en_US

## This file is part of the 'stringi' package for R.
## Copyright (c) 2013-2025, Marek Gagolewski <https://www.gagolewski.com/>
## All rights reserved.
##
## Redistribution and use in source and binary forms, with or without
## modification, are permitted provided that the following conditions are met:
##
## 1. Redistributions of source code must retain the above copyright notice,
## this list of conditions and the following disclaimer.
##
## 2. Redistributions in binary form must reproduce the above copyright notice,
## this list of conditions and the following disclaimer in the documentation
## and/or other materials provided with the distribution.
##
## 3. Neither the name of the copyright holder nor the names of its
## contributors may be used to endorse or promote products derived from
## this software without specific prior written permission.
##
## THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
## 'AS IS' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
## BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
## FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
## HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
## SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
## PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
## OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
## WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
## OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
## EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


# @title
# Determine if the Start or End of a String Matches a Pattern
#
# @description
# These functions check if a string starts or ends with a match
# to a given pattern. Also, it is possible to check if there is a match
# at a specific position.
#
# @details
# Vectorized over \code{str}, \code{pattern},
# and \code{from} or \code{to} (with recycling
# of the elements in the shorter vector if necessary).
#
# If \code{pattern} is empty, then the result is \code{NA}
# and a warning is generated.
#
# Argument \code{start} controls the start position in \code{str}
# where there is a match to a \code{pattern}.
# \code{to} gives the end position.
#
# Indexes given by \code{from} or \code{to} are of course 1-based,
# i.e., an index 1 denotes the first character
# in a string. This gives a typical R look-and-feel.
#
# For negative indexes in \code{from} or \code{to}, counting starts
# at the end of the string. For instance, index -1 denotes the last code point
# in the string.
#
# If you wish to test for a pattern match at an arbitrary
# position in \code{str}, use \code{\link{ci_detect}}.
#
# \code{ci_startswith} and \code{ci_endswith} are convenience functions.
# They call either \code{ci_*_fixed}, \code{ci_*_coll},
# or \code{ci_*_charclass}, depending on the argument used.
# Relying on these underlying functions directly will make your code run
# slightly faster.
#
# Note that testing for a pattern match at the start or end of a string
# has not been implemented separately for regex patterns.
# For that you may use the '\code{^}' and '\code{$}' meta-characters,
# see \link{stringi-search-regex}.
#
# @param str character vector
# @param pattern,fixed,coll,charclass character vector defining search patterns;
# for more details refer to \link{stringi-search}
# @param from integer vector
# @param to integer vector
# @param negate single logical value; whether a no-match to a pattern
#     is rather of interest
# @param opts_collator,opts_fixed a named list used to tune up
# the search engine's settings; see \code{\link{ci_opts_collator}}
# and \code{\link{ci_opts_fixed}}, respectively; \code{NULL}
# for the defaults
# @param ... supplementary arguments passed to the underlying functions,
# including additional settings for \code{opts_collator}, \code{opts_fixed},
# and so on.
#
# @return Each function returns a logical vector.
#
#
# @examples
# ci_startswith_charclass(' trim me! ', '\\p{WSpace}')
# ci_startswith_fixed(c('a1', 'a2', 'b3', 'a4', 'c5'), 'a')
# ci_detect_regex(c('a1', 'a2', 'b3', 'a4', 'c5'), '^a')
# ci_startswith_fixed('ababa', 'ba')
# ci_startswith_fixed('ababa', 'ba', from=2)
# ci_startswith_coll(c('a1', 'A2', 'b3', 'A4', 'C5'), 'a', strength=1)
# pat <- ci_paste('\u0635\u0644\u0649 \u0627\u0644\u0644\u0647 ',
#                   '\u0639\u0644\u064a\u0647 \u0648\u0633\u0644\u0645XYZ')
# ci_endswith_coll('\ufdfa\ufdfa\ufdfaXYZ', pat, strength=1)
#
# @family search_detect
# @export
# @rdname ci_startsendswith


# @export
# @rdname ci_startsendswith


# @export
# @rdname ci_startsendswith
ci_startswith_fixed <- function(str, pattern, from = 1L,
    negate = FALSE, ..., opts_fixed = NULL)
{
    if (!missing(...))
        opts_fixed <- do.call(ci_opts_fixed, as.list(c(opts_fixed, ...)))
    .Call(
        C_ci_startswith_fixed, str, pattern, from, negate, opts_fixed
    )
}


# @export
# @rdname ci_startsendswith
ci_endswith_fixed <- function(str, pattern, to = -1L,
    negate = FALSE, ..., opts_fixed = NULL)
{
    if (!missing(...))
        opts_fixed <- do.call(ci_opts_fixed, as.list(c(opts_fixed, ...)))
    .Call(
        C_ci_endswith_fixed, str, pattern, to, negate, opts_fixed
    )
}


# @export
# @rdname ci_startsendswith


# @export
# @rdname ci_startsendswith


# @export
# @rdname ci_startsendswith
ci_startswith_coll <- function(str, pattern, from = 1L,
    negate = FALSE, ..., opts_collator = NULL)
{
    if (!missing(...))
        opts_collator <- do.call(ci_opts_collator, as.list(c(opts_collator, ...)))
    .Call(
        C_ci_startswith_coll, str, pattern, from, negate, opts_collator
    )
}


# @export
# @rdname ci_startsendswith
ci_endswith_coll <- function(str, pattern, to = -1L,
    negate = FALSE, ..., opts_collator = NULL)
{
    if (!missing(...))
        opts_collator <- do.call(ci_opts_collator, as.list(c(opts_collator, ...)))
    .Call(
        C_ci_endswith_coll, str, pattern, to, negate, opts_collator
    )
}
