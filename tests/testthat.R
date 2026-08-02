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

run_backend_tests <- function(backend) {
  message("== charr backend: ", backend, " ==")

  # test_check() sources setup-charr.R, which reads CHARR_BACKEND. Replace the
  # runner-only "all" value with the concrete backend for each pass.
  Sys.setenv(CHARR_BACKEND = backend)
  charr_backend(backend)

  # The top-level directory is the imported stringr suite. Charr-owned
  # semantic tests run from common under the same selected backend.
  test_check("charr")

  charr_backend(backend)
  run_charr_tests("testthat/charr/common")
}

backend_errors <- list()
for (backend in test_backends) {
  tryCatch(
    run_backend_tests(backend),
    error = function(error) {
      backend_errors[[backend]] <<- error
    }
  )
}

if (length(backend_errors) > 0L) {
  stop(
    "Test failures for backend(s): ",
    paste(names(backend_errors), collapse = ", "),
    call. = FALSE
  )
}
