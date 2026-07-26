# Charr-owned tests for the public reversal operation.

test_that("str_reverse dispatches without materializing its input", {
  values <- c(
    first = "aé🙂üz", second = "a", third = "very_long_🙂_record",
    fourth = "z", fifth = NA, sixth = ""
  )
  expected <- with_backend("stringi", str_reverse(values))
  input <- charport::as_charvec(values)
  result <- with_backend(selected_test_backend, str_reverse(input))

  expect_false(charport::is_charvec(expected))
  expect_identical(
    charport::is_charvec(result),
    identical(selected_test_backend, "altrep")
  )
  if (identical(selected_test_backend, "altrep")) {
    expect_false(charport::charport_info(input)$is_materialized)
    expect_false(charport::charport_info(result)$is_materialized)
  }
  expect_identical(result, expected)
  expect_identical(
    unname(result),
    c("z̈u🙂éa", "a", "drocer_🙂_gnol_yrev", "z", NA, "")
  )
  expect_identical(names(result), names(values))
})


test_that("str_reverse preserves stringi validation and CE_BYTES errors", {
  malformed_byte <- rawToChar(as.raw(0xc3))
  Encoding(malformed_byte) <- "UTF-8"
  malformed <- charport::as_charvec(malformed_byte)
  expect_error(
    with_backend("stringi", str_reverse(malformed_byte)),
    "invalid UTF-8"
  )
  expect_error(
    with_backend(selected_test_backend, str_reverse(malformed)),
    "invalid UTF-8"
  )
  if (identical(selected_test_backend, "altrep")) {
    expect_false(charport::charport_info(malformed)$is_materialized)
  }

  bytes <- "\xff"
  Encoding(bytes) <- "bytes"
  expect_error(
    with_backend("stringi", str_reverse(bytes)),
    "bytes encoding"
  )
  expect_error(
    with_backend(
      selected_test_backend,
      str_reverse(charport::as_charvec(bytes))
    ),
    "bytes encoding"
  )
})
