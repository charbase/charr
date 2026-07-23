# charr-owned file: targeted equivalence tests for Reader-backed width.

width_cp <- function(x) charr:::ci_width(x)


test_that("width preserves ICU property and emoji-context semantics", {
  strings <- charr:::ci_replace_all_fixed(c(
    "a", "Ａ", "ｱ", "Ω", "ü", "\t", "🙂", "👩‍👧", "🇺🇸", "", NA
  ), "not present", "")
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    width_cp(strings),
    c(1L, 2L, 1L, 1L, 1L, 0L, 2L, 2L, 2L, 0L, NA_integer_)
  )
})


test_that("width stays inside adjacent Reader records", {
  strings <- charr:::ci_trim_both(c(
    " a ", " very_long_👩‍👧_record ", " z ", " ü ", " Ａ "
  ))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(width_cp(strings), c(1L, 19L, 1L, 1L, 2L))
})


test_that("width validates UTF-8 and rejects CE_BYTES", {
  malformed_byte <- rawToChar(as.raw(0xc3))
  Encoding(malformed_byte) <- "UTF-8"
  malformed <- charr:::ci_replace_all_fixed("a", "a", malformed_byte)
  expect_identical(charport::is_charvec(malformed), charr_altrep())
  expect_error(width_cp(malformed), "invalid UTF-8")

  bytes <- "\xff"
  Encoding(bytes) <- "bytes"
  expect_error(width_cp(bytes), "bytes encoding")
})
