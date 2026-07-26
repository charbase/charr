#' Reverse strings by code point
#'
#' `str_reverse()` reverses the order of the Unicode code points in each
#' string.
#'
#' @inheritParams str_detect
#' @return A character vector the same length as `string`.
#' @seealso [stringi::stri_reverse()] for the underlying implementation.
#' @export
#' @examples
#' str_reverse(c("abc", "caf\u00e9", NA))
str_reverse <- function(string) {
  copy_names(string, stri_reverse(string))
}
