# charr-owned targeted equivalence tests for the Reader-backed regex detect
# foundation. The OFF route is installed stringi; ON is charr's ICU backend.

detect_regex <- function(...) charr:::ci_detect_regex(...)


test_that("regex detect uses length-delimited UTF-8 subjects and full ICU modes", {
  strings <- charr:::ci_trim_both(c(
    " café ", " 😀a ", " 𐐷Z ", " ü ", " WORD ", NA_character_
  ))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    detect_regex(strings, c("é", "a$", "𐐷", "\\p{M}", "(?wi)word", "x")),
    c(TRUE, TRUE, TRUE, TRUE, TRUE, NA)
  )
  expect_identical(
    detect_regex(strings, "é|😀|𐐷|u", negate = TRUE),
    c(FALSE, FALSE, FALSE, FALSE, TRUE, NA)
  )
})
test_that("regex detect preserves recycling, NA, zero-length, and max_count", {
  strings <- charr:::ci_trim_both(c(" a ", " b ", " 😀 ", "", NA, " aa "))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    detect_regex(strings, c("^a", "(?=b)")),
    c(TRUE, TRUE, FALSE, FALSE, NA, FALSE)
  )
  expect_identical(
    detect_regex(strings, "a", negate = TRUE, max_count = 2L),
    c(FALSE, TRUE, TRUE, NA, NA, NA)
  )
  expect_identical(
    detect_regex(strings, "a", max_count = 1L),
    c(TRUE, NA, NA, NA, NA, NA)
  )
})

test_that("regex detect preserves eager encoding and lazy compile errors", {
  present <- charr:::ci_trim_both(" x ")
  missing <- charr:::ci_trim_both(NA_character_)
  exhausted <- charr:::ci_trim_both(c(" x ", " x "))
  expect_identical(charport::is_charvec(present), charr_altrep())
  expect_identical(charport::is_charvec(missing), charr_altrep())
  expect_identical(charport::is_charvec(exhausted), charr_altrep())

  expect_error(
    detect_regex(present, "["),
    "Missing closing bracket.*U_REGEX_MISSING_CLOSE_BRACKET.*context=`\\[`")
  expect_identical(detect_regex(missing, "["), NA)
  expect_identical(detect_regex(present, "[", max_count = 0L), NA)
  expect_identical(
    detect_regex(exhausted, c("x", "["), max_count = 1L),
    c(TRUE, NA)
  )

  bytes <- charr:::ci_conv("Ą", "UTF-8", "ISO-8859-2")
  expect_identical(charport::is_charvec(bytes), charr_altrep())
  expect_error(
    detect_regex(bytes, "x", max_count = 0L),
    "bytes encoding is not supported"
  )
})
