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
# Split a String Into Text Lines
#
# @description
# These functions split each character string in a given vector
# into text lines.
#
# @details
# Vectorized over \code{str} and \code{omit_empty}.
#
# \code{omit_empty} is applied when splitting. If set to \code{TRUE},
# then empty strings will never appear in the resulting vector.
#
# Newlines are represented with the Carriage Return
# (CR, 0x0D), Line Feed (LF, 0x0A), CRLF, or Next Line (NEL, 0x85) characters,
# depending on the platform.
# Moreover, the Unicode Standard defines two unambiguous separator characters,
# the Paragraph Separator (PS, 0x2029) and the Line Separator (LS, 0x2028).
# Sometimes also the Vertical Tab (VT, 0x0B) and the Form Feed (FF, 0x0C)
# are used for this purpose.
#
# These \pkg{stringi} functions follow UTR#18 rules,
# where a newline sequence
# corresponds to the following regular expression:
# \code{(?:\\u\{D A\}|(?!\\u\{D A\})[\\u\{A\}-\\u\{D\}\\u\{85\}\\u\{2028\}\\u\{2029\}]}.
# Each match serves as a text line separator.
#
#
# @param str character vector (\code{ci_split_lines})
#        or a single string (\code{ci_split_lines1})
# @param omit_empty logical vector; determines whether empty
# strings should be removed from the result
#    [\code{ci_split_lines} only]
#
# @return \code{ci_split_lines} returns a list of character vectors.
# If any input string is \code{NA}, then the corresponding list element
# is a single \code{NA} string.
#
# \code{ci_split_lines1(str)} is equivalent to
# \code{ci_split_lines(str[1])[[1]]} (with default parameters),
# therefore it returns a character vector. Moreover, if the input string
# ends with a newline sequence, the last empty string is omitted from the
#  result. This function may come in handy if you wish to split a text
# file's contents into text lines.
#
# @references
# \emph{Unicode Newline Guidelines} -- Unicode Technical Report #13,
# \url{https://www.unicode.org/standard/reports/tr13/tr13-5.html}
#
# \emph{Unicode Regular Expressions} -- Unicode Technical Standard #18,
# \url{https://www.unicode.org/reports/tr18/}
#
# @family search_split
# @family text_boundaries
# @export
# @rdname ci_split_lines
# @aliases ci_split_lines ci_split_lines1
ci_split_lines <- function(str, omit_empty = FALSE) {
    .Call(C_ci_split_lines, str, omit_empty)
}


# @rdname ci_split_lines
# @export
ci_split_lines1 <- function(str) {
    .Call(C_ci_split_lines1, str)
}


# @title
# Split a String at Text Boundaries
#
# @description
# This function locates text boundaries
# (like character, word, line, or sentence boundaries)
# and splits strings at the indicated positions.
#
# @details
# Vectorized over \code{str} and \code{n}.
#
# If \code{n} is negative (the default), then all text pieces are extracted.
#
# Otherwise, if \code{tokens_only} is \code{FALSE} (which is the default),
# then \code{n-1} tokens are extracted (if possible) and the \code{n}-th string
# gives the (non-split) remainder (see Examples).
# On the other hand, if \code{tokens_only} is \code{TRUE},
# then only full tokens (up to \code{n} pieces) are extracted.
#
# For more information on text boundary analysis
# performed by \pkg{ICU}'s \code{BreakIterator}, see
# \link{stringi-search-boundaries}.
#
# @param str character vector or an object coercible to
# @param n integer vector, maximal number of strings to return
# @param tokens_only single logical value; may affect the result if \code{n}
# is positive, see Details
# @param simplify single logical value; if \code{TRUE} or \code{NA},
# then a character matrix is returned; otherwise (the default), a list of
# character vectors is given, see Value
# @param opts_brkiter a named list with \pkg{ICU} BreakIterator's settings,
# see \code{\link{ci_opts_brkiter}}; \code{NULL} for the
# default break iterator, i.e., \code{line_break}
# @param ... additional settings for \code{opts_brkiter}
#
# @return If \code{simplify=FALSE} (the default),
# then the functions return a list of character vectors.
#
# Otherwise, \code{\link{ci_list2matrix}} with \code{byrow=TRUE}
# and \code{n_min=n} arguments is called on the resulting object.
# In such a case, a character matrix with \code{length(str)} rows
# is returned. Note that \code{\link{ci_list2matrix}}'s \code{fill}
# argument is set to an empty string and \code{NA},
# for \code{simplify} equal to \code{TRUE} and \code{NA}, respectively.
#
# @examples
# test <- 'The\u00a0above-mentioned    features are very useful. ' %s+%
#    'Spam, spam, eggs, bacon, and spam. 123 456 789'
# ci_split_boundaries(test, type='line')
# ci_split_boundaries(test, type='word')
# ci_split_boundaries(test, type='word', skip_word_none=TRUE)
# ci_split_boundaries(test, type='word', skip_word_none=TRUE, skip_word_letter=TRUE)
# ci_split_boundaries(test, type='word', skip_word_none=TRUE, skip_word_number=TRUE)
# ci_split_boundaries(test, type='sentence')
# ci_split_boundaries(test, type='sentence', skip_sentence_sep=TRUE)
# ci_split_boundaries(test, type='character')
#
# # a filtered break iterator with the new ICU:
# ci_split_boundaries('Mr. Jones and Mrs. Brown are very happy.
# So am I, Prof. Smith.', type='sentence', locale='en_US@ss=standard') # ICU >= 56 only
#
# @export
# @family search_split
# @family locale_sensitive
# @family text_boundaries
ci_split_boundaries <- function(str, n = -1L,
    tokens_only = FALSE, simplify = FALSE,
    ..., opts_brkiter = NULL)
{
    if (!missing(...))
        opts_brkiter <- do.call(ci_opts_brkiter, as.list(c(opts_brkiter, ...)))
    .Call(C_ci_split_boundaries, str, n, tokens_only, simplify, opts_brkiter)
}
