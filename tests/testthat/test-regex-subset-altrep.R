# charr-owned equivalence tests for Reader-backed regex subset.
#
# str_subset() routes through str_detect() + R indexing, so ci_subset_regex and
# ci_subset_regex<- are off the stringr-facing dispatch map. They are validated
# here directly against installed stringi as the oracle. `charr:::ci_subset_regex`
# always runs the Reader backend (it is not swapped by the OFF route), so these
# assertions hold on both routes; the charvec input still exercises the Reader.

subset_regex <- function(...) as.character(charr:::ci_subset_regex(...))
`subset_regex<-` <- function(...) charr:::`ci_subset_regex<-`(...)


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

test_that("regex subset replacement matches stringi (value recycling, NA)", {
  base <- c("apple", "banana", "cherry", NA, "kiwi", "melon")
  strings <- charr:::ci_replace_all_fixed(base, "~", "~")
  expect_identical(charport::is_charvec(strings), charr_altrep())

  cases <- list(
    list(pat = "a", value = "X", negate = FALSE),
    list(pat = "a", value = "X", negate = TRUE),
    list(pat = "a", value = NA_character_, negate = FALSE),
    list(pat = "[aeiou]", value = c("A", "B"), negate = FALSE)
  )
  # value shorter than the match count warns identically on both backends
  # (verified separately); suppress here and compare the substituted values.
  for (case in cases) {
    got <- strings
    suppressWarnings(
      subset_regex(got, case$pat, negate = case$negate) <- case$value
    )
    want <- base
    suppressWarnings(
      stringi::stri_subset_regex(want, case$pat, negate = case$negate) <- case$value
    )
    expect_identical(as.character(got), want)
  }
})


test_that("regex subset replacement keeps output-only bytes and killbom", {
  malformed <- rawToChar(as.raw(0x80))
  Encoding(malformed) <- "UTF-8"

  with_altrep(TRUE, {
    malformed <- charport::as_charvec(malformed)
    expect_true(charport::is_charvec(malformed))

    kept <- malformed
    subset_regex(kept, "z") <- "X"
    expect_identical(charToRaw(as.character(kept)), as.raw(0x80))

    matched <- malformed
    subset_regex(matched, "^\\x{FFFD}$") <- "X"
    expect_identical(as.character(matched), "X")

    replaced <- charport::as_charvec("a")
    subset_regex(replaced, "a") <- malformed
    expect_identical(charToRaw(as.character(replaced)), as.raw(0x80))

    doubled_bom <- charport::as_charvec("\ufeff\ufeffx")
    kept_bom <- doubled_bom
    subset_regex(kept_bom, "z") <- "X"
    expect_identical(as.character(kept_bom), "\ufeffx")

    value_bom <- charport::as_charvec("a")
    subset_regex(value_bom, "a") <- doubled_bom
    expect_identical(as.character(value_bom), "\ufeffx")
  })
})
