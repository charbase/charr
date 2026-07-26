# charr-owned file: targeted equivalence tests for the Reader-backed fixed
# locate unit. The OFF route is installed stringi; the ON route is charr C++.

locate_first_fixed <- function(...) charr:::ci_locate_first_fixed(...)
locate_all_fixed <- function(...) charr:::ci_locate_all_fixed(...)


test_that("fixed locate converts Reader byte offsets to code-point positions", {
  strings <- charr:::ci_trim_both(c(" éaé ", " 🙂x🙂 ", " üü "))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    locate_first_fixed(strings, c("a", "x", "̈")),
    cbind(start = c(2L, 2L, 2L), end = c(2L, 2L, 2L))
  )
  expect_identical(
    locate_first_fixed(strings, c("éa", "🙂x", "ü"), get_length = TRUE),
    cbind(start = c(1L, 1L, 1L), length = c(2L, 2L, 2L))
  )

  expect_identical(
    locate_all_fixed(
      charr:::ci_trim_both(c(" ééé ", " 🙂x🙂 ", " üü ")),
      c("éé", "🙂", "ü"), opts_fixed = list(overlap = TRUE)
    ),
    list(
      cbind(start = c(1L, 2L), end = c(2L, 3L)),
      cbind(start = c(1L, 3L), end = c(1L, 3L)),
      cbind(start = c(1L, 3L), end = c(2L, 4L))
    )
  )
})
test_that("fixed locate keeps Reader scans inside each record", {
  strings <- charr:::ci_trim_both(c("aaa", "z", "aaaa", "xyz", "abcx"))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    locate_first_fixed(strings, c("z", "z", "xy", "xy", "x")),
    cbind(
      start = c(NA_integer_, 1L, NA_integer_, 1L, 4L),
      end = c(NA_integer_, 1L, NA_integer_, 2L, 4L)
    )
  )
})

test_that("fixed locate preserves no-match, NA, and omit_no_match shapes", {
  strings <- charr:::ci_trim_both(c("abc", "", "éé", NA_character_))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  patterns <- c("x", "x", "é", "x")

  expect_identical(
    locate_all_fixed(strings, patterns, get_length = TRUE),
    list(
      cbind(start = -1L, length = -1L),
      cbind(start = -1L, length = -1L),
      cbind(start = c(1L, 2L), length = c(1L, 1L)),
      cbind(start = NA_integer_, length = NA_integer_)
    )
  )

  omitted <- locate_all_fixed(
    strings, patterns, omit_no_match = TRUE, get_length = TRUE
  )
  expect_identical(omitted[[1]], cbind(start = integer(), length = integer()))
  expect_identical(omitted[[2]], cbind(start = integer(), length = integer()))
  expect_identical(
    omitted[[3]],
    cbind(start = c(1L, 2L), length = c(1L, 1L))
  )
  # omit_no_match applies to genuine no-matches, not an NA argument.
  expect_identical(
    omitted[[4]],
    cbind(start = NA_integer_, length = NA_integer_)
  )
})


test_that("fixed empty patterns warn and never become zero-length matches", {
  strings <- charr:::ci_trim_both(c("abc", "", NA_character_))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_warning(
    first <- locate_first_fixed(strings, ""),
    "empty search patterns are not supported"
  )
  expect_identical(
    first,
    cbind(
      start = rep(NA_integer_, 3L),
      end = rep(NA_integer_, 3L)
    )
  )

  expect_warning(
    all <- locate_all_fixed(strings, "", omit_no_match = TRUE),
    "empty search patterns are not supported"
  )
  argument_na <- cbind(start = NA_integer_, end = NA_integer_)
  expect_identical(all, rep(list(argument_na), 3L))
})


test_that("fixed locate preserves recycling and lenient malformed UTF-8 indexing", {
  strings <- charr:::ci_trim_both(c("a", "ba", "ca"))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_warning(
    recycled <- locate_first_fixed(strings, c("a", "b")),
    "longer object length is not a multiple"
  )
  expect_identical(
    recycled,
    cbind(start = c(1L, 1L, 2L), end = c(1L, 1L, 2L))
  )

  malformed <- rawToChar(as.raw(c(0x61, 0xc3, 0x28, 0x62)))
  Encoding(malformed) <- "UTF-8"
  malformed <- charr:::ci_replace_all_fixed(malformed, "z", "z")
  expect_identical(charport::is_charvec(malformed), charr_altrep())
  expect_identical(
    locate_first_fixed(malformed, "b"),
    cbind(start = 4L, end = 4L)
  )

  lone_continuation <- rawToChar(as.raw(c(0x61, 0x80, 0x62)))
  Encoding(lone_continuation) <- "UTF-8"
  lone_continuation <- charr:::ci_replace_all_fixed(
    lone_continuation, "z", "z"
  )
  expect_identical(
    locate_first_fixed(lone_continuation, "b"),
    cbind(start = 3L, end = 3L)
  )
  expect_identical(
    locate_all_fixed(lone_continuation, "b", get_length = TRUE),
    list(cbind(start = 3L, length = 1L))
  )
})
