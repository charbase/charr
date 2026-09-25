#' Write a character vector as lines to a file
#'
#' `str_write_lines()` writes each element of `string` followed by `sep`,
#' including the last one, and converts the output to `encoding`. It is the
#' counterpart of [str_read_lines()].
#'
#' Missing values are an error. Strings marked with the `"bytes"` encoding
#' are an error, as in [str_c()]. Characters that the output encoding cannot
#' represent are written as that encoding's substitution character, with a
#' warning.
#'
#' @param string A character vector to write.
#' @param con A file name or a connection opened in binary mode.
#' @param encoding A single string giving the output encoding. `NULL` or `""`
#'   uses the current default encoding.
#' @param sep A single string written after each element. Defaults to `"\n"`
#'   on all platforms.
#' @return `string`, invisibly and unchanged.
#' @seealso [str_read_lines()]; [stringi::stri_write_lines()], which writes
#'   an empty file instead of signalling an error for missing values.
#' @export
#' @examples
#' path <- tempfile()
#' str_write_lines(c("first", "second"), path)
#' str_read_lines(path, encoding = "UTF-8")
#' unlink(path)
str_write_lines <- function(string, con, encoding = "UTF-8", sep = "\n") {
  check_character(string)
  if (!is_string(con) && !inherits(con, "connection")) {
    stop_input_type(con, "a single string or a connection")
  }
  if (inherits(con, "connection") && !isOpen(con)) {
    cli::cli_abort("{.arg con} must be a connection opened in binary mode.")
  }
  check_string(encoding, allow_null = TRUE)
  check_string(sep)

  if (identical(charr_backend(), "stringi") && anyNA(string)) {
    cli::cli_abort("{.arg string} must not contain missing values.")
  }
  stri_write_lines(string, con, encoding = encoding, sep = sep)
  invisible(string)
}
