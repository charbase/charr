# charr-owned file: targeted equivalence tests for Reader-backed escaping.

escape_unicode <- function(x) charr:::ci_escape_unicode(x)
unescape_unicode <- function(x) charr:::ci_unescape_unicode(x)


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


test_that("unescape_unicode keeps its UTF-16 conversion semantics", {
  strings <- charr:::ci_trim_both(c(
    " \\u00e9\\U0001f642u\\u0308 ", " a\\t\\n ", " \\q ", " ", NA
  ))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  result <- unescape_unicode(strings)
  expect_identical(result, c("é🙂ü", "a\t\n", "q", "", NA))
  expect_true(charport::is_charvec(result))

  malformed <- rawToChar(as.raw(0xc3))
  Encoding(malformed) <- "UTF-8"
  malformed <- charr:::ci_replace_all_fixed("a", "a", malformed)
  expect_identical(unescape_unicode(malformed), "�")

  bom_literal <- rawToChar(as.raw(c(0xef, 0xbb, 0xbf, 0x61)))
  Encoding(bom_literal) <- "UTF-8"
  bom <- unescape_unicode(bom_literal)
  expect_true(charport::is_charvec(bom))
  expect_identical(unescape_unicode(bom), "﻿a")
})


test_that("unescape_unicode warns once per invalid escape", {
  strings <- charr:::ci_trim_both(c(" \\x ", " ok ", " \\U00110000 ", NA))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  warnings <- character()
  result <- withCallingHandlers(
    unescape_unicode(strings),
    warning = function(cnd) {
      warnings <<- c(warnings, conditionMessage(cnd))
      invokeRestart("muffleWarning")
    }
  )
  expect_length(warnings, 2L)
  expect_true(all(grepl("invalid escape sequence", warnings, fixed = TRUE)))
  expect_identical(result, c(NA, "ok", NA, NA))
})


test_that("escape kernels preserve validation", {
  bytes <- "\xff"
  Encoding(bytes) <- "bytes"
  expect_error(escape_unicode(bytes), "bytes encoding")
  expect_error(unescape_unicode(bytes), "bytes encoding")

  malformed_byte <- rawToChar(as.raw(0xc3))
  Encoding(malformed_byte) <- "UTF-8"
  malformed <- charr:::ci_replace_all_fixed("a", "a", malformed_byte)
  expect_error(escape_unicode(malformed), "invalid UTF-8")

})
