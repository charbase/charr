# charr-owned file: targeted equivalence tests for Reader-backed conversion.



test_that("conv reads CHARVEC records and preserves marked conversion", {
  strings <- charr_test_leaf("ci_replace_all_fixed")(
    c("ascii", "aé", "ü", "🙂", "very_long_🙂_record", "", NA),
    "not present", ""
  )
  expect_identical(charport::is_charvec(strings), charr_altrep())

  utf8 <- charr_test_leaf("ci_conv")(strings, NULL, "UTF-8")
  expect_identical(utf8, strings)
  expect_identical(charport::is_charvec(utf8), charr_altrep())

  latin_input <- strings[c(1L, 2L, 6L, 7L)]
  latin_raw <- charr_test_leaf("ci_conv")(latin_input, NULL, "latin1", to_raw = TRUE)
  expect_identical(
    latin_raw,
    list(charToRaw("ascii"), as.raw(c(0x61, 0xe9)), raw(), NULL)
  )
  expect_warning(
    charr_test_leaf("ci_conv")("🙂", NULL, "latin1"),
    "cannot be converted"
  )
  latin <- suppressWarnings(charr_test_leaf("ci_conv")(strings, NULL, "latin1"))
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
    charr_test_leaf("ci_conv")(input, "UTF-8", "UTF-8"),
    c("abc", "é", "🙂", NA)
  )
  expect_identical(
    charr_test_leaf("ci_conv")(input, "UTF-8", "UTF-16LE", to_raw = TRUE),
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
    charr_test_leaf("ci_conv")(marked_bytes, "latin1", "UTF-8"),
    "aÿ"
  )
  expect_error(
    charr_test_leaf("ci_conv")(marked_bytes, NULL, "UTF-8"),
    "bytes encoding"
  )
})
