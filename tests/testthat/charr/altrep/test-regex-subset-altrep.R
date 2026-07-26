# charr-owned equivalence tests for Reader-backed regex subset.
#
# str_subset() routes through str_detect() + R indexing, so ci_subset_regex
# is off the stringr-facing dispatch map. It is validated here directly against
# installed stringi as the oracle. `charr:::ci_subset_regex`
# always runs the Reader backend (it is not swapped by the OFF route), so these
# assertions hold on both routes; the charvec input still exercises the Reader.

subset_regex <- function(...) as.character(charr:::ci_subset_regex(...))


test_that("regex subset matches stringi on a charvec input", {
  base <- c(" 😀a ", " béé ", " xyz ", NA, " 😀z ", " ")
  strings <- charr:::ci_trim_both(base)
  expect_identical(charport::is_charvec(strings), charr_altrep())
  trimmed <- c("😀a", "béé", "xyz", NA, "😀z", "")

  for (pat in c("😀", "a", "[aeiou]", ".", "Z", "𐐷")) {
    for (om in c(FALSE, TRUE)) {
      for (neg in c(FALSE, TRUE)) {
        expect_identical(
          subset_regex(strings, pat, omit_na = om, negate = neg),
          stringi::stri_subset_regex(trimmed, pat, omit_na = om, negate = neg)
        )
      }
    }
  }
})
test_that("regex subset empty pattern warns like stringi", {
  strings <- charr:::ci_trim_both(c(" apple ", " pear ", NA))
  expect_identical(charport::is_charvec(strings), charr_altrep())
  expect_warning(
    e <- charr:::ci_subset_regex(strings, ""),
    "empty search patterns are not supported"
  )
  expect_identical(as.character(e), rep(NA_character_, 3))
})
