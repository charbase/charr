# Charr-owned regression for per-operation UTF-8 BOM handling. Stringi strips
# a leading BOM in extract, split, match, and subset replacement, but keeps it
# in detect, count, locate, replace, and subset. The common UTF-8 adapter does
# not decide this; each optimized operation must preserve its public behavior.

bom_str <- function() {
  # Chain through a BOM-PRESERVING charr op (regex replace is UTF16-origin, no
  # killbom) so the value is a charvec under ON but still carries the leading
  # BOM. NB: fixed ops (ci_replace_all_fixed etc.) would strip it here.
  x <- rawToChar(as.raw(c(0xEF, 0xBB, 0xBF, 0x61, 0x62, 0x63)))  # BOM + "abc"
  Encoding(x) <- "UTF-8"
  charr_test_leaf("ci_replace_all_regex")(x, "NOMATCHZZZ", "", vectorize_all = TRUE,
                               opts_regex = NULL)
}


test_that("BOM is stripped by UTF8-origin regex ops (extract/split/match)", {
  x <- bom_str()
  expect_identical(charport::is_charvec(x), charr_altrep())

  # extract "." gets "a" (BOM gone), not the BOM code point
  expect_identical(as.character(charr_test_leaf("ci_extract_first_regex")(x, ".", NULL)), "a")
  # split on "b" -> "a","c" (leading field is "a", BOM stripped)
  expect_identical(charr_test_leaf("ci_split_regex")(x, "b", -1L, FALSE, FALSE, FALSE, NULL),
                   list(c("a", "c")))
  # match "(.)" whole + group are "a"
  expect_identical(
    as.vector(charr_test_leaf("ci_match_first_regex")(x, "(.)", cg_missing = NA, opts_regex = NULL)),
    c("a", "a")
  )
})


test_that("BOM is preserved by non-stripping regex ops (detect/replace)", {
  x <- bom_str()
  expect_identical(charport::is_charvec(x), charr_altrep())

  # ^a must NOT match: the first code point is still the BOM
  expect_false(charr_test_leaf("ci_detect_regex")(x, "^a", FALSE, -1L, NULL))
  # replacing the first char replaces the BOM, leaving "abc"
  r <- as.character(charr_test_leaf("ci_replace_first_regex")(x, ".", "Z", NULL))
  expect_identical(charToRaw(r), as.raw(c(0x5A, 0x61, 0x62, 0x63)))  # "Zabc"
})


test_that("a mid-string BOM is never stripped", {
  mid <- rawToChar(as.raw(c(0x61, 0xEF, 0xBB, 0xBF, 0x62)))  # a BOM b
  Encoding(mid) <- "UTF-8"
  midv <- charr_test_leaf("ci_replace_all_regex")(mid, "NOMATCHZZZ", "", vectorize_all = TRUE,
                                       opts_regex = NULL)
  ex <- as.character(charr_test_leaf("ci_extract_first_regex")(midv, ".+", NULL))
  expect_identical(charToRaw(ex), as.raw(c(0x61, 0xEF, 0xBB, 0xBF, 0x62)))
})
