#' Read a text file as Unicode lines
#'
#' `str_read_lines()` reads a text file in its entirety, converts it to UTF-8,
#' and splits it at Unicode line boundaries.
#'
#' @param con A file name or a connection opened in binary mode.
#' @param encoding A single string giving the input encoding. `NULL` or `""`
#'   uses the current default encoding.
#' @return A character vector with one element per line.
#' @seealso [stringi::stri_read_lines()] for the underlying implementation.
#' @export
#' @examples
#' path <- tempfile()
#' writeLines(c("first", "second"), path, useBytes = TRUE)
#' str_read_lines(path, encoding = "UTF-8")
#' unlink(path)
str_read_lines <- function(con, encoding = NULL) {
  stri_read_lines(con, encoding = encoding)
}
