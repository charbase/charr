# charr-owned file: targeted equivalence tests for Reader-backed escaping.

escape_unicode <- function(x) charr:::ci_escape_unicode(x)


test_that("escape_unicode emits exact ASCII escapes from CHARVEC records", {
  strings <- charr:::ci_replace_all_fixed(
    c("aé🙂ü", "a\\\"'\t\n", "", NA, "very_long_🙂_record"),
    "not present", ""
  )
  expect_identical(charport::is_charvec(strings), charr_altrep())

  result <- escape_unicode(strings)
  expect_identical(
    result,
    c("a\\u00e9\\U0001f642u\\u0308", "a\\\\\\\"\\'\\t\\n", "", NA,
      "very_long_\\U0001f642_record")
  )
  expect_identical(Encoding(result), rep("unknown", 5L))
  expect_identical(charport::is_charvec(result), charr_altrep())
})






test_that("escape kernels preserve validation", {
  bytes <- "\xff"
  Encoding(bytes) <- "bytes"
  expect_error(escape_unicode(bytes), "bytes encoding")

  malformed_byte <- rawToChar(as.raw(0xc3))
  Encoding(malformed_byte) <- "UTF-8"
  malformed <- charr:::ci_replace_all_fixed("a", "a", malformed_byte)
  expect_error(escape_unicode(malformed), "invalid UTF-8")

})
