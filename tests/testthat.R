library(testthat)
library(charr)

requested_backend <- Sys.getenv("CHARR_BACKEND", unset = "")
test_backends <- if (identical(requested_backend, "all")) {
  c("stringi", "base", "altrep")
} else if (nzchar(requested_backend)) {
  requested_backend
} else {
  charr_backend()
}

run_charr_tests <- function(path) {
  test_env <- new.env(parent = globalenv())
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

prepare_backend <- function(backend) {
  # test_check() sources setup-charr.R, which reads CHARR_BACKEND. Replace the
  # runner-only "all" value with the concrete backend for each pass.
  Sys.setenv(CHARR_BACKEND = backend)
  charr_backend(backend)
}

run_backend_suite <- function(backend, suite) {
  message("== charr backend: ", backend, "; suite: ", suite, " ==")
  prepare_backend(backend)

  if (identical(suite, "imported")) {
    test_check("charr")
    return(invisible(NULL))
  }
  if (!identical(suite, "common")) {
    stop("unknown test suite: ", suite, call. = FALSE)
  }

  run_charr_tests("testthat/charr/common")
}

suite_errors <- list()
for (backend in test_backends) {
  for (suite in c("imported", "common")) {
    key <- paste(backend, suite, sep = "/")
    tryCatch(
      run_backend_suite(backend, suite),
      error = function(error) {
        suite_errors[[key]] <<- error
      }
    )
  }
}

if (length(suite_errors) > 0L) {
  stop(
    "Test failures for backend/suite: ",
    paste(names(suite_errors), collapse = ", "),
    call. = FALSE
  )
}
