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
# Compare Strings with or without Collation
#
# @description
# These functions may be used to determine if two strings
# are equal, canonically equivalent (this is performed in a much more clever
# fashion than when testing for equality), or to check whether they are in
# a specific lexicographic order.
#
#
# @details
# All the functions listed here are vectorized over \code{e1} and \code{e2}.
#
# \code{ci_cmp_eq} tests whether two corresponding strings
# consist of exactly the same code points, while \code{ci_cmp_neq} allows
# to check whether there is any difference between them. These are
# locale-independent operations: for natural language processing,
# where the notion of canonical equivalence is more valid, this might
# not be exactly what you are looking for, see Examples.
# Please note that \pkg{stringi} always silently removes UTF-8
# BOMs from input strings, therefore, e.g., \code{ci_cmp_eq} does not take
# BOMs into account while comparing strings.
#
# \code{ci_cmp_equiv} tests for canonical equivalence of two strings
# and is locale-dependent. Additionally, the \pkg{ICU}'s Collator may be
# tuned up so that, e.g., the comparison is case-insensitive.
# To test whether two strings are not canonically equivalent,
# call \code{ci_cmp_nequiv}.
#
# \code{ci_cmp_le} tests whether
# the elements in the first vector are less than or equal to
# the corresponding elements in the second vector,
# \code{ci_cmp_ge} tests whether they are greater or equal,
# \code{ci_cmp_lt} if less, and \code{ci_cmp_gt} if greater,
# see also, e.g., \code{\link{\%s<\%}}.
#
# \code{ci_compare} is an alias to \code{ci_cmp}. They both
# perform exactly the same locale-dependent operation.
# Both functions provide a C library's \code{strcmp()} look-and-feel,
# see Value for details.
#
#
# For more information on \pkg{ICU}'s Collator and how to tune its settings
# refer to \code{\link{ci_opts_collator}}.
# Note that different locale settings may lead to different results
# (see the examples below).
#
#
# @param e1,e2 character vectors or objects coercible to character vectors
# @param opts_collator a named list with \pkg{ICU} Collator's options,
# see \code{\link{ci_opts_collator}}, \code{NULL}
# for the default collation options.
# @param ... additional settings for \code{opts_collator}
#
# @return The \code{ci_cmp} and \code{ci_compare} functions
# return an integer vector representing the comparison results:
# \code{-1} if \code{e1[...] < e2[...]},
# \code{0} if they are canonically equivalent, and \code{1} if greater.
#
# All the other functions return a logical vector that indicates
# whether a given relation holds between two corresponding elements
# in \code{e1} and \code{e2}.
#
# @references
# \emph{Collation} -- ICU User Guide,
# \url{https://unicode-org.github.io/icu/userguide/collation/}
#
# @examples
# # in Polish, ch < h:
# ci_cmp_lt('hladny', 'chladny', locale='pl_PL')
#
# # in Slovak, ch > h:
# ci_cmp_lt('hladny', 'chladny', locale='sk_SK')
#
# # < or > (depends on locale):
# ci_cmp('hladny', 'chladny')
#
# # ignore case differences:
# ci_cmp_equiv('hladny', 'HLADNY', strength=2)
#
# # also ignore diacritical differences:
# ci_cmp_equiv('hladn\u00FD', 'hladny', strength=1, locale='sk_SK')
#
# marios <- c('Mario', 'mario', 'M\\u00e1rio', 'm\\u00e1rio')
# ci_cmp_equiv(marios, 'mario', case_level=TRUE, strength=2L)
# ci_cmp_equiv(marios, 'mario', case_level=TRUE, strength=1L)
# ci_cmp_equiv(marios, 'mario', strength=1L)
# ci_cmp_equiv(marios, 'mario', strength=2L)
#
# # non-Unicode-normalized vs normalized string:
# ci_cmp_equiv(ci_trans_nfkd('\u0105'), '\u105')
#
# # note the difference:
# ci_cmp_eq(ci_trans_nfkd('\u0105'), '\u105')
#
# # ligatures:
# ci_cmp_equiv('\ufb00', 'ff', strength=2)
#
# # phonebook collation
# ci_cmp_equiv('G\u00e4rtner', 'Gaertner', locale='de_DE@@collation=phonebook', strength=1L)
# ci_cmp_equiv('G\u00e4rtner', 'Gaertner', locale='de_DE', strength=1L)
#
# @family locale_sensitive
# @export
# @rdname ci_compare
ci_compare <- function(e1, e2, ..., opts_collator = NULL)
{
    if (!missing(...))
        opts_collator <- do.call(ci_opts_collator, as.list(c(opts_collator, ...)))
    .Call(C_ci_cmp, e1, e2, opts_collator)
}


# @export
# @rdname ci_compare
ci_cmp <- ci_compare


# @export
# @rdname ci_compare
ci_cmp_eq <- function(e1, e2)
{
    .Call(C_ci_cmp_eq, e1, e2)
}


# @export
# @rdname ci_compare
ci_cmp_neq <- function(e1, e2)
{
    .Call(C_ci_cmp_neq, e1, e2)
}


# @export
# @rdname ci_compare
ci_cmp_equiv <- function(e1, e2, ..., opts_collator = NULL)
{
    if (!missing(...))
        opts_collator <- do.call(ci_opts_collator, as.list(c(opts_collator, ...)))
    .Call(C_ci_cmp_equiv, e1, e2, opts_collator)
}


# @export
# @rdname ci_compare
ci_cmp_nequiv <- function(e1, e2, ..., opts_collator = NULL)
{
    if (!missing(...))
        opts_collator <- do.call(ci_opts_collator, as.list(c(opts_collator, ...)))
    .Call(C_ci_cmp_nequiv, e1, e2, opts_collator)
}

# @export
# @rdname ci_compare
ci_cmp_lt <- function(e1, e2, ..., opts_collator = NULL)
{
    if (!missing(...))
        opts_collator <- do.call(ci_opts_collator, as.list(c(opts_collator, ...)))
    .Call(C_ci_cmp_lt, e1, e2, opts_collator)
}


# @export
# @rdname ci_compare
ci_cmp_gt <- function(e1, e2, ..., opts_collator = NULL)
{
    if (!missing(...))
        opts_collator <- do.call(ci_opts_collator, as.list(c(opts_collator, ...)))
    .Call(C_ci_cmp_gt, e1, e2, opts_collator)
}

# @export
# @rdname ci_compare
ci_cmp_le <- function(e1, e2, ..., opts_collator = NULL)
{
    if (!missing(...))
        opts_collator <- do.call(ci_opts_collator, as.list(c(opts_collator, ...)))
    .Call(C_ci_cmp_le, e1, e2, opts_collator)
}


# @export
# @rdname ci_compare
ci_cmp_ge <- function(e1, e2, ..., opts_collator = NULL)
{
    if (!missing(...))
        opts_collator <- do.call(ci_opts_collator, as.list(c(opts_collator, ...)))
    .Call(C_ci_cmp_ge, e1, e2, opts_collator)
}


# @title
# Compare Strings with or without Collation
#
# @description
# Relational operators for comparing corresponding strings in
# two character vectors, with a typical R look-and-feel.
#
# @details
# These functions call \code{\link{ci_cmp_le}} or its
# friends, using the default collator options.
# As a consequence, they are vectorized over \code{e1} and \code{e2}.
#
# \code{\%stri==\%} tests for canonical equivalence of strings
# (see \code{\link{ci_cmp_equiv}}) and is a locale-dependent operation.
#
# \code{\%stri===\%} performs a locale-independent,
# code point-based comparison.
#
#
# @param e1,e2 character vectors or objects coercible to character vectors
#
# @return All the functions return a logical vector
# indicating the result of a pairwise comparison.
# As usual, the elements of shorter vectors are recycled if necessary.
#
#
# @examples
# 'a' %stri<% 'b'
# c('a', 'b', 'c') %stri>=% 'b'
#
# @usage
# e1 \%s<\% e2
#
# @family locale_sensitive
# @rdname operator_compare
# @aliases operator_compare oper_comparison oper_compare
# @export


# @usage
# e1 \%s<=\% e2
# @rdname operator_compare
# @export


# @usage
# e1 \%s>\% e2
# @rdname operator_compare
# @export


# @usage
# e1 \%s>=\% e2
# @rdname operator_compare
# @export


# @usage
# e1 \%s==\% e2
# @rdname operator_compare
# @export


# @usage
# e1 \%s!=\% e2
# @rdname operator_compare
# @export


# @usage
# e1 \%s===\% e2
# @rdname operator_compare
# @export


# @usage
# e1 \%s!==\% e2
# @rdname operator_compare
# @export


# @usage
# e1 \%stri<\% e2
# @rdname operator_compare
# @export


# @usage
# e1 \%stri<=\% e2
# @rdname operator_compare
# @export


# @usage
# e1 \%stri>\% e2
# @rdname operator_compare
# @export


# @usage
# e1 \%stri>=\% e2
# @rdname operator_compare
# @export


# @usage
# e1 \%stri==\% e2
# @rdname operator_compare
# @export


# @usage
# e1 \%stri!=\% e2
# @rdname operator_compare
# @export


# @usage
# e1 \%stri===\% e2
# @rdname operator_compare
# @export


# @usage
# e1 \%stri!==\% e2
# @rdname operator_compare
# @export
