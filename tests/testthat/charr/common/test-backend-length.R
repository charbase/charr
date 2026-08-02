# charr-owned tests for Reader-backed length operations. These are not
# imported from stringr.

length_marked_string <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_length_input_unmaterialized <- function(x) {
  expect_altrep_unmaterialized(x)
}

test_that("length and width read an unmaterialized charvec", {
  latin1 <- length_marked_string(0xe9, "latin1")
  values <- c(
    "", NA_character_, "caf\u00e9", "u\u0308", "\U0001f642", latin1
  )
  expected_length <- with_test_backend(FALSE, str_length(values))
  expected_width <- with_test_backend(FALSE, str_width(values))
  input <- charport::as_charvec(values)

  expect_length_input_unmaterialized(input)
  expect_identical(
    with_test_backend(TRUE, str_length(input)),
    expected_length
  )
  expect_length_input_unmaterialized(input)
  expect_identical(
    with_test_backend(TRUE, str_width(input)),
    expected_width
  )
  expect_length_input_unmaterialized(input)
})

test_that("empty charvec inputs avoid materialization", {
  input <- charport::as_charvec(character())

  expect_identical(with_test_backend(TRUE, str_length(input)), integer())
  expect_identical(with_test_backend(TRUE, str_width(input)), integer())
  expect_length_input_unmaterialized(input)
})

test_that("length counts a leading BOM while width ignores it", {
  value <- enc2utf8("\ufeffa")
  expected_length <- with_test_backend(FALSE, str_length(value))
  expected_width <- with_test_backend(FALSE, str_width(value))
  input <- charport::as_charvec(value)

  expect_identical(expected_length, 2L)
  expect_identical(with_test_backend(TRUE, str_length(input)), expected_length)
  expect_length_input_unmaterialized(input)
  expect_identical(expected_width, 1L)
  expect_identical(with_test_backend(TRUE, str_width(input)), expected_width)
  expect_length_input_unmaterialized(input)
})

test_that("length and width reject bytes and invalid UTF-8", {
  bytes_value <- length_marked_string(c(0xff, 0xfe), "bytes")
  malformed_value <- length_marked_string(c(0x61, 0xc3), "UTF-8")
  bytes <- charport::as_charvec(bytes_value)
  malformed <- charport::as_charvec(malformed_value)

  expect_error(with_test_backend(FALSE, str_length(bytes_value)), "bytes encoding")
  expect_error(with_test_backend(TRUE, str_length(bytes)), "bytes encoding")
  expect_length_input_unmaterialized(bytes)
  expect_error(with_test_backend(FALSE, str_width(bytes_value)), "bytes encoding")
  expect_error(with_test_backend(TRUE, str_width(bytes)), "bytes encoding")
  expect_length_input_unmaterialized(bytes)

  expect_error(
    with_test_backend(FALSE, str_length(malformed_value)),
    "invalid UTF-8"
  )
  expect_error(with_test_backend(TRUE, str_length(malformed)), "invalid UTF-8")
  expect_length_input_unmaterialized(malformed)
  expect_error(
    with_test_backend(FALSE, str_width(malformed_value)),
    "invalid UTF-8"
  )
  expect_error(with_test_backend(TRUE, str_width(malformed)), "invalid UTF-8")
  expect_length_input_unmaterialized(malformed)
})

test_that("length trusts ASCII canonicalization but rejects real bytes", {
  ascii <- "abc"
  Encoding(ascii) <- "bytes"
  expect_identical(Encoding(ascii), "unknown")
  ascii_input <- charport::as_charvec(ascii)

  expect_identical(with_test_backend(FALSE, str_length(ascii)), 3L)
  expect_identical(with_test_backend(TRUE, str_length(ascii_input)), 3L)
  expect_length_input_unmaterialized(ascii_input)

  utf8_bytes <- length_marked_string(c(0xc3, 0xa9), "bytes")
  utf8_bytes_input <- charport::as_charvec(utf8_bytes)
  expect_error(
    with_test_backend(FALSE, str_length(utf8_bytes)),
    "bytes encoding"
  )
  expect_error(
    with_test_backend(TRUE, str_length(utf8_bytes_input)),
    "bytes encoding"
  )
  expect_length_input_unmaterialized(utf8_bytes_input)
})

test_that("length validates UTF-8 boundary sequences", {
  valid <- list(
    c(0xc2, 0x80), c(0xdf, 0xbf),
    c(0xe0, 0xa0, 0x80), c(0xed, 0x9f, 0xbf), c(0xee, 0x80, 0x80),
    c(0xf0, 0x90, 0x80, 0x80), c(0xf4, 0x8f, 0xbf, 0xbf)
  )
  for (bytes in valid) {
    value <- length_marked_string(bytes, "UTF-8")
    input <- charport::as_charvec(value)
    expected <- with_test_backend(FALSE, str_length(value))

    expect_identical(expected, 1L)
    expect_identical(with_test_backend(TRUE, str_length(input)), expected)
    expect_length_input_unmaterialized(input)
  }

  invalid <- list(
    c(0x80), c(0xff, 0xfe), c(0xc0, 0x80), c(0xc1, 0xbf),
    c(0xe0, 0x9f, 0xbf), c(0xed, 0xa0, 0x80),
    c(0xf0, 0x8f, 0xbf, 0xbf), c(0xf4, 0x90, 0x80, 0x80),
    c(0xf5, 0x80, 0x80, 0x80), c(0xe2, 0x82), c(0xf0, 0x90, 0x80)
  )
  for (bytes in invalid) {
    value <- length_marked_string(bytes, "UTF-8")
    input <- charport::as_charvec(value)

    expect_error(with_test_backend(FALSE, str_length(value)), "invalid UTF-8")
    expect_error(with_test_backend(TRUE, str_length(input)), "invalid UTF-8")
    expect_length_input_unmaterialized(input)
  }
})

test_that("length handles large vectors and invalid elements at any position", {
  values <- rep(
    c("abc", "café", "\U0001f600", "汉字", "", "ü"),
    5000L
  )
  subject <- charport::as_charvec(values)
  expected <- rep(c(3L, 4L, 1L, 2L, 0L, 2L), 5000L)

  expect_identical(with_test_backend(FALSE, str_length(values)), expected)
  expect_identical(with_test_backend(TRUE, str_length(subject)), expected)
  expect_length_input_unmaterialized(subject)

  make_invalid <- function(position, n = 20000L) {
    value <- rep("abc", n)
    value[[position]] <- length_marked_string(0xff, "UTF-8")
    value
  }
  for (position in c(1L, 12345L, 20000L)) {
    value <- make_invalid(position)
    input <- charport::as_charvec(value)

    expect_error(with_test_backend(FALSE, str_length(value)), "invalid UTF-8")
    expect_error(with_test_backend(TRUE, str_length(input)), "invalid UTF-8")
    expect_length_input_unmaterialized(input)
  }
})
