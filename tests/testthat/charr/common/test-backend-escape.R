# charr-owned file: targeted tests for Reader-backed escape operations.

backend_escape_unicode <- function(x) {
  with_test_backend(TRUE, charr_test_leaf("ci_escape_unicode")(x))
}

expect_escape_unmaterialized <- function(x) {
  expect_altrep_unmaterialized(x)
}

test_that("escape emits the copied Unicode escapes from charvec input", {
  values <- c(
    "a\u00e9\U0001f642u\u0308", "a\\\"'\t\n", "", NA_character_
  )
  subject <- charport::as_charvec(values)

  actual <- backend_escape_unicode(subject)
  expect_escape_unmaterialized(actual)
  expect_identical(actual, stringi::stri_escape_unicode(values))
  expect_escape_unmaterialized(subject)
})
test_that("escape preserves copied encoding validation", {
  bytes <- rawToChar(as.raw(0xff))
  Encoding(bytes) <- "bytes"
  bytes_subject <- charport::as_charvec(bytes)
  malformed <- rawToChar(as.raw(0xc3))
  Encoding(malformed) <- "UTF-8"
  malformed_subject <- charport::as_charvec(malformed)

  expect_error(backend_escape_unicode(bytes_subject), "bytes encoding")
  expect_error(backend_escape_unicode(malformed_subject), "invalid UTF-8")
  expect_escape_unmaterialized(bytes_subject)
  expect_escape_unmaterialized(malformed_subject)
})
