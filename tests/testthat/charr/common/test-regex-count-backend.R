# charr-owned targeted equivalence tests for Reader-backed regex count.

count_regex <- function(...) charr_test_leaf("ci_count_regex")(...)


test_that("regex count handles UTF-16, astral text, and inline modes", {
  strings <- charr_test_leaf("ci_trim_both")(c(
    " café ", " 😀a😀 ", " 𐐷𐐷 ", " üü ", "", NA_character_
  ))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    count_regex(strings, c("a", "😀", "𐐷", "\\p{M}", "^", "(?w).")),
    c(1L, 2L, 2L, 2L, 1L, NA_integer_)
  )

  words <- charr_test_leaf("ci_trim_both")(c(" WORD word ", "words", "word"))
  expect_identical(charport::is_charvec(words), charr_altrep())
  expect_identical(count_regex(words, "(?wi)\\bword\\b"), c(2L, 0L, 1L))
})
test_that("regex count preserves ICU zero-length find advancement", {
  strings <- charr_test_leaf("ci_trim_both")(c("", "a", "😀a", "😀"))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(count_regex(strings, ".*?"), c(1L, 2L, 3L, 2L))
  expect_identical(count_regex(strings, "^"), rep(1L, 4L))
  expect_identical(count_regex(strings, "(?=a)"), c(0L, 1L, 1L, 0L))
})

test_that("regex count preserves NA, recycling, and lazy compile errors", {
  strings <- charr_test_leaf("ci_trim_both")(c(" aa ", " b ", NA, " aaa "))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_identical(count_regex(strings, c("a", "b")), c(2L, 1L, NA, 0L))

  present <- charr_test_leaf("ci_trim_both")(" x ")
  missing <- charr_test_leaf("ci_trim_both")(NA_character_)
  expect_identical(charport::is_charvec(present), charr_altrep())
  expect_identical(charport::is_charvec(missing), charr_altrep())
  expect_error(
    count_regex(present, "["),
    "Missing closing bracket.*U_REGEX_MISSING_CLOSE_BRACKET.*context=`\\[`")
  expect_identical(count_regex(missing, "["), NA_integer_)
})
