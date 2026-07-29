# charr-owned targeted equivalence tests for Reader/Builder boundary extract.

extract_first_boundaries <- function(...) charr:::ci_extract_first_boundaries(...)
extract_all_boundaries <- function(...) charr:::ci_extract_all_boundaries(...)

boundary_extract_charvec <- function(x) charr::str_trim(x)


test_that("boundary extract emits every ICU iterator type through Builders", {
  strings <- boundary_extract_charvec(c(
    "👩‍👩‍👧‍👦e\u0301", "日本語の文章です", "ภาษาไทยภาษาไทย",
    "One. Two! Three?", "", NA
  ))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  character <- extract_all_boundaries(
    strings[1L], opts_brkiter = list(type = "character")
  )
  expect_identical(character, list(c("👩‍👩‍👧‍👦", "e\u0301")))
  expect_identical(
    charport::is_charvec(character[[1L]]), charr_altrep()
  )

  word_opts <- list(type = "word", skip_word_none = TRUE)
  cjk <- extract_all_boundaries(strings[2L], opts_brkiter = word_opts)
  thai <- extract_all_boundaries(strings[3L], opts_brkiter = word_opts)
  expect_identical(cjk, list(c("日本語", "の", "文章", "です")))
  expect_identical(thai, list(c("ภาษา", "ไทย", "ภาษา", "ไทย")))
  expect_identical(
    extract_all_boundaries(strings[4L],
      opts_brkiter = list(type = "sentence")),
    list(c("One. ", "Two! ", "Three?"))
  )
  expect_identical(
    extract_first_boundaries(strings[2L],
      opts_brkiter = list(type = "line_break")),
    "日"
  )
})
test_that("boundary extract honors skip rule-status ranges", {
  string <- boundary_extract_charvec("abc 123 日本語 カナ")
  expect_identical(charport::is_charvec(string), charr_altrep())
  base <- list(type = "word", skip_word_none = TRUE)

  expect_identical(
    extract_all_boundaries(string, opts_brkiter = base),
    list(c("abc", "123", "日本語", "カナ"))
  )
  expect_identical(
    extract_all_boundaries(string, opts_brkiter = c(
      base, skip_word_letter = TRUE
    )),
    list(c("123", "日本語", "カナ"))
  )
  expect_identical(
    extract_all_boundaries(string, opts_brkiter = c(
      base, skip_word_number = TRUE
    )),
    list(c("abc", "日本語", "カナ"))
  )
  expect_identical(
    extract_all_boundaries(string, opts_brkiter = c(
      base, skip_word_ideo = TRUE
    )),
    list(c("abc", "123"))
  )
})

test_that("boundary extract preserves NA, no-match, omit, and simplify shapes", {
  strings <- boundary_extract_charvec(c("", NA, "abc"))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  opts <- list(type = "word", skip_word_none = TRUE,
    skip_word_letter = TRUE)

  first <- extract_first_boundaries(strings, opts_brkiter = opts)
  expect_identical(first, rep(NA_character_, 3L))
  expect_identical(charport::is_charvec(first), charr_altrep())

  all <- extract_all_boundaries(strings, opts_brkiter = opts)
  expect_identical(all, rep(list(NA_character_), 3L))
  expect_identical(
    vapply(all, charport::is_charvec, logical(1L)),
    rep(charr_altrep(), 3L)
  )
  omitted <- extract_all_boundaries(
    strings, omit_no_match = TRUE, opts_brkiter = opts
  )
  expect_identical(lengths(omitted), c(0L, 1L, 0L))

  padded_empty <- extract_all_boundaries(
    boundary_extract_charvec(c("abc 123", "abc")), simplify = TRUE,
    opts_brkiter = list(type = "word", skip_word_none = TRUE)
  )
  padded_na <- extract_all_boundaries(
    boundary_extract_charvec(c("abc 123", "abc")), simplify = NA,
    opts_brkiter = list(type = "word", skip_word_none = TRUE)
  )
  expect_identical(padded_empty[2L, 2L], "")
  expect_identical(padded_na[2L, 2L], NA_character_)
})


test_that("boundary extract supports custom RBBI rules", {
  rules <- paste0(
    "$letters = [[:L:]]; $numbers = [[:N:]]; ",
    "$letters+; $numbers+; .;"
  )
  strings <- boundary_extract_charvec(c("abc 123", "é42"))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(
    extract_all_boundaries(strings, opts_brkiter = list(type = rules)),
    list(c("abc", " ", "123"), c("é", "42"))
  )
})


test_that("boundary extract is lenient for malformed declared UTF-8", {
  malformed <- rawToChar(as.raw(c(0x61, 0xc3, 0x28, 0x62)))
  Encoding(malformed) <- "UTF-8"
  malformed <- str_replace_all(malformed, fixed("\uffff"), "")
  expect_identical(charport::is_charvec(malformed), charr_altrep())
  opts <- list(type = "character")
  expect_identical(
    extract_all_boundaries(malformed, opts_brkiter = opts),
    stringi::stri_extract_all_boundaries(
      as.character(malformed), opts_brkiter = opts
    )
  )
})
