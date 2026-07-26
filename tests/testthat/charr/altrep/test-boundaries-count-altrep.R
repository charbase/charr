# charr-owned targeted equivalence tests for Reader-backed ICU boundaries.

count_boundaries <- function(...) charr:::ci_count_boundaries(...)

boundary_count_charvec <- function(x) charr::str_trim(x)


test_that("boundary count reads CHARVEC records for every iterator type", {
  strings <- boundary_count_charvec(c(
    "日本語の文章です", "ภาษาไทยภาษาไทย", "👩‍👩‍👧‍👦e\u0301",
    "One. Two! Three?", "", NA
  ))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    count_boundaries(strings, opts_brkiter = list(type = "character")),
    c(8L, 14L, 2L, 16L, 0L, NA_integer_)
  )
  expect_identical(
    count_boundaries(strings, opts_brkiter = list(type = "line_break")),
    c(8L, 4L, 2L, 3L, 0L, NA_integer_)
  )
  expect_identical(
    count_boundaries(strings, opts_brkiter = list(type = "sentence")),
    c(1L, 1L, 1L, 3L, 0L, NA_integer_)
  )
  expect_identical(
    count_boundaries(strings, opts_brkiter = list(
      type = "word", skip_word_none = TRUE
    )),
    c(4L, 4L, 1L, 3L, 0L, NA_integer_)
  )
})

test_that("boundary count keeps dictionary engines and custom rules", {
  dictionary <- boundary_count_charvec(c(
    "日本語の文章です", "ภาษาไทยภาษาไทย"
  ))
  expect_identical(charport::is_charvec(dictionary), charr_altrep())
  expect_identical(
    count_boundaries(dictionary, opts_brkiter = list(
      type = "word", skip_word_none = TRUE, locale = "ja"
    )),
    c(4L, 4L)
  )

  rules <- paste0(
    "$letters = [[:L:]]; $numbers = [[:N:]]; ",
    "$letters+; $numbers+; .;"
  )
  custom <- boundary_count_charvec(c("abc 123", "é42"))
  expect_identical(
    count_boundaries(custom, opts_brkiter = list(type = rules)),
    c(3L, 2L)
  )
})


test_that("boundary count is lenient for malformed declared UTF-8", {
  malformed <- rawToChar(as.raw(c(0xc3, 0x28)))
  Encoding(malformed) <- "UTF-8"
  malformed <- str_replace_all(malformed, fixed("\uffff"), "")
  expect_identical(charport::is_charvec(malformed), charr_altrep())
  expect_identical(
    count_boundaries(malformed, opts_brkiter = list(type = "word")),
    2L
  )
})


test_that("boundary locale fallback warns once per call", {
  strings <- boundary_count_charvec(rep("abc", 64L))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  warnings <- character()
  value <- withCallingHandlers(
    count_boundaries(strings, opts_brkiter = list(
      type = "word", locale = "zz_ZZ"
    )),
    warning = function(w) {
      warnings <<- c(warnings, conditionMessage(w))
      invokeRestart("muffleWarning")
    }
  )
  expect_identical(value, rep(1L, 64L))
  expect_length(warnings, 1L)
  expect_match(warnings, "resource bundle")
})
