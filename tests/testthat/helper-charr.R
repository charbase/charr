selected_test_backend <- charr_backend()

with_backend <- function(backend, code) {
  old <- charr_backend(backend)
  on.exit(charr_backend(old), add = TRUE)
  force(code)
}

charr_altrep <- function(on = NULL) {
  if (is.null(on)) {
    return(identical(charr_backend(), "altrep"))
  }

  backend <- if (is.character(on)) {
    on
  } else if (isTRUE(on)) {
    "altrep"
  } else {
    "stringi"
  }
  invisible(charr_backend(backend))
}

with_altrep <- function(on, code) {
  backend <- if (is.character(on)) {
    on
  } else if (isTRUE(on)) {
    "altrep"
  } else {
    "stringi"
  }
  with_backend(backend, code)
}
