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
# Extract Regex Pattern Matches, Together with Capture Groups
#
# @description
# These functions extract substrings in \code{str} that
# match a given regex \code{pattern}. Additionally, they extract matches
# to every \emph{capture group}, i.e., to all the sub-patterns given
# in round parentheses.
#
# @details
# Vectorized over \code{str} and \code{pattern} (with recycling
# of the elements in the shorter vector if necessary). This allows to,
# for instance, search for one pattern in each given string,
# search for each pattern in one given string,
# and search for the i-th pattern within the i-th string.
#
# If no pattern match is detected and \code{omit_no_match=FALSE},
# then \code{NA}s are included in the resulting matrix (matrices), see Examples.
#
# \code{ci_match}, \code{ci_match_all}, \code{ci_match_first},
# and \code{ci_match_last} are convenience functions.
# They merely call \code{ci_match_*_regex} and are
# provided for consistency with other string searching functions' wrappers,
# see, among others, \code{\link{ci_extract}}.
#
# @param str character vector; strings to search in
# @param pattern,regex character vector;
#     search patterns; for more details refer to \link{stringi-search}
# @param opts_regex a named list with \pkg{ICU} Regex settings,
# see \code{\link{ci_opts_regex}}; \code{NULL}
# for default settings
# @param omit_no_match single logical value; if \code{FALSE},
# then a row with missing values will indicate that there was no match;
# \code{ci_match_all_*} only
# @param cg_missing single string to be used if a capture group match
# is unavailable
# @param mode single string;
# one of: \code{'first'} (the default), \code{'all'}, \code{'last'}
# @param ... supplementary arguments passed to the underlying functions,
# including additional settings for \code{opts_regex}
#
# @return
# For \code{ci_match_all*},
# a list of character matrices is returned. Each list element
# represents the results of a different search scenario.
#
# For \code{ci_match_first*} and \code{ci_match_last*}
# a character matrix is returned.
# Each row corresponds to a different search result.
#
# The first matrix column gives the whole match. The second one corresponds to
# the first capture group, the third -- the second capture group, and so on.
#
# If regular expressions feature a named capture group,
# the matrix columns will be named accordingly.
# However, for \code{ci_match_first*} and \code{ci_match_last*}
# this will only be the case if there is a single pattern.
#
#
# @examples
# ci_match_all_regex('breakfast=eggs, lunch=pizza, dessert=icecream',
#    '(\\w+)=(\\w+)')
# ci_match_all_regex(c('breakfast=eggs', 'lunch=pizza', 'no food here'),
#    '(\\w+)=(\\w+)')
# ci_match_all_regex(c('breakfast=eggs;lunch=pizza',
#    'breakfast=bacon;lunch=spaghetti', 'no food here'),
#    '(\\w+)=(\\w+)')
# ci_match_all_regex(c('breakfast=eggs;lunch=pizza',
#    'breakfast=bacon;lunch=spaghetti', 'no food here'),
#    '(?<when>\\w+)=(?<what>\\w+)')  # named capture groups
# ci_match_first_regex(c('breakfast=eggs;lunch=pizza',
#    'breakfast=bacon;lunch=spaghetti', 'no food here'),
#    '(\\w+)=(\\w+)')
# ci_match_last_regex(c('breakfast=eggs;lunch=pizza',
#    'breakfast=bacon;lunch=spaghetti', 'no food here'),
#    '(\\w+)=(\\w+)')
#
# ci_match_first_regex(c('abcd', ':abcd', ':abcd:'), '^(:)?([^:]*)(:)?$')
# ci_match_first_regex(c('abcd', ':abcd', ':abcd:'), '^(:)?([^:]*)(:)?$', cg_missing='')
#
# # Match all the pattern of the form XYX, including overlapping matches:
# ci_match_all_regex('ACAGAGACTTTAGATAGAGAAGA', '(?=(([ACGT])[ACGT]\\2))')[[1]][,2]
# # Compare the above to:
# ci_extract_all_regex('ACAGAGACTTTAGATAGAGAAGA', '([ACGT])[ACGT]\\1')
#
# @family search_extract
# @export
# @rdname ci_match


# @export
# @rdname ci_match


# @export
# @rdname ci_match


# @export
# @rdname ci_match


# @export
# @rdname ci_match
ci_match_all_regex <- function(str, pattern,
    omit_no_match = FALSE, cg_missing = NA_character_,
    ..., opts_regex = NULL)
{
    if (!missing(...))
        opts_regex <- do.call(ci_opts_regex, as.list(c(opts_regex, ...)))
    .Call(C_ci_match_all_regex, str, pattern, omit_no_match, cg_missing, opts_regex)
}


# @export
# @rdname ci_match
ci_match_first_regex <- function(str, pattern, cg_missing = NA_character_, ...,
    opts_regex = NULL)
{
    if (!missing(...))
        opts_regex <- do.call(ci_opts_regex, as.list(c(opts_regex, ...)))
    .Call(C_ci_match_first_regex, str, pattern, cg_missing, opts_regex)
}


# @export
# @rdname ci_match
ci_match_last_regex <- function(str, pattern, cg_missing = NA_character_, ...,
    opts_regex = NULL)
{
    if (!missing(...))
        opts_regex <- do.call(ci_opts_regex, as.list(c(opts_regex, ...)))
    .Call(C_ci_match_last_regex, str, pattern, cg_missing, opts_regex)
}
