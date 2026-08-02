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
# Convert a List to a Character Matrix
#
# @description
# This function converts a given list of atomic vectors to
# a character matrix.
#
# @details
# This function is similar to the built-in \code{\link{simplify2array}}
# function. However, it always returns a character matrix,
# even if each element in \code{x} is of length 1
# or if elements in \code{x} are not of the same lengths.
# Moreover, the elements in \code{x} are always coerced to character vectors.
#
# If \code{byrow} is \code{FALSE}, then a matrix with \code{length(x)}
# columns is returned.
# The number of rows is the length of the
# longest vector in \code{x}, but no less than \code{n_min}. Basically, we have
# \code{result[i,j] == x[[j]][i]} if \code{i <= length(x[[j]])}
# and \code{result[i,j] == fill} otherwise, see Examples.
#
# If \code{byrow} is \code{TRUE}, then the resulting matrix is
# a transposition of the above-described one.
#
# This function may be useful, e.g., in connection with \code{\link{ci_split}}
# and \code{\link{ci_extract_all}}.
#
# @param x a list of atomic vectors
# @param byrow a single logical value; should the resulting matrix be
# transposed?
# @param fill a single string, see Details
# @param n_min a single integer value; minimal number of rows (\code{byrow==FALSE})
# or columns (otherwise) in the resulting matrix
# @param by_row alias of \code{byrow}
#
# @return
# Returns a character matrix.
#
# @examples
# simplify2array(list(c('a', 'b'), c('c', 'd'), c('e', 'f')))
# ci_list2matrix(list(c('a', 'b'), c('c', 'd'), c('e', 'f')))
# ci_list2matrix(list(c('a', 'b'), c('c', 'd'), c('e', 'f')), byrow=TRUE)
#
# simplify2array(list('a', c('b', 'c')))
# ci_list2matrix(list('a', c('b', 'c')))
# ci_list2matrix(list('a', c('b', 'c')), fill='')
# ci_list2matrix(list('a', c('b', 'c')), fill='', n_min=5)
#
# @family utils
# @export


# @title
# Replace NAs with Empty Strings
#
# @description
# This function replaces all missing values with empty strings.
# See \code{\link{ci_replace_na}} for a generalization.
#
# @param x a character vector
#
# @return
# Returns a character vector.
#
# @examples
# ci_na2empty(c('a', NA, '', 'b'))
#
# @family utils
# @export


# @title
# Remove All Empty Strings from a Character Vector
#
# @description
# \code{ci_remove_empty} (alias \code{ci_omit_empty})
# removes all empty strings from a character vector,
# and, if \code{na_empty} is \code{TRUE}, also gets rid of all missing
# values.
#
# \code{ci_remove_empty_na} (alias \code{ci_omit_empty_na})
# removes both empty strings and missing values.
#
# \code{ci_remove_na} (alias \code{ci_omit_na})
# returns a version of \code{x} with missing values removed.
#
# @param x a character vector
# @param na_empty should missing values be treated as empty strings?
#
# @return
# Returns a character vector.
#
# @examples
# ci_remove_empty(ci_na2empty(c('a', NA, '', 'b')))
# ci_remove_empty(c('a', NA, '', 'b'))
# ci_remove_empty(c('a', NA, '', 'b'), TRUE)
#
# ci_omit_empty_na(c('a', NA, '', 'b'))
#
# @family utils
# @rdname ci_remove_empty
# @export


# @rdname ci_remove_empty
# @export


# @rdname ci_remove_empty
# @export


# @rdname ci_remove_empty
# @export


# @rdname ci_remove_empty
# @export


# @rdname ci_remove_empty
# @export


# @title
# Replace Missing Values in a Character Vector
#
# @description
# This function gives a convenient way to replace each missing (\code{NA})
# value with a given string.
#
# @details
# This function is roughly equivalent to
# \code{str2 <- ci_enc_toutf8(str);
# str2[is.na(str2)] <- ci_enc_toutf8(replacement);
# str2}.
# It may be used, e.g., wherever the 'plain R' \code{NA} handling is
# desired, see Examples.
#
# @param str character vector or an object coercible to
# @param replacement single string
#
# @return Returns a character vector.
#
# @examples
# x <- c('test', NA)
# ci_paste(x, 1:2)                           # 'test1' NA
# paste(x, 1:2)                                # 'test 1' 'NA 2'
# ci_paste(ci_replace_na(x), 1:2, sep=' ') # 'test 1' 'NA 2'
#
# @export
# @family utils
ci_replace_na <- function(str, replacement = "NA")
{
    .Call(C_ci_replace_na, str, replacement)
}
