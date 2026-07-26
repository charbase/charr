# charr-owned file: targeted equivalence tests for Reader-backed conversion.

to_utf8 <- function(x, unknown = FALSE, validate = FALSE) {
  charr:::ci_enc_toutf8(x, unknown, validate)
}
to_ascii <- function(x) charr:::ci_enc_toascii(x)
to_utf32 <- function(x) charr:::ci_enc_toutf32(x)
from_utf32 <- function(x) charr:::ci_enc_fromutf32(x)


test_that("conv reads CHARVEC records and preserves marked conversion", {
  strings <- charr:::ci_replace_all_fixed(
    c("ascii", "aé", "ü", "🙂", "very_long_🙂_record", "", NA),
    "not present", ""
  )
  expect_identical(charport::is_charvec(strings), charr_altrep())

  utf8 <- charr:::ci_conv(strings, NULL, "UTF-8")
  expect_identical(utf8, strings)
  expect_identical(charport::is_charvec(utf8), charr_altrep())

  latin_input <- strings[c(1L, 2L, 6L, 7L)]
  latin_raw <- charr:::ci_conv(latin_input, NULL, "latin1", to_raw = TRUE)
  expect_identical(
    latin_raw,
    list(charToRaw("ascii"), as.raw(c(0x61, 0xe9)), raw(), NULL)
  )
  expect_warning(
    charr:::ci_conv("🙂", NULL, "latin1"),
    "cannot be converted"
  )
  latin <- suppressWarnings(charr:::ci_conv(strings, NULL, "latin1"))
  expect_identical(charport::is_charvec(latin), charr_altrep())
})


test_that("conv handles explicit raw source encodings and raw output", {
  input <- list(
    charToRaw("abc"),
    as.raw(c(0xc3, 0xa9)),
    as.raw(c(0xf0, 0x9f, 0x99, 0x82)),
    NULL
  )
  expect_identical(
    charr:::ci_conv(input, "UTF-8", "UTF-8"),
    c("abc", "é", "🙂", NA)
  )
  expect_identical(
    charr:::ci_conv(input, "UTF-8", "UTF-16LE", to_raw = TRUE),
    list(
      as.raw(c(0x61, 0x00, 0x62, 0x00, 0x63, 0x00)),
      as.raw(c(0xe9, 0x00)),
      as.raw(c(0x3d, 0xd8, 0x42, 0xde)),
      NULL
    )
  )

  marked_bytes <- rawToChar(as.raw(c(0x61, 0xff)))
  Encoding(marked_bytes) <- "bytes"
  expect_identical(
    charr:::ci_conv(marked_bytes, "latin1", "UTF-8"),
    "aÿ"
  )
  expect_error(
    charr:::ci_conv(marked_bytes, NULL, "UTF-8"),
    "bytes encoding"
  )
})


test_that("UTF-8 and ASCII helpers retain their operation-specific rules", {
  strings <- charr:::ci_trim_both(c(" aé🙂 ", " x ", " ", NA))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(to_utf8(strings), c("aé🙂", "x", "", NA))
  expect_identical(to_ascii(strings), c("a\032\032", "x", "", NA))

  latin <- iconv("é", from = "UTF-8", to = "latin1")
  Encoding(latin) <- "latin1"
  bytes <- rawToChar(as.raw(c(0x61, 0xff)))
  Encoding(bytes) <- "bytes"
  expect_error(to_utf8(bytes), "bytes encoding")
  expect_identical(to_utf8(c(latin, bytes), unknown = TRUE), c("�", "a�"))
  expect_identical(to_ascii(c(latin, bytes)), c("\032", "a\032"))

  malformed <- rawToChar(as.raw(c(0x61, 0xc3)))
  Encoding(malformed) <- "UTF-8"
  expect_warning(
    repaired <- to_utf8(malformed, validate = TRUE),
    "fixing"
  )
  expect_identical(repaired, "a�")
  expect_warning(
    missing <- to_utf8(malformed, validate = NA),
    "setting string to NA"
  )
  expect_identical(missing, NA_character_)
  expect_warning(to_ascii(malformed), "fixing")
})


test_that("UTF-32 helpers preserve invalid-code-point handling", {
  strings <- charr:::ci_replace_all_fixed(
    c("aé🙂", "ü", "", NA), "not present", ""
  )
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(
    to_utf32(strings),
    list(c(97L, 233L, 128578L), c(117L, 776L), integer(), NULL)
  )
  expect_identical(
    from_utf32(list(c(97L, 233L, 128578L), integer(), NULL)),
    c("aé🙂", "", NA)
  )
  expect_warning(from_utf32(list(0L)), "invalid Unicode code point")
  invalid <- suppressWarnings(
    from_utf32(list(0L, c(0x110000L), c(0xd800L)))
  )
  expect_identical(invalid, rep(NA_character_, 3L))

  malformed <- rawToChar(as.raw(0xc3))
  Encoding(malformed) <- "UTF-8"
  expect_error(to_utf32(malformed), "invalid UTF-8")
})
