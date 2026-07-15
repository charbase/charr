# Copied from stringi 19e9586ba39b3320df49355e32bd18d74ed6098f; stri_* renamed to ci_*.
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
# Detect Pattern Occurrences
#
# @description
# These functions determine, for each string in \code{str},
# if there is at least one match to a corresponding \code{pattern}.
#
# @details
# Vectorized over \code{str} and \code{pattern} (with recycling
# of the elements in the shorter vector if necessary). This allows to,
# for instance, search for one pattern in each given string,
# search for each pattern in one given string,
# and search for the i-th pattern within the i-th string.
#
# If \code{pattern} is empty, then the result is \code{NA}
# and a warning is generated.
#
# \code{ci_detect} is a convenience function.
# It calls either \code{ci_detect_regex},
# \code{ci_detect_fixed}, \code{ci_detect_coll},
# or \code{ci_detect_charclass}, depending on the argument used.
#
# See also \code{\link{ci_startswith}} and \code{\link{ci_endswith}}
# for testing whether a string starts or ends with a match to a given pattern.
# Moreover, see \code{\link{ci_subset}} for a character vector subsetting.
#
# If \code{max_count} is negative, then all stings are examined.
# Otherwise, searching terminates
# once \code{max_count} matches (or, if \code{negate} is \code{TRUE},
# no-matches) are detected. The uninspected cases are marked
# as missing in the return vector. Be aware that, unless \code{pattern} is a
# singleton, the elements in \code{str} might be inspected in a
# non-consecutive order.
#
#
# @param str character vector; strings to search in
# @param pattern,regex,fixed,coll,charclass character vector;
#     search patterns; for more details refer to \link{stringi-search}
# @param negate single logical value; whether a no-match to a pattern
#     is rather of interest
# @param max_count single integer; allows to stop searching once a given
#    number of occurrences is detected; \code{-1} (the default) inspects all
#    elements
# @param opts_collator,opts_fixed,opts_regex a named list used to tune up
#    the search engine's settings; see
#    \code{\link{ci_opts_collator}}, \code{\link{ci_opts_fixed}},
#    and \code{\link{ci_opts_regex}}, respectively; \code{NULL}
#    for the defaults
# @param ... supplementary arguments passed to the underlying functions,
# including additional settings for \code{opts_collator}, \code{opts_regex},
# \code{opts_fixed}, and so on
#
# @return Each function returns a logical vector.
#
# @examples
# ci_detect_fixed(c('stringi R', 'R STRINGI', '123'), c('i', 'R', '0'))
# ci_detect_fixed(c('stringi R', 'R STRINGI', '123'), 'R')
#
# ci_detect_charclass(c('stRRRingi','R STRINGI', '123'),
#    c('\\p{Ll}', '\\p{Lu}', '\\p{Zs}'))
#
# ci_detect_regex(c('stringi R', 'R STRINGI', '123'), 'R.')
# ci_detect_regex(c('stringi R', 'R STRINGI', '123'), '[[:alpha:]]*?')
# ci_detect_regex(c('stringi R', 'R STRINGI', '123'), '[a-zC1]')
# ci_detect_regex(c('stringi R', 'R STRINGI', '123'), '( R|RE)')
# ci_detect_regex('stringi', 'STRING.', case_insensitive=TRUE)
#
# ci_detect_regex(c('abc', 'def', '123', 'ghi', '456', '789', 'jkl'),
#    '^[0-9]+$', max_count=1)
# ci_detect_regex(c('abc', 'def', '123', 'ghi', '456', '789', 'jkl'),
#    '^[0-9]+$', max_count=2)
# ci_detect_regex(c('abc', 'def', '123', 'ghi', '456', '789', 'jkl'),
#    '^[0-9]+$', negate=TRUE, max_count=3)
#
# @family search_detect
# @export
# @rdname ci_detect
ci_detect <- function(str, ..., regex, fixed, coll, charclass)
{
    providedarg <- c(
        regex = !missing(regex),
        fixed = !missing(fixed),
        coll = !missing(coll),
        charclass = !missing(charclass))

    if (sum(providedarg) != 1)
        stop("you have to specify one of: `regex`, `fixed`, `coll`, or `charclass`")

    if (providedarg["regex"])
        ci_detect_regex(str, regex, ...) else if (providedarg["fixed"])
        ci_detect_fixed(str, fixed, ...) else if (providedarg["coll"])
        ci_detect_coll(str, coll, ...) else if (providedarg["charclass"])
        ci_detect_charclass(str, charclass, ...)
}


# @export
# @rdname ci_detect
ci_detect_fixed <- function(
    str, pattern, negate=FALSE, max_count=-1, ...,
    opts_fixed=NULL
) {
    if (!missing(...))
        opts_fixed <- do.call(ci_opts_fixed, as.list(c(opts_fixed, ...)))
    .Call(C_ci_detect_fixed, str, pattern, negate, max_count, opts_fixed)
}


# @export
# @rdname ci_detect
ci_detect_charclass <- function(str, pattern, negate = FALSE, max_count = -1)
{
    .Call(C_ci_detect_charclass, str, pattern, negate, max_count)
}


# @export
# @rdname ci_detect
ci_detect_coll <- function(
    str, pattern, negate = FALSE,
    max_count = -1, ..., opts_collator = NULL
) {
    if (!missing(...))
        opts_collator <- do.call(ci_opts_collator, as.list(c(opts_collator, ...)))
    .Call(C_ci_detect_coll, str, pattern, negate, max_count, opts_collator)
}


# @export
# @rdname ci_detect
ci_detect_regex <- function(
    str, pattern, negate = FALSE,
    max_count = -1, ...,
    opts_regex = NULL
) {
    if (!missing(...))
        opts_regex <- do.call(ci_opts_regex, as.list(c(opts_regex, ...)))
    .Call(C_ci_detect_regex, str, pattern, negate, max_count, opts_regex)
}
