# charr-owned targeted equivalence tests for Reader-backed regex match.
# str is read through the Reader (charvec input); the character-matrix output is
# a plain STRSXP matrix, so is_charvec is asserted on the input only.

match_first <- function(...) charr:::ci_match_first_regex(...)
match_all <- function(...) charr:::ci_match_all_regex(...)


test_that("regex match extracts capture groups over UTF-16 subjects", {
  strings <- charr:::ci_trim_both(c(" abc123 ", " é4😀5 ", " zzz ", NA))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  m <- match_first(strings, "([[:alpha:]]+)([0-9]+)")
  expect_identical(dim(m), c(4L, 3L))
  expect_identical(m[1, ], c("abc123", "abc", "123"))
  # é4😀5: [:alpha:]+ = "é", digits "4" (then 😀 breaks); whole = "é4"
  expect_identical(m[2, ], c("é4", "é", "4"))
  expect_identical(m[3, ], rep(NA_character_, 3))  # no match
  expect_identical(m[4, ], rep(NA_character_, 3))  # NA str
})


test_that("regex match names columns for named capture groups; cg_missing", {
  strings <- charr:::ci_replace_all_fixed(c("2024-01", "nope"), "~", "~")
  expect_identical(charport::is_charvec(strings), charr_altrep())

  m <- match_first(strings, "(?<yr>[0-9]+)-(?<mo>[0-9]+)", cg_missing = "?")
  expect_identical(dimnames(m)[[2]], c("", "yr", "mo"))
  expect_identical(unname(m[1, ]), c("2024-01", "2024", "01"))
  # no match fills the whole row (incl. the match column) with cg_missing
  expect_identical(unname(m[2, ]), rep("?", 3))

  # optional group absent -> cg_missing fill
  m2 <- match_first(charr:::ci_replace_all_fixed("ac", "~", "~"), "(a)(b)?(c)",
                    cg_missing = "MISS")
  expect_identical(as.vector(m2), c("ac", "a", "MISS", "c"))
})


test_that("regex match first and match_all preserve shapes", {
  strings <- charr:::ci_replace_all_fixed(c("a1b2c3", "xx"), "~", "~")
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(match_first(strings, "([a-z])([0-9])")[1, ], c("a1", "a", "1"))

  all <- match_all(strings, "([a-z])([0-9])")
  expect_identical(dim(all[[1]]), c(3L, 3L))
  expect_identical(all[[1]][, 1], c("a1", "b2", "c3"))
  # no match: 1 all-NA row, or 0 rows under omit_no_match
  expect_identical(dim(all[[2]]), c(1L, 3L))
  expect_identical(
    dim(match_all(strings, "([a-z])([0-9])", omit_no_match = TRUE)[[2]]),
    c(0L, 3L)
  )
})


test_that("regex match preserves malformed declared UTF-8 like stringi", {
  malformed <- rawToChar(as.raw(0x80))
  Encoding(malformed) <- "UTF-8"
  with_altrep(TRUE, {
    malformed <- charport::as_charvec(malformed)
    expect_true(charport::is_charvec(malformed))

    # R and stringi retain the declared UTF-8 byte in captured source slices.
    replacement <- rawToChar(as.raw(0x80))
    Encoding(replacement) <- "UTF-8"
    expect_identical(
      unname(match_all(malformed, "(.)")[[1]]),
      structure(c(replacement, replacement), dim = c(1L, 2L))
    )
  })
})
