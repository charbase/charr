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
# Read Text File as Raw
#
# @description
# Reads a text file as-is, with no conversion or text line splitting.
#
# @details
# Once a text file is read into memory,
# encoding detection (see \code{\link{ci_enc_detect}}),
# conversion (see \code{\link{ci_encode}}), and/or
# splitting of text into lines (see \code{\link{ci_split_lines1}})
# can be performed.
#
# @param con name of the output file or a connection object
#        (opened in the binary mode)
# @param fname [DEPRECATED] alias of \code{con}
#
# @return
# Returns a vector of type \code{raw}.
#
# @family files
# @export
ci_read_raw <- function(con, fname = con)
{
    if (!missing(fname) && missing(con)) { # DEPRECATED
        warning("The 'fname' argument in ci_read_raw is a deprecated alias of 'con' and will be removed in a future release of 'stringi'.")
        con <- fname
    }

    if (is.character(con)) {
        con <- file(con, "rb")
        on.exit(close(con))
    }

    bufsize <- 4194304L
    data <- list()
    n <- 1L
    repeat {
        buf <- readBin(con, what = "raw", size = 1L, n = bufsize)
        data[[n]] <- buf
        n <- n + 1L
        if (length(buf) < bufsize)
            break
    }
    do.call(c, data)
}


# @title
# Read Text Lines from a Text File
#
# @description
# Reads a text file in ins entirety, re-encodes it, and splits it into text lines.
#
# @details
# This aims to be a substitute for the \code{\link{readLines}} function,
# with the ability to re-encode the input file in a much more robust way,
# and split the text into lines with \code{\link{ci_split_lines1}}
# (which conforms with the Unicode guidelines for newline markers).
#
# The function calls \code{\link{ci_read_raw}},
# \code{\link{ci_encode}}, and \code{\link{ci_split_lines1}},
# in this order.
#
# Because of the way this function is currently implemented,
# maximal file size cannot exceed ~0.67 GB.
#
# @param con name of the output file or a connection object
#        (opened in the binary mode)
# @param encoding single string; input encoding;
# \code{NULL} or \code{''} for the current default encoding.
# @param fname [DEPRECATED] alias of \code{con}
#
# @return
# Returns a character vector, each text line is a separate string.
# The output is always marked as UTF-8.
#
# @family files
# @export
ci_read_lines <- function(con, encoding = NULL,
    fname = con)
{
    if (!missing(fname) && missing(con)) { # DEPRECATED
        warning("The 'fname' argument in ci_read_lines is a deprecated alias of 'con' and will be removed in a future release of 'stringi'.")
        con <- fname
    }

    stopifnot(is.null(encoding) || is.character(encoding))

    if (is.null(encoding) || encoding == "")
        encoding <- NULL

    if (!is.null(encoding) && encoding == "auto")
        stop("encoding `auto` is no longer supported")  # TODO: remove in the future

    if (
        is.character(con) &&
        length(con) == 1L &&
        !is.null(encoding) &&
        length(encoding) == 1L &&
        !is.na(encoding) &&
        tolower(encoding) %in% c("utf-8", "utf8")
    ) {
        return(.Call(C_ci_read_lines, con, encoding))
    }

    txt <- ci_read_raw(con)
    txt <- ci_encode(txt, encoding, "UTF-8")
    ci_split_lines1(txt)
}


# @title
# Write Text Lines to a Text File
#
# @description
# Writes a text file is such a way that each element of a given
# character vector becomes a separate text line.
#
#
# @details
# It is a substitute for the \R \code{\link{writeLines}} function,
# with the ability to easily re-encode the output.
#
# We suggest using the UTF-8 encoding for all text files:
# thus, it is the default one for the output.
#
# @param str character vector with data to write
# @param con name of the output file or a connection object
#        (opened in the binary mode)
# @param encoding output encoding, \code{NULL} or \code{''} for
# the current default one
# @param sep newline separator
# @param fname [DEPRECATED] alias of \code{con}
#
# @return
# This function returns nothing noteworthy.
#
# @family files
# @export
