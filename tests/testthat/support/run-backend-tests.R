arguments <- commandArgs(trailingOnly = TRUE)
if (length(arguments) != 1L ||
    !arguments[[1L]] %in% c("stringi", "base", "altrep")) {
  stop("expected one backend argument: stringi, base, or altrep", call. = FALSE)
}
backend <- arguments[[1L]]

library(testthat)
library(charr)

charr_backend(backend)

run_test_directory <- function(path) {
  test_env <- testthat::test_env("charr")
  sys.source("testthat/helper-charr.R", envir = test_env)
  test_dir(
    path,
    package = "charr",
    reporter = check_reporter(),
    env = test_env,
    load_helpers = FALSE,
    load_package = "installed",
    stop_on_failure = TRUE
  )
}

suite_paths <- c(
  `stringr-imported` = "testthat/stringr-imported",
  common = "testthat/charr/common"
)
suite_errors <- list()
for (suite in names(suite_paths)) {
  message("== charr backend: ", backend, "; suite: ", suite, " ==")
  tryCatch(
    run_test_directory(suite_paths[[suite]]),
    error = function(error) {
      suite_errors[[suite]] <<- error
    }
  )
}

if (length(suite_errors) > 0L) {
  stop(
    "test failures for backend/suite: ",
    paste(paste(backend, names(suite_errors), sep = "/"), collapse = ", "),
    call. = FALSE
  )
}
