# charr-owned targeted equivalence tests for Reader-backed regex split.

split_regex <- function(...) charr_test_leaf("ci_split_regex")(...)


test_that("regex split delimits UTF-16 matches, fields re-encoded to UTF-8", {
  strings <- charr_test_leaf("ci_trim_both")(c(" a,😀,𐐷 ", " x1y22z ", " none ", NA))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  got <- split_regex(strings, c(",", "\\d+", ",", ","))
  expect_identical(got, list(
    c("a", "😀", "𐐷"),
    c("x", "y", "z"),
    "none",
    NA_character_
  ))
  expect_identical(charport::is_charvec(got[[1]]), charr_altrep())
  expect_identical(Encoding(got[[1]]), c("unknown", "UTF-8", "UTF-8"))
})


test_that("regex split honours n, omit_empty, tokens_only", {
  strings <- charr_test_leaf("ci_replace_all_fixed")(c("a,,b,c", ",a,"), "~", "~")
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    split_regex(strings, ",", n = 2L),
    list(c("a", ",b,c"), c("", "a,"))
  )
  expect_identical(
    split_regex(strings, ",", omit_empty = TRUE),
    list(c("a", "b", "c"), "a")
  )
  expect_identical(
    split_regex(strings, ",", n = 2L, tokens_only = TRUE),
    list(c("a", ""), c("", "a"))
  )
  # omit_empty = NA turns empty fields into NA
  expect_identical(
    split_regex(charr_test_leaf("ci_replace_all_fixed")("a,,b", "~", "~"), ",", omit_empty = NA),
    list(c("a", NA, "b"))
  )
})


test_that("regex split simplify builds a padded matrix; zero-length matches", {
  strings <- charr_test_leaf("ci_replace_all_fixed")(c("a,b,c", "x,y"), "~", "~")
  expect_identical(charport::is_charvec(strings), charr_altrep())

  m <- split_regex(strings, ",", simplify = TRUE)
  expect_identical(dim(m), c(2L, 3L))
  expect_identical(m[2, 3], "")

  # zero-length lookahead match splits before every character
  expect_identical(
    split_regex(charr_test_leaf("ci_replace_all_fixed")("abc", "~", "~"), "(?=.)"),
    list(c("", "a", "b", "c"))
  )
})


test_that("regex split preserves malformed declared UTF-8 like stringi", {
  malformed <- rawToChar(as.raw(c(0x61, 0x80, 0x2c, 0x62)))
  Encoding(malformed) <- "UTF-8"
  with_test_backend(TRUE, {
    malformed <- charport::as_charvec(malformed)
    expect_altrep_charvec(malformed)
    first <- rawToChar(as.raw(c(0x61, 0x80)))
    Encoding(first) <- "UTF-8"
    expect_identical(split_regex(malformed, ","), list(c(first, "b")))
  })
})
