# charr-owned file: targeted equivalence tests for Reader-backed fixed extract.

extract_first_fixed <- function(...) charr_test_leaf("ci_extract_first_fixed")(...)
extract_all_fixed <- function(...) charr_test_leaf("ci_extract_all_fixed")(...)


test_that("fixed extract emits multibyte Reader slices with exact encodings", {
  strings <- charr_test_leaf("ci_trim_both")(c(" éaé ", " 🙂x🙂 ", " üü ", " abc "))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  first <- extract_first_fixed(strings, c("é", "🙂", "ü", "b"))
  expect_identical(first, c("é", "🙂", "ü", "b"))
  expect_identical(charport::is_charvec(first), charr_altrep())
  expect_identical(Encoding(first), c("UTF-8", "UTF-8", "UTF-8", "unknown"))

  expect_identical(
    extract_all_fixed(
      charr_test_leaf("ci_trim_both")(c(" ééé ", " 🙂x🙂 ", " üü ")),
      c("éé", "🙂", "ü"), opts_fixed = list(overlap = TRUE)
    ),
    list(c("éé", "éé"), c("🙂", "🙂"), c("ü", "ü"))
  )
})
test_that("fixed extract stays inside each Reader record", {
  strings <- charr_test_leaf("ci_trim_both")(c("aaa", "z", "aaaa", "xyz", "abcx"))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    extract_first_fixed(strings, c("z", "z", "xy", "xy", "x")),
    c(NA, "z", NA, "xy", "x")
  )
  expect_identical(
    extract_all_fixed(strings, c("z", "z", "xy", "xy", "x")),
    list(NA_character_, "z", NA_character_, "xy", "x")
  )
})

test_that("fixed extract preserves no-match, NA, omit, and simplify shapes", {
  strings <- charr_test_leaf("ci_trim_both")(c("aa", "none", "", NA_character_))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    extract_all_fixed(strings, "a", omit_no_match = FALSE),
    list(c("a", "a"), NA_character_, NA_character_, NA_character_)
  )
  expect_identical(
    extract_all_fixed(strings, "a", omit_no_match = TRUE),
    list(c("a", "a"), character(), character(), NA_character_)
  )
  expect_identical(
    extract_all_fixed(strings, "a", omit_no_match = TRUE, simplify = TRUE),
    structure(c("a", "", "", NA, "a", "", "", ""), dim = c(4L, 2L))
  )
  expect_identical(
    extract_all_fixed(strings, "a", omit_no_match = TRUE, simplify = NA),
    structure(c("a", NA, NA, NA, "a", NA, NA, NA), dim = c(4L, 2L))
  )
})


test_that("fixed extract preserves recycling and empty-pattern semantics", {
  strings <- charr_test_leaf("ci_trim_both")(c("a", "ba", "ca"))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_warning(
    recycled <- extract_first_fixed(strings, c("a", "b")),
    "longer object length is not a multiple"
  )
  expect_identical(recycled, c("a", "b", "a"))

  expect_warning(
    empty_first <- extract_first_fixed(strings, ""),
    "empty search patterns are not supported"
  )
  expect_identical(empty_first, rep(NA_character_, 3L))

  expect_warning(
    empty_all <- extract_all_fixed(strings, "", omit_no_match = TRUE),
    "empty search patterns are not supported"
  )
  expect_identical(empty_all, rep(list(NA_character_), 3L))
})


test_that("fixed extract passes malformed UTF-8 slices through verbatim", {
  malformed <- rawToChar(as.raw(c(0x61, 0xff, 0x62)))
  Encoding(malformed) <- "UTF-8"
  malformed <- charr_test_leaf("ci_replace_all_fixed")(malformed, "z", "z")
  expect_identical(charport::is_charvec(malformed), charr_altrep())

  bad_pattern <- rawToChar(as.raw(0xff))
  Encoding(bad_pattern) <- "UTF-8"
  extracted <- extract_first_fixed(malformed, bad_pattern)
  expect_identical(charToRaw(extracted), as.raw(0xff))
})
