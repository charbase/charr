# charr-owned targeted equivalence tests for Reader/Builder coll split.

split_coll <- function(...) charr:::ci_split_coll(...)
split_coll_charvec <- function(x) charr:::ci_trim_both(x)


test_that("coll split slices UTF-16 fields around tailored delimiters", {
  strings <- split_coll_charvec(c(
    " 😀äAäB ", " üÜx ", " åaaÅ ", " none ", " ", NA
  ))
  patterns <- split_coll_charvec(c("a", "ü", "å", "a", "x", "a"))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(charport::is_charvec(patterns), charr_altrep())
  opts <- list(locale = "de", strength = 1L)

  expect_identical(
    split_coll(strings, patterns, opts_collator = opts),
    list(
      c("😀", "", "", "B"), c("", "", "x"),
      c("", "", "", "", ""), "none", "", NA_character_
    )
  )
  expect_identical(
    split_coll(strings, patterns, n = 2L, opts_collator = opts),
    list(
      c("😀", "AäB"), c("", "Üx"), c("", "aaÅ"),
      "none", "", NA_character_
    )
  )
})


test_that("coll split preserves tokens_only and omit-empty accounting", {
  strings <- split_coll_charvec(c("aAäB", "üÜx", "åaaÅ", "", NA))
  patterns <- split_coll_charvec(c("a", "ü", "å", "x", "a"))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(charport::is_charvec(patterns), charr_altrep())
  opts <- list(locale = "de", strength = 1L)

  expect_identical(
    split_coll(
      strings, patterns, n = 2L, tokens_only = TRUE,
      opts_collator = opts
    ),
    list(c("", ""), c("", ""), c("", ""), "", NA_character_)
  )
  expect_warning(
    omitted <- split_coll(
      strings, patterns, n = c(3L, 1L), omit_empty = TRUE,
      opts_collator = opts
    ),
    "not a multiple"
  )
  expect_identical(
    omitted,
    list("B", "üÜx", character(), character(), NA_character_)
  )
  expect_identical(
    split_coll(
      strings, patterns, n = 3L, omit_empty = NA,
      opts_collator = opts
    ),
    list(
      c(NA, NA, "äB"), c(NA, NA, "x"), c(NA, NA, "aÅ"),
      NA_character_, NA_character_
    )
  )
})


test_that("coll split preserves simplify padding and validation order", {
  strings <- split_coll_charvec(c("😀äAäB", "üÜx", "none", "", NA))
  patterns <- split_coll_charvec(c("a", "ü", "a", "x", "a"))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(charport::is_charvec(patterns), charr_altrep())
  opts <- list(locale = "de", strength = 1L)

  empty_pad <- split_coll(
    strings, patterns, n = 2L, simplify = TRUE, opts_collator = opts
  )
  na_pad <- split_coll(
    strings, patterns, n = 2L, simplify = NA, opts_collator = opts
  )
  expect_identical(dim(empty_pad), c(5L, 2L))
  expect_identical(empty_pad[3L, 2L], "")
  expect_identical(na_pad[3L, 2L], NA_character_)

  empty_pattern <- split_coll_charvec("")
  expect_warning(
    empty <- split_coll(strings, empty_pattern),
    "empty search patterns are not supported"
  )
  expect_identical(empty, rep(list(NA_character_), length(strings)))
  one_string <- split_coll_charvec("😀äAäB")
  one_pattern <- split_coll_charvec("a")
  expect_error(
    split_coll(one_string, one_pattern, n = .Machine$integer.max),
    "argument `n`"
  )
})
