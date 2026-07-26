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
# Extract Pattern Occurrences
#
# @description
# These functions extract all substrings matching a given pattern.
#
# \code{ci_extract_all_*} extracts all the matches.
# \code{ci_extract_first_*} and \code{ci_extract_last_*}
# yield the first or the last matches, respectively.
#
# @details
# Vectorized over \code{str} and \code{pattern} (with recycling
# of the elements in the shorter vector if necessary). This allows to,
# for instance, search for one pattern in each given string,
# search for each pattern in one given string,
# and search for the i-th pattern within the i-th string.
#
# Check out \code{\link{ci_match}} for the extraction of matches
# to individual regex capture groups.
#
# \code{ci_extract}, \code{ci_extract_all}, \code{ci_extract_first},
# and \code{ci_extract_last} are convenience functions.
# They merely call \code{ci_extract_*_*}, depending on the arguments used.
#
# @param str character vector; strings to search in
# @param pattern,regex,fixed,coll,charclass character vector;
#     search patterns; for more details refer to \link{stringi-search}
# @param opts_collator,opts_fixed,opts_regex a named list to tune up
#     the search engine's settings; see \code{\link{ci_opts_collator}},
#     \code{\link{ci_opts_fixed}}, and \code{\link{ci_opts_regex}},
#     respectively; \code{NULL} for the defaults
# @param merge single logical value; indicates whether consecutive pattern
#     matches will be merged into one string;
#     \code{ci_extract_all_charclass} only
# @param simplify single logical value;
#     if \code{TRUE} or \code{NA}, then a character matrix is returned;
#     otherwise (the default), a list of character vectors is given, see Value;
#     \code{ci_extract_all_*} only
# @param omit_no_match single logical value; if \code{FALSE},
#     then a missing value will indicate that there was no match;
#     \code{ci_extract_all_*} only
# @param mode single string;
#     one of: \code{'first'} (the default), \code{'all'}, \code{'last'}
# @param ... supplementary arguments passed to the underlying functions,
#     including additional settings for \code{opts_collator}, \code{opts_regex},
#     and so on
#
# @return
# For \code{ci_extract_all*}, if \code{simplify=FALSE} (the default), then
# a list of character vectors is returned. Each list element
# represents the results of a different search scenario.
# If a pattern is not found and \code{omit_no_match=FALSE},
# then a character vector of length 1
# with single \code{NA} value will be generated.
#
# Otherwise, i.e., if \code{simplify} is not \code{FALSE},
# then \code{\link{ci_list2matrix}} with \code{byrow=TRUE} argument
# is called on the resulting object.
# In such a case, the function yields a character matrix with an appropriate
# number of rows (according to the length of \code{str}, \code{pattern}, etc.).
# Note that \code{\link{ci_list2matrix}}'s \code{fill} argument is set
# either to an empty string or \code{NA}, depending on
# whether \code{simplify} is \code{TRUE} or \code{NA}, respectively.
#
# \code{ci_extract_first*} and \code{ci_extract_last*}
# return a character vector. A \code{NA} element indicates a no-match.
#
# Note that \code{ci_extract_last_regex} searches from start to end,
# but skips overlapping matches, see the example below.
#
# @examples
# ci_extract_all('XaaaaX', regex=c('\\p{Ll}', '\\p{Ll}+', '\\p{Ll}{2,3}', '\\p{Ll}{2,3}?'))
# ci_extract_all('Bartolini', coll='i')
# ci_extract_all('stringi is so good!', charclass='\\p{Zs}') # all white-spaces
#
# ci_extract_all_charclass(c('AbcdeFgHijK', 'abc', 'ABC'), '\\p{Ll}')
# ci_extract_all_charclass(c('AbcdeFgHijK', 'abc', 'ABC'), '\\p{Ll}', merge=FALSE)
# ci_extract_first_charclass('AaBbCc', '\\p{Ll}')
# ci_extract_last_charclass('AaBbCc', '\\p{Ll}')
#
# \dontrun{
# # emoji support available since ICU 57
# ci_extract_all_charclass(ci_enc_fromutf32(32:55200), '\\p{EMOJI}')
# }
#
# ci_extract_all_coll(c('AaaaaaaA', 'AAAA'), 'a')
# ci_extract_first_coll(c('Yy\u00FD', 'AAA'), 'y', strength=2, locale='sk_SK')
# ci_extract_last_coll(c('Yy\u00FD', 'AAA'), 'y',  strength=1, locale='sk_SK')
#
# ci_extract_all_regex('XaaaaX', c('\\p{Ll}', '\\p{Ll}+', '\\p{Ll}{2,3}', '\\p{Ll}{2,3}?'))
# ci_extract_first_regex('XaaaaX', c('\\p{Ll}', '\\p{Ll}+', '\\p{Ll}{2,3}', '\\p{Ll}{2,3}?'))
# ci_extract_last_regex('XaaaaX', c('\\p{Ll}', '\\p{Ll}+', '\\p{Ll}{2,3}', '\\p{Ll}{2,3}?'))
#
# ci_list2matrix(ci_extract_all_regex('XaaaaX', c('\\p{Ll}', '\\p{Ll}+')))
# ci_extract_all_regex('XaaaaX', c('\\p{Ll}', '\\p{Ll}+'), simplify=TRUE)
# ci_extract_all_regex('XaaaaX', c('\\p{Ll}', '\\p{Ll}+'), simplify=NA)
#
# ci_extract_all_fixed('abaBAba', 'Aba', case_insensitive=TRUE)
# ci_extract_all_fixed('abaBAba', 'Aba', case_insensitive=TRUE, overlap=TRUE)
#
# # Searching for the last occurrence:
# # Note the difference - regex searches left to right, with no overlaps.
# ci_extract_last_fixed("agAGA", "aga", case_insensitive=TRUE)
# ci_extract_last_regex("agAGA", "aga", case_insensitive=TRUE)
#
# @family search_extract
#
# @export
# @rdname ci_extract


# @export
# @rdname ci_extract


# @export
# @rdname ci_extract


# @export
# @rdname ci_extract


# @export
# @rdname ci_extract


# @export
# @rdname ci_extract


# @export
# @rdname ci_extract


# @export
# @rdname ci_extract
ci_extract_all_coll <- function(str, pattern, simplify = FALSE,
    omit_no_match = FALSE, ..., opts_collator = NULL)
{
    if (!missing(...))
        opts_collator <- do.call(ci_opts_collator, as.list(c(opts_collator, ...)))
    .Call(C_ci_extract_all_coll, str, pattern, simplify, omit_no_match, opts_collator)
}


# @export
# @rdname ci_extract
ci_extract_first_coll <- function(str, pattern, ..., opts_collator = NULL)
{
    if (!missing(...))
        opts_collator <- do.call(ci_opts_collator, as.list(c(opts_collator, ...)))
    .Call(C_ci_extract_first_coll, str, pattern, opts_collator)
}


# @export
# @rdname ci_extract
ci_extract_last_coll <- function(str, pattern, ..., opts_collator = NULL)
{
    if (!missing(...))
        opts_collator <- do.call(ci_opts_collator, as.list(c(opts_collator, ...)))
    .Call(C_ci_extract_last_coll, str, pattern, opts_collator)
}


# @export
# @rdname ci_extract
ci_extract_all_regex <- function(str, pattern, simplify = FALSE,
    omit_no_match = FALSE, ..., opts_regex = NULL)
{
    if (!missing(...))
        opts_regex <- do.call(ci_opts_regex, as.list(c(opts_regex, ...)))
    .Call(C_ci_extract_all_regex, str, pattern, simplify, omit_no_match, opts_regex)
}


# @export
# @rdname ci_extract
ci_extract_first_regex <- function(str, pattern, ..., opts_regex = NULL)
{
    if (!missing(...))
        opts_regex <- do.call(ci_opts_regex, as.list(c(opts_regex, ...)))
    .Call(C_ci_extract_first_regex, str, pattern, opts_regex)
}


# @export
# @rdname ci_extract
ci_extract_last_regex <- function(str, pattern, ..., opts_regex = NULL)
{
    if (!missing(...))
        opts_regex <- do.call(ci_opts_regex, as.list(c(opts_regex, ...)))
    .Call(C_ci_extract_last_regex, str, pattern, opts_regex)
}


# @export
# @rdname ci_extract
ci_extract_all_fixed <- function(str, pattern, simplify = FALSE,
    omit_no_match = FALSE, ..., opts_fixed = NULL)
{
    if (!missing(...))
        opts_fixed <- do.call(ci_opts_fixed, as.list(c(opts_fixed, ...)))
    .Call(C_ci_extract_all_fixed, str, pattern, simplify, omit_no_match, opts_fixed)
}


# @export
# @rdname ci_extract
ci_extract_first_fixed <- function(str, pattern, ..., opts_fixed = NULL)
{
    if (!missing(...))
        opts_fixed <- do.call(ci_opts_fixed, as.list(c(opts_fixed, ...)))
    .Call(C_ci_extract_first_fixed, str, pattern, opts_fixed)
}


# @export
# @rdname ci_extract
ci_extract_last_fixed <- function(str, pattern, ..., opts_fixed = NULL)
{
    if (!missing(...))
        opts_fixed <- do.call(ci_opts_fixed, as.list(c(opts_fixed, ...)))
    .Call(C_ci_extract_last_fixed, str, pattern, opts_fixed)
}
