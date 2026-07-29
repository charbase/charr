# charr-owned targeted equivalence tests for Reader-backed coll locate.

locate_first_coll <- function(...) charr:::ci_locate_first_coll(...)
locate_all_coll <- function(...) charr:::ci_locate_all_coll(...)

locate_coll_charvec <- function(x) charr:::ci_trim_both(x)


test_that("coll locate converts UTF-16 offsets to code-point positions", {
  strings <- locate_coll_charvec(c(
    " 😀ä-a-A ", " üxÜ ", " å-aa-Å ", " none ", " ", NA
  ))
  patterns <- locate_coll_charvec(c("a", "ü", "aa", "a", "x", NA))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(charport::is_charvec(patterns), charr_altrep())
  opts <- list(locale = "de", strength = 1L)

  expect_identical(
    locate_first_coll(strings, patterns, opts_collator = opts),
    structure(
      c(2L, 1L, 3L, NA, NA, NA, 2L, 2L, 4L, NA, NA, NA),
      dim = c(6L, 2L), dimnames = list(NULL, c("start", "end"))
    )
  )
})


test_that("coll locate all preserves match and no-match matrix shapes", {
  strings <- locate_coll_charvec(c("😀ä-a-A", "üxÜ", "none", "", NA))
  patterns <- locate_coll_charvec(c("a", "ü", "a", "x", "a"))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(charport::is_charvec(patterns), charr_altrep())
  opts <- list(locale = "de", strength = 1L)

  all <- locate_all_coll(strings, patterns, opts_collator = opts)
  expect_identical(all[[1L]][, 1L], c(2L, 4L, 6L))
  expect_identical(all[[1L]][, 2L], c(2L, 4L, 6L))
  expect_identical(all[[2L]][, 1L], c(1L, 4L))
  expect_identical(all[[2L]][, 2L], c(2L, 4L))
  expect_identical(all[[3L]], structure(
    c(NA_integer_, NA_integer_), dim = c(1L, 2L),
    dimnames = list(NULL, c("start", "end"))
  ))
  expect_identical(all[[4L]], all[[3L]])
  expect_identical(all[[5L]], all[[3L]])

  omitted <- locate_all_coll(
    strings, patterns, omit_no_match = TRUE, get_length = TRUE,
    opts_collator = opts
  )
  expect_identical(dim(omitted[[3L]]), c(0L, 2L))
  expect_identical(dim(omitted[[4L]]), c(0L, 2L))
  expect_identical(omitted[[5L]], structure(
    c(NA_integer_, NA_integer_), dim = c(1L, 2L),
    dimnames = list(NULL, c("start", "length"))
  ))
})


test_that("coll locate preserves recycling and empty-pattern warnings", {
  strings <- locate_coll_charvec(c("Aarhus", "Århus", "blaa", "blå", NA, ""))
  patterns <- locate_coll_charvec(c("Å", "aa"))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(charport::is_charvec(patterns), charr_altrep())

  located <- locate_first_coll(
    strings, patterns, opts_collator = list(locale = "da", strength = 1L)
  )
  expect_identical(located[, 1L], c(1L, 1L, 3L, 3L, NA, NA))

  empty <- locate_coll_charvec("")
  expect_warning(
    empty_result <- locate_all_coll(strings, empty),
    "empty search patterns are not supported"
  )
  expect_true(all(vapply(empty_result, nrow, integer(1L)) == 1L))
  expect_true(all(vapply(empty_result, function(x) all(is.na(x)), logical(1L))))
})
