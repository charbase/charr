# charr-owned targeted equivalence tests for Reader-backed regex replace.

replace_all <- function(...) charr:::ci_replace_all_regex(...)
replace_first <- function(...) charr:::ci_replace_first_regex(...)
replace_last <- function(...) charr:::ci_replace_last_regex(...)


test_that("regex replace applies $-substitution over UTF-16 subjects", {
  strings <- charr:::ci_trim_both(c(" abcde ", " a1b2 ", " é😀x ", NA))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  got <- replace_all(strings, c("(b)(c)(d)", "[0-9]", "(.)", "z"),
                     c("[$2]", "#", "<$1>", "Q"))
  expect_identical(got, c("a[c]e", "a#b#", "<é><😀><x>", NA))
  expect_identical(charport::is_charvec(got), charr_altrep())
  expect_identical(Encoding(got[1:3]), c("unknown", "unknown", "UTF-8"))
})
test_that("regex replace first/last differ and NA replacement blanks matches", {
  strings <- charr:::ci_replace_all_fixed(c("ababab", "xyz", "aa"), "~", "~")
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(replace_first(strings, "a", "Z"), c("Zbabab", "xyz", "Za"))
  expect_identical(replace_last(strings, "a", "Z"), c("ababZb", "xyz", "aZ"))
  # NA replacement -> whole element becomes NA iff it matched
  expect_identical(
    replace_all(strings, "a", NA_character_),
    c(NA, "xyz", NA)
  )
})

test_that("regex replace raises ICU error on an invalid backreference", {
  strings <- charr:::ci_replace_all_fixed("abcde", "~", "~")
  expect_identical(charport::is_charvec(strings), charr_altrep())
  # (b)(c)(d) has 3 groups; $9 / \\9 is out of bounds
  expect_error(replace_all(strings, "(b)(c)(d)", "$9"))
})


test_that("regex replace vectorize_all = FALSE applies patterns in sequence", {
  strings <- charr:::ci_replace_all_fixed(c("a1b2", "cccc", NA), "~", "~")
  expect_identical(charport::is_charvec(strings), charr_altrep())

  got <- replace_all(strings, c("[0-9]", "c"), c("_", "C"), vectorize_all = FALSE)
  expect_identical(got, c("a_b_", "CCCC", NA))
})
