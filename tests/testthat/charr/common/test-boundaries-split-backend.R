# Charr-owned cross-backend tests for boundary splitting.

split_boundaries <- function(...) charr_test_leaf("ci_split_boundaries")(...)

boundary_split_input <- function(x) charr::str_trim(x)


test_that("boundary split handles every ICU iterator type", {
  strings <- boundary_split_input(c(
    "\U0001f469\u200d\U0001f469\u200d\U0001f467\u200d\U0001f466e\u0301",
    "日本語の文章です", "ภาษาไทยภาษาไทย",
    "One. Two! Three?"
  ))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  character <- split_boundaries(
    strings[1L], opts_brkiter = list(type = "character")
  )
  expect_identical(character, list(c("👩‍👩‍👧‍👦", "e\u0301")))
  expect_identical(
    charport::is_charvec(character[[1L]]), charr_altrep()
  )
  word_opts <- list(type = "word", skip_word_none = TRUE)
  expect_identical(
    split_boundaries(strings[2L], opts_brkiter = word_opts),
    list(c("日本語", "の", "文章", "です"))
  )
  expect_identical(
    split_boundaries(strings[3L], opts_brkiter = word_opts),
    list(c("ภาษา", "ไทย", "ภาษา", "ไทย"))
  )
  expect_identical(
    split_boundaries(strings[4L], opts_brkiter = list(type = "sentence")),
    list(c("One. ", "Two! ", "Three?"))
  )
  expect_identical(
    split_boundaries(strings[2L], opts_brkiter = list(type = "line_break")),
    list(strsplit(strings[2L], "", fixed = TRUE)[[1L]])
  )
})
test_that("boundary split preserves n, tokens_only, and skip accounting", {
  string <- boundary_split_input("abc 123 日本語 カナ")
  expect_identical(charport::is_charvec(string), charr_altrep())
  words <- list(type = "word", skip_word_none = TRUE)

  expect_identical(
    split_boundaries(string, n = 1L, opts_brkiter = words),
    list("abc 123 日本語 カナ")
  )
  expect_identical(
    split_boundaries(string, n = 1L, tokens_only = TRUE,
      opts_brkiter = words),
    list("abc")
  )
  expect_identical(
    split_boundaries(string, n = 3L, opts_brkiter = words),
    list(c("abc", "123", "日本語 カナ"))
  )
  expect_identical(
    split_boundaries(string, opts_brkiter = c(
      words, skip_word_number = TRUE, skip_word_ideo = TRUE
    )),
    list("abc")
  )
})

test_that("boundary split preserves recycling, NA, empty, and simplify shapes", {
  strings <- boundary_split_input(c("abc def", "", NA, "123 456"))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  opts <- list(type = "word", skip_word_none = TRUE)

  expect_warning(
    recycled <- split_boundaries(strings, n = c(1L, 2L, NA_integer_),
      opts_brkiter = opts),
    "not a multiple"
  )
  expect_identical(
    recycled,
    list("abc def", character(), NA_character_, "123 456")
  )
  expect_identical(
    vapply(recycled, charport::is_charvec, logical(1L)),
    rep(charr_altrep(), 4L)
  )

  empty <- split_boundaries(strings, n = 0L, opts_brkiter = opts)
  expect_identical(lengths(empty), c(0L, 0L, 1L, 0L))
  padded_empty <- split_boundaries(
    strings, n = 2L, simplify = TRUE, opts_brkiter = opts
  )
  padded_na <- split_boundaries(
    strings, n = 2L, simplify = NA, opts_brkiter = opts
  )
  expect_identical(dim(padded_empty), c(4L, 2L))
  expect_identical(padded_empty[1L, ], c("abc", "def"))
  expect_identical(padded_empty[2L, ], c("", ""))
  expect_true(all(is.na(padded_na[2L, ])))
})


test_that("boundary split retains copied iterator-open and n-error order", {
  empty <- boundary_split_input("")
  text <- boundary_split_input(c("abc", "def"))
  bad_rules <- list(type = "[")
  expect_identical(
    split_boundaries(empty, n = 0L, opts_brkiter = bad_rules),
    list(character())
  )
  expect_error(
    split_boundaries(text[1L], n = .Machine$integer.max,
      opts_brkiter = bad_rules),
    "argument `n`"
  )
  expect_error(
    split_boundaries(text, n = c(1L, .Machine$integer.max),
      opts_brkiter = bad_rules),
    "U_MALFORMED_SET"
  )
})


test_that("boundary split supports custom rules and malformed UTF-8", {
  rules <- paste0(
    "$letters = [[:L:]]; $numbers = [[:N:]]; ",
    "$letters+; $numbers+; .;"
  )
  strings <- boundary_split_input(c("abc 123", "é42"))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(
    split_boundaries(strings, opts_brkiter = list(type = rules)),
    list(c("abc", " ", "123"), c("é", "42"))
  )

  malformed <- rawToChar(as.raw(c(0x61, 0xc3, 0x28, 0x62)))
  Encoding(malformed) <- "UTF-8"
  malformed <- str_replace_all(malformed, fixed("\uffff"), "")
  expect_identical(charport::is_charvec(malformed), charr_altrep())
  opts <- list(type = "character")
  expect_identical(
    split_boundaries(malformed, opts_brkiter = opts),
    stringi::stri_split_boundaries(
      as.character(malformed), opts_brkiter = opts
    )
  )
})
