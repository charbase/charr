# Charr-owned cross-backend tests for boundary locations.

locate_first_boundaries <- function(...) charr_test_leaf("ci_locate_first_boundaries")(...)
locate_all_boundaries <- function(...) charr_test_leaf("ci_locate_all_boundaries")(...)

boundary_locate_input <- function(x) charr::str_trim(x)


test_that("boundary locate uses code-point positions for every iterator type", {
  emoji <- boundary_locate_input(
    "\U0001f469\u200d\U0001f469\u200d\U0001f467\u200d\U0001f466e\u0301"
  )
  cjk <- boundary_locate_input("日本語の文章です")
  thai <- boundary_locate_input("ภาษาไทยภาษาไทย")
  sentence <- boundary_locate_input("One. Two! Three?")
  expect_identical(charport::is_charvec(emoji), charr_altrep())
  expect_identical(charport::is_charvec(cjk), charr_altrep())
  expect_identical(charport::is_charvec(thai), charr_altrep())
  expect_identical(charport::is_charvec(sentence), charr_altrep())

  expect_identical(
    locate_all_boundaries(emoji, opts_brkiter = list(type = "character"))[[1L]],
    structure(c(1L, 8L, 7L, 9L), dim = c(2L, 2L),
      dimnames = list(NULL, c("start", "end")))
  )
  word_opts <- list(type = "word", skip_word_none = TRUE)
  expect_identical(
    locate_all_boundaries(cjk, opts_brkiter = word_opts)[[1L]],
    structure(c(1L, 4L, 5L, 7L, 3L, 4L, 6L, 8L),
      dim = c(4L, 2L), dimnames = list(NULL, c("start", "end")))
  )
  expect_identical(
    locate_all_boundaries(thai, opts_brkiter = word_opts)[[1L]],
    structure(c(1L, 5L, 8L, 12L, 4L, 7L, 11L, 14L),
      dim = c(4L, 2L), dimnames = list(NULL, c("start", "end")))
  )
  expect_identical(
    locate_first_boundaries(sentence,
      opts_brkiter = list(type = "sentence")),
    structure(c(1L, 5L), dim = c(1L, 2L),
      dimnames = list(NULL, c("start", "end")))
  )
})
test_that("boundary locate preserves empty, NA, and omit shapes", {
  strings <- boundary_locate_input(c("", NA))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  first <- locate_first_boundaries(strings, get_length = TRUE)
  expect_identical(first, structure(
    c(-1L, NA_integer_, -1L, NA_integer_), dim = c(2L, 2L),
    dimnames = list(NULL, c("start", "length"))
  ))
  all <- locate_all_boundaries(strings)
  expect_identical(all[[1L]], structure(
    c(NA_integer_, NA_integer_), dim = c(1L, 2L),
    dimnames = list(NULL, c("start", "end"))
  ))
  expect_identical(all[[2L]], all[[1L]])

  omitted <- locate_all_boundaries(
    strings, omit_no_match = TRUE, get_length = TRUE
  )
  expect_identical(dim(omitted[[1L]]), c(0L, 2L))
  expect_identical(omitted[[2L]], structure(
    c(NA_integer_, NA_integer_), dim = c(1L, 2L),
    dimnames = list(NULL, c("start", "length"))
  ))
})

test_that("boundary locate retains copied lazy custom-rule parsing", {
  strings <- boundary_locate_input(c("", NA))
  bad_rules <- list(type = "[")
  expect_no_error(
    locate_first_boundaries(strings, opts_brkiter = bad_rules)
  )
  expect_error(
    locate_all_boundaries(strings, opts_brkiter = bad_rules),
    "U_MALFORMED_SET"
  )
})


test_that("boundary locate is lenient for malformed declared UTF-8", {
  malformed <- rawToChar(as.raw(c(0x61, 0xc3, 0x28, 0x62)))
  Encoding(malformed) <- "UTF-8"
  malformed <- str_replace_all(malformed, fixed("\uffff"), "")
  expect_identical(charport::is_charvec(malformed), charr_altrep())
  opts <- list(type = "character")
  expect_identical(
    locate_all_boundaries(malformed, opts_brkiter = opts),
    stringi::stri_locate_all_boundaries(
      as.character(malformed), opts_brkiter = opts
    )
  )
})
