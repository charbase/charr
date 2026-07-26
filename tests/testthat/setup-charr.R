# This is a test-runner control, not a package startup interface.
test_backend <- Sys.getenv("CHARR_BACKEND", unset = "")
if (nzchar(test_backend)) {
  charr_backend(test_backend)
}
