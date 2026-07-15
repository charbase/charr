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
# Count the Number of Text Boundaries
#
# @description
# These functions determine the number of text boundaries
# (like character, word, line, or sentence boundaries) in a string.
#
# @details
# Vectorized over \code{str}.
#
# For more information on text boundary analysis
# performed by \pkg{ICU}'s \code{BreakIterator}, see
# \link{stringi-search-boundaries}.
#
# In case of \code{ci_count_words},
# just like in \code{\link{ci_extract_all_words}} and
# \code{\link{ci_locate_all_words}},
# \pkg{ICU}'s word \code{BreakIterator} iterator is used
# to locate the word boundaries, and all non-word characters
# (\code{UBRK_WORD_NONE} rule status) are ignored.
# This function is equivalent to a call to
# \code{\link{ci_count_boundaries}(str, type='word', skip_word_none=TRUE, locale=locale)}.
#
# Note that a \code{BreakIterator} of type \code{character}
# may be used to count the number of \emph{Unicode characters} in a string.
# The \code{\link{ci_length}} function,
# which aims to count the number of \emph{Unicode code points},
# might report different results.
#
# Moreover, a \code{BreakIterator} of type \code{sentence}
# may be used to count the number of sentences in a text piece.
#
#
# @param str character vector or an object coercible to
# @param opts_brkiter a named list with \pkg{ICU} BreakIterator's settings,
# see \code{\link{ci_opts_brkiter}};
# \code{NULL} for the default break iterator, i.e., \code{line_break}
# @param ... additional settings for \code{opts_brkiter}
# @param locale \code{NULL} or \code{''} for text boundary analysis following
# the conventions of the default locale, or a single string with
# locale identifier, see \link{stringi-locale}
#
# @return
# Both functions return an integer vector.
#
# @examples
# test <- 'The\u00a0above-mentioned    features are very useful. Spam, spam, eggs, bacon, and spam.'
# ci_count_boundaries(test, type='word')
# ci_count_boundaries(test, type='sentence')
# ci_count_boundaries(test, type='character')
# ci_count_words(test)
#
# test2 <- ci_trans_nfkd('\u03c0\u0153\u0119\u00a9\u00df\u2190\u2193\u2192')
# ci_count_boundaries(test2, type='character')
# ci_length(test2)
# ci_numbytes(test2)
#
# @export
# @family search_count
# @family locale_sensitive
# @family text_boundaries
# @rdname ci_count_boundaries
ci_count_boundaries <- function(str, ..., opts_brkiter = NULL)
{
    if (!missing(...))
        opts_brkiter <- do.call(ci_opts_brkiter, as.list(c(opts_brkiter, ...)))
    .Call(C_ci_count_boundaries, str, opts_brkiter)
}


# @export
# @rdname ci_count_boundaries
ci_count_words <- function(str, locale = NULL)
{
    ci_count_boundaries(str,
        opts_brkiter = ci_opts_brkiter(type = "word", skip_word_none = TRUE,
        locale = locale))
}
