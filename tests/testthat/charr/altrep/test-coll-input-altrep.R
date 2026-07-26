# charr-owned targeted equivalence tests for Reader-backed collation search.

detect_coll <- function(...) charr:::ci_detect_coll(...)
count_coll <- function(...) charr:::ci_count_coll(...)
starts_coll <- function(...) charr:::ci_startswith_coll(...)
ends_coll <- function(...) charr:::ci_endswith_coll(...)

coll_charvec <- function(x) charr:::ci_trim_both(x)


test_that("coll input search reads CHARVEC UTF-16 records", {
  strings <- coll_charvec(c(
    " äpfel ", " Apfel ", " blåbær ", " blaa ", " 😀ä ", " ", NA
  ))
  patterns <- coll_charvec(c("a", "a", "blaa", "blå", "ä", "x", "a"))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(charport::is_charvec(patterns), charr_altrep())

  primary <- list(locale = "de", strength = 1L)
  tertiary <- list(locale = "de", strength = 3L)
  expect_identical(
    detect_coll(strings, patterns, opts_collator = primary),
    c(TRUE, TRUE, FALSE, TRUE, TRUE, FALSE, NA)
  )
  expect_identical(
    detect_coll(strings, coll_charvec("A"), opts_collator = tertiary),
    c(FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, NA)
  )
  expect_identical(
    count_coll(strings, patterns, opts_collator = primary),
    c(1L, 1L, 0L, 1L, 1L, 0L, NA_integer_)
  )
})


test_that("coll starts and ends use code-point from/to indexes", {
  strings <- coll_charvec(rep("aé😀bcä", 8L))
  patterns <- coll_charvec(c("a", "é", "😀", "b", "c", "a", "ä", "x"))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(charport::is_charvec(patterns), charr_altrep())

  opts <- list(locale = "de", strength = 1L)
  expect_identical(
    starts_coll(
      strings, patterns, from = c(1L, 2L, 3L, -3L, 5L, 9L, -1L, NA),
      opts_collator = opts
    ),
    c(TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, TRUE, NA)
  )
  expect_identical(
    ends_coll(
      strings, patterns, to = c(1L, 2L, 3L, -3L, 5L, 0L, -1L, NA),
      opts_collator = opts
    ),
    c(TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, TRUE, NA)
  )
})


test_that("coll search preserves Danish tailoring, recycling, and max_count", {
  strings <- coll_charvec(c(
    "Aarhus", "Århus", "blaa", "blå", "none", "", NA, "Aalborg"
  ))
  patterns <- coll_charvec(c("Å", "aa"))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(charport::is_charvec(patterns), charr_altrep())

  opts <- list(locale = "da", strength = 1L)
  expect_identical(
    detect_coll(strings, patterns, opts_collator = opts),
    c(TRUE, TRUE, TRUE, TRUE, FALSE, FALSE, NA, TRUE)
  )
  expect_identical(
    detect_coll(
      strings, patterns, max_count = 2L, opts_collator = opts
    ),
    c(TRUE, NA, TRUE, NA, NA, NA, NA, NA)
  )
  expect_identical(
    detect_coll(
      strings, patterns, negate = TRUE, max_count = 2L,
      opts_collator = opts
    ),
    c(FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, NA, NA)
  )
})


test_that("coll UTF-16 cursor recovers after missing subjects", {
  strings <- coll_charvec(c(NA, "Äpfel", "", "Apfel"))
  pattern <- coll_charvec("apfel")
  opts <- list(locale = "de", strength = 1L)

  expect_identical(
    detect_coll(strings, pattern, opts_collator = opts),
    stringi::stri_detect_coll(
      c(NA, "Äpfel", "", "Apfel"), "apfel", opts_collator = opts
    )
  )
})


test_that("coll empty patterns warn once and numeric search errors", {
  strings <- coll_charvec(c("abc", "", NA))
  empty <- coll_charvec("")
  numeric_pattern <- coll_charvec("x2")
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(charport::is_charvec(empty), charr_altrep())

  expect_warning(
    value <- detect_coll(strings, empty, max_count = 0L),
    "empty search patterns are not supported"
  )
  expect_identical(value, rep(NA, 3L))
  expect_warning(
    value <- count_coll(strings, empty),
    "empty search patterns are not supported"
  )
  expect_identical(value, rep(NA_integer_, 3L))
  expect_error(
    detect_coll(coll_charvec("x02"), numeric_pattern,
      opts_collator = list(numeric = TRUE)),
    "U_UNSUPPORTED_ERROR"
  )
})
