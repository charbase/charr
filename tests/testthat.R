library(testthat)
library(charr)

test_backend <- Sys.getenv("CHARR_BACKEND", unset = "")
if (nzchar(test_backend)) {
  charr_backend(test_backend)
}
selected_backend <- charr_backend()

# The top-level directory is the imported stringr suite. Charr-owned tests are
# kept separately so implementation-specific ALTREP tests do not masquerade as
# coverage of the base or stringi routes.
test_check("charr")

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

charr_backend(selected_backend)
run_charr_tests("testthat/charr/common")

if (identical(charr_backend(), "altrep")) {
  run_charr_tests("testthat/charr/altrep")
}
