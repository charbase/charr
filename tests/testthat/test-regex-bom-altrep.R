# charr-owned regression for per-op UTF-8 BOM handling. stringi strips a leading
# BOM in ops built on StriContainerUTF8 (extract/split/match/subset-replacement)
# but keeps it in StriContainerUTF16 ops (detect/count/locate/replace/subset).
# The Reader UTF-16 context must reproduce that per-op split (killbom flag).

bom_str <- function() {
  # Chain through a BOM-PRESERVING charr op (regex replace is UTF16-origin, no
  # killbom) so the value is a charvec under ON but still carries the leading
  # BOM. NB: fixed ops (ci_replace_all_fixed etc.) would strip it here.
  x <- rawToChar(as.raw(c(0xEF, 0xBB, 0xBF, 0x61, 0x62, 0x63)))  # BOM + "abc"
  Encoding(x) <- "UTF-8"
  charr:::ci_replace_all_regex(x, "NOMATCHZZZ", "", vectorize_all = TRUE,
                               opts_regex = NULL)
}


test_that("BOM is stripped by UTF8-origin regex ops (extract/split/match)", {
  x <- bom_str()
  expect_identical(charport::is_charvec(x), charr_altrep())

  # extract "." gets "a" (BOM gone), not the BOM code point
  expect_identical(as.character(charr:::ci_extract_first_regex(x, ".", NULL)), "a")
  # split on "b" -> "a","c" (leading field is "a", BOM stripped)
  expect_identical(charr:::ci_split_regex(x, "b", -1L, FALSE, FALSE, FALSE, NULL),
                   list(c("a", "c")))
  # match "(.)" whole + group are "a"
  expect_identical(
    as.vector(charr:::ci_match_first_regex(x, "(.)", cg_missing = NA, opts_regex = NULL)),
    c("a", "a")
  )
})


test_that("BOM is preserved by UTF16-origin regex ops (detect/replace)", {
  x <- bom_str()
  expect_identical(charport::is_charvec(x), charr_altrep())

  # ^a must NOT match: the first code point is still the BOM
  expect_false(charr:::ci_detect_regex(x, "^a", FALSE, -1L, NULL))
  # replacing the first char replaces the BOM, leaving "abc"
  r <- as.character(charr:::ci_replace_first_regex(x, ".", "Z", NULL))
  expect_identical(charToRaw(r), as.raw(c(0x5A, 0x61, 0x62, 0x63)))  # "Zabc"
})


test_that("a mid-string BOM is never stripped", {
  mid <- rawToChar(as.raw(c(0x61, 0xEF, 0xBB, 0xBF, 0x62)))  # a BOM b
  Encoding(mid) <- "UTF-8"
  midv <- charr:::ci_replace_all_regex(mid, "NOMATCHZZZ", "", vectorize_all = TRUE,
                                       opts_regex = NULL)
  ex <- as.character(charr:::ci_extract_first_regex(midv, ".+", NULL))
  expect_identical(charToRaw(ex), as.raw(c(0x61, 0xEF, 0xBB, 0xBF, 0x62)))
})
