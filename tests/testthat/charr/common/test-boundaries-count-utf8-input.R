# Charr-owned coverage for the optimized UTF-8 boundary-count input path.

utf8_boundary_count <- function(x) {
  str_count(x, boundary("character"))
}

utf8_boundary_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


test_that("boundary count preserves UTF-8 input normalization", {
  latin1 <- utf8_boundary_marked(c(0x63, 0x61, 0x66, 0xe9), "latin1")
  values <- c(latin1, "\ufeffabc", "\ufeff\ufeffabc", "", NA_character_)
  input <- charport::as_charvec(values)

  expected <- with_backend("stringi", utf8_boundary_count(values))
  actual <- with_backend(selected_test_backend, utf8_boundary_count(input))

  expect_identical(expected, c(4L, 3L, 4L, 0L, NA_integer_))
  expect_identical(actual, expected)
  if (identical(selected_test_backend, "altrep")) {
    expect_false(charport::charport_info(input)$is_materialized)
  }
})


test_that("boundary count rejects bytes input", {
  bytes <- utf8_boundary_marked(c(0xff, 0xfe), "bytes")
  input <- charport::as_charvec(bytes)

  expect_error(
    with_backend(selected_test_backend, utf8_boundary_count(input)),
    "bytes encoding"
  )
  if (identical(selected_test_backend, "altrep")) {
    expect_false(charport::charport_info(input)$is_materialized)
  }
})
