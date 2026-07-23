# charr-owned targeted equivalence tests for Reader-backed boundary locate.

locate_first_boundaries <- function(...) charr:::ci_locate_first_boundaries(...)
locate_last_boundaries <- function(...) charr:::ci_locate_last_boundaries(...)
locate_all_boundaries <- function(...) charr:::ci_locate_all_boundaries(...)

boundary_locate_charvec <- function(x) charr::str_trim(x)


test_that("boundary locate uses code-point positions for every iterator type", {
  emoji <- boundary_locate_charvec("👩‍👩‍👧‍👦e\u0301")
  cjk <- boundary_locate_charvec("日本語の文章です")
  thai <- boundary_locate_charvec("ภาษาไทยภาษาไทย")
  sentence <- boundary_locate_charvec("One. Two! Three?")
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
  expect_identical(
    locate_last_boundaries(cjk,
      opts_brkiter = list(type = "line_break"), get_length = TRUE),
    structure(c(8L, 1L), dim = c(1L, 2L),
      dimnames = list(NULL, c("start", "length")))
  )
})
test_that("boundary locate preserves empty, NA, and omit shapes", {
  strings <- boundary_locate_charvec(c("", NA))
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
  strings <- boundary_locate_charvec(c("", NA))
  bad_rules <- list(type = "[")
  expect_no_error(
    locate_first_boundaries(strings, opts_brkiter = bad_rules)
  )
  expect_no_error(
    locate_last_boundaries(strings, opts_brkiter = bad_rules)
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


test_that("revealed boundary locate-last matches stringi on seeded cases", {
  set.seed(20260716)
  atoms <- c(
    "a", " ", ". ", "é", "e\u0301", "👩‍👩‍👧‍👦",
    "日本語", "文章", "ภาษา", "ไทย", "! "
  )
  raw <- vapply(seq_len(600L), function(i) {
    paste0(sample(atoms, sample.int(8L, 1L) - 1L, replace = TRUE),
      collapse = "")
  }, character(1L))
  raw[seq(19L, 600L, 19L)] <- ""
  raw[seq(37L, 600L, 37L)] <- NA_character_
  strings <- boundary_locate_charvec(raw)
  expect_identical(charport::is_charvec(strings), charr_altrep())

  option_cases <- list(
    list(type = "character", locale = "en"),
    list(type = "line_break", locale = "ja"),
    list(type = "sentence", locale = "en_US"),
    list(type = "word", skip_word_none = TRUE, locale = "th")
  )
  for (opts in option_cases) {
    for (get_length in c(FALSE, TRUE)) {
      expect_identical(
        suppressWarnings(locate_last_boundaries(
          strings, get_length = get_length, opts_brkiter = opts
        )),
        suppressWarnings(stringi::stri_locate_last_boundaries(
          as.character(strings), get_length = get_length,
          opts_brkiter = opts
        ))
      )
    }
  }
})
