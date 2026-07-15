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
# Select Elements that Match a Given Pattern
#
# @description
# These functions return or modify a sub-vector where there is a match to
# a given pattern. In other words, they
# are roughly equivalent (but faster and easier to use) to a call to
# \code{str[\link{ci_detect}(str, ...)]} or
# \code{str[\link{ci_detect}(str, ...)] <- value}.
#
# @details
# Vectorized over \code{str} as well as partially over \code{pattern}
# and \code{value},
# with recycling of the elements in the shorter vector if necessary.
# As the aim here is to subset \code{str}, \code{pattern}
# cannot be longer than the former. Moreover, if the number of
# items to replace is not a multiple of length of \code{value},
# a warning is emitted and the unused elements are ignored.
# Hence, the length of the output will be the same as length of \code{str}.
#
# \code{ci_subset} and \code{ci_subset<-} are convenience functions.
# They call either \code{ci_subset_regex},
# \code{ci_subset_fixed}, \code{ci_subset_coll},
# or \code{ci_subset_charclass},
# depending on the argument used.
#
# @param str character vector; strings to search within
#
# @param pattern,regex,fixed,coll,charclass character vector;
#     search patterns (no more than the length of \code{str});
#     for more details refer to \link{stringi-search}
#
# @param negate single logical value; whether a no-match is rather of interest
#
# @param omit_na single logical value; should missing values be excluded
# from the result?
#
# @param opts_collator,opts_fixed,opts_regex a named list used to tune up
# the search engine's settings; see
# \code{\link{ci_opts_collator}}, \code{\link{ci_opts_fixed}},
# and \code{\link{ci_opts_regex}}, respectively; \code{NULL}
# for the defaults
#
# @param ... supplementary arguments passed to the underlying functions,
#     including additional settings for \code{opts_collator}, \code{opts_regex},
#     \code{opts_fixed}, and so on
#
# @param value non-empty character vector of replacement strings;
#     replacement function only
#
#
# @return The \code{ci_subset_*} functions return a character vector.
# As usual, the output encoding is UTF-8.
#
# The \code{ci_subset_*<-} functions modifies \code{str} 'in-place'.
#
#
# @examples
# ci_subset_regex(c('stringi R', '123', 'ID456', ''), '^[0-9]+$')
#
# x <- c('stringi R', '123', 'ID456', '')
# `ci_subset_regex<-`(x, '[0-9]+$', negate=TRUE, value=NA)  # returns a copy
# ci_subset_regex(x, '[0-9]+$') <- NA  # modifies `x` in-place
# print(x)
#
# @family search_subset
# @export
# @rdname ci_subset
ci_subset <- function(str, ..., regex, fixed, coll, charclass)
{
    providedarg <- c(
        regex = !missing(regex),
        fixed = !missing(fixed),
        coll = !missing(coll),
        charclass = !missing(charclass))

    if (sum(providedarg) != 1)
        stop("you have to specify either `regex`, `fixed`, `coll`, or `charclass`")

    if (providedarg["regex"])
        ci_subset_regex(str, regex, ...)
    else if (providedarg["fixed"])
        ci_subset_fixed(str, fixed, ...)
    else if (providedarg["coll"])
        ci_subset_coll(str, coll, ...)
    else if (providedarg["charclass"])
        ci_subset_charclass(str, charclass, ...)
}


# @export
# @rdname ci_subset
# @usage ci_subset(str, ..., regex, fixed, coll, charclass) <- value
`ci_subset<-` <- function(str, ..., regex, fixed, coll, charclass, value)
{
    providedarg <- c(
        regex = !missing(regex),
        fixed = !missing(fixed),
        coll = !missing(coll),
        charclass = !missing(charclass))

    if (sum(providedarg) != 1)
        stop("you have to specify either `regex`, `fixed`, `coll`, or `charclass`")

    if (providedarg["regex"])
        `ci_subset_regex<-`(str, regex, ..., value = value)
    else if (providedarg["fixed"])
        `ci_subset_fixed<-`(str, fixed, ..., value = value)
    else if (providedarg["coll"])
        `ci_subset_coll<-`(str, coll, ..., value = value)
    else if (providedarg["charclass"])
        `ci_subset_charclass<-`(str, charclass, ..., value = value)
}


# @export
# @rdname ci_subset
ci_subset_fixed <- function(str, pattern, omit_na = FALSE, negate = FALSE, ...,
    opts_fixed = NULL)
{
    if (!missing(...))
        opts_fixed <- do.call(ci_opts_fixed, as.list(c(opts_fixed, ...)))
    .Call(C_ci_subset_fixed, str, pattern, omit_na, negate, opts_fixed)
}


# @export
# @rdname ci_subset
# @usage ci_subset_fixed(str, pattern, negate=FALSE, ..., opts_fixed=NULL) <- value
`ci_subset_fixed<-` <- function(str, pattern, negate = FALSE, ...,
    opts_fixed = NULL,  value)
{
    if (!missing(...))
        opts_fixed <- do.call(ci_opts_fixed, as.list(c(opts_fixed, ...)))
    .Call(C_ci_subset_fixed_replacement, str, pattern, negate, opts_fixed, value)
}


# @export
# @rdname ci_subset
ci_subset_charclass <- function(str, pattern, omit_na = FALSE, negate = FALSE)
{
    .Call(C_ci_subset_charclass, str, pattern, omit_na, negate)
}


# @export
# @rdname ci_subset
# @usage ci_subset_charclass(str, pattern, negate=FALSE) <- value
`ci_subset_charclass<-` <- function(str, pattern, negate = FALSE, value)
{
    .Call(C_ci_subset_charclass_replacement, str, pattern, negate, value)
}


# @export
# @rdname ci_subset
ci_subset_coll <- function(str, pattern, omit_na = FALSE, negate = FALSE, ...,
    opts_collator = NULL)
{
    if (!missing(...))
        opts_collator <- do.call(ci_opts_collator, as.list(c(opts_collator, ...)))
    .Call(C_ci_subset_coll, str, pattern, omit_na, negate, opts_collator)
}


# @export
# @rdname ci_subset
# @usage ci_subset_coll(str, pattern, negate=FALSE, ..., opts_collator=NULL) <- value
`ci_subset_coll<-` <- function(str, pattern, negate = FALSE, ..., opts_collator = NULL,
    value)
{
    if (!missing(...))
        opts_collator <- do.call(ci_opts_collator, as.list(c(opts_collator, ...)))
    .Call(C_ci_subset_coll_replacement, str, pattern, negate, opts_collator, value)
}


# @export
# @rdname ci_subset
ci_subset_regex <- function(str, pattern, omit_na = FALSE, negate = FALSE, ...,
    opts_regex = NULL)
{
    if (!missing(...))
        opts_regex <- do.call(ci_opts_regex, as.list(c(opts_regex, ...)))
    .Call(C_ci_subset_regex, str, pattern, omit_na, negate, opts_regex)
}


# @export
# @rdname ci_subset
# @usage ci_subset_regex(str, pattern, negate=FALSE, ..., opts_regex=NULL) <- value
`ci_subset_regex<-` <- function(str, pattern, negate = FALSE, ..., opts_regex = NULL,
    value)
{
    if (!missing(...))
        opts_regex <- do.call(ci_opts_regex, as.list(c(opts_regex, ...)))
    .Call(C_ci_subset_regex_replacement, str, pattern, negate, opts_regex, value)
}
