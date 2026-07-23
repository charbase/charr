# charr-owned file: targeted equivalence tests for Reader-backed fixed split.

split_fixed <- function(...) charr:::ci_split_fixed(...)


test_that("fixed split emits exact multibyte Reader slices", {
  strings <- charr:::ci_trim_both(c(" café🙂café ", " ü|🙂|é ", " x|á|x "))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  result <- split_fixed(strings, c("🙂", "|", "|"))
  expect_identical(
    result,
    list(c("café", "café"), c("ü", "🙂", "é"), c("x", "á", "x"))
  )
  expect_identical(
    lapply(result, Encoding),
    list(c("UTF-8", "UTF-8"), rep("UTF-8", 3L), c("unknown", "UTF-8", "unknown"))
  )
})

test_that("fixed split stays inside adjacent Reader records", {
  strings <- charr:::ci_trim_both(c("a", "long_record_without_delimiter", "x_y", "z", "p__q"))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    split_fixed(strings, "_"),
    list(
      "a", c("long", "record", "without", "delimiter"),
      c("x", "y"), "z", c("p", "", "q")
    )
  )
})


test_that("fixed split preserves n, omit_empty, and tokens_only interactions", {
  strings <- charr:::ci_trim_both(c(" a_b_c__d ", " _a__b_ "))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    split_fixed(strings[1], "_", n = 4L),
    list(c("a", "b", "c", "_d"))
  )
  expect_identical(
    split_fixed(strings[1], "_", n = 4L, tokens_only = TRUE),
    list(c("a", "b", "c", ""))
  )
  expect_identical(
    split_fixed(strings[1], "_", n = 4L, omit_empty = TRUE, tokens_only = TRUE),
    list(c("a", "b", "c", "d"))
  )
  expect_identical(
    split_fixed(strings[2], "_", n = 2L, omit_empty = TRUE),
    list(c("a", "_b_"))
  )
  expect_identical(
    split_fixed(strings[2], "_", n = 2L, omit_empty = TRUE, tokens_only = TRUE),
    list(c("a", "b"))
  )
})


test_that("fixed split preserves empty, missing, and no-token shapes", {
  strings <- charr:::ci_trim_both(c("", "abc", NA_character_))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(split_fixed(strings[1], "_", n = 0L), list(character()))
  expect_identical(
    split_fixed(strings[1], "_", n = 0L, omit_empty = NA),
    list(NA_character_)
  )
  expect_identical(
    split_fixed(strings[1], "_", omit_empty = TRUE),
    list(character())
  )
  expect_identical(
    split_fixed(strings, "_", n = c(-1L, 0L, -1L)),
    list("", character(), NA_character_)
  )
  expect_identical(
    split_fixed(strings, "_", n = NA_integer_),
    rep(list(NA_character_), 3L)
  )
})


test_that("fixed split preserves empty-pattern warnings and n validation order", {
  strings <- charr:::ci_trim_both(c("abc", ""))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_warning(
    empty_pattern <- split_fixed(strings, ""),
    "empty search patterns are not supported"
  )
  expect_identical(empty_pattern, rep(list(NA_character_), 2L))

  expect_error(
    split_fixed(strings[1], "_", n = .Machine$integer.max),
    "incorrect argument `n`; value too large",
    fixed = TRUE
  )
  # The copied backend handles empty input before the upper-bound check.
  expect_identical(
    split_fixed(strings[2], "_", n = .Machine$integer.max),
    list("")
  )
})


test_that("fixed split preserves omit NA, simplify, and recycling", {
  strings <- charr:::ci_trim_both(c("a__b", "x_y", ""))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    split_fixed(strings[1], "_", omit_empty = NA),
    list(c("a", NA, "b"))
  )
  expect_identical(
    split_fixed(strings, "_", n = 3L, simplify = TRUE),
    structure(
      c("a", "x", "", "", "y", "", "b", "", ""),
      dim = c(3L, 3L)
    )
  )
  expect_identical(
    split_fixed(strings, "_", n = 3L, simplify = NA),
    structure(
      c("a", "x", "", "", "y", NA, "b", NA, NA),
      dim = c(3L, 3L)
    )
  )

  expect_warning(
    recycled <- split_fixed(strings, c("_", "|")),
    "longer object length is not a multiple"
  )
  expect_identical(recycled, list(c("a", "", "b"), "x_y", ""))
})


test_that("fixed split passes malformed UTF-8 fields through verbatim", {
  malformed <- rawToChar(as.raw(c(0x61, 0xff, 0x7c, 0x62)))
  Encoding(malformed) <- "UTF-8"
  strings <- charr:::ci_replace_all_fixed(malformed, "z", "z")
  expect_identical(charport::is_charvec(strings), charr_altrep())

  fields <- split_fixed(strings, "|")[[1]]
  expect_identical(charToRaw(fields[1]), as.raw(c(0x61, 0xff)))
  expect_identical(fields[2], "b")
})


test_that("fixed split persists many fields", {
  string <- paste(rep(c("a", "", "é"), 128L), collapse = "|")
  strings <- charr:::ci_trim_both(rep(string, 16L))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expected <- rep(c("a", NA_character_, "é"), 128L)
  fields <- split_fixed(strings, "|", omit_empty = NA)
  expect_identical(fields, rep(list(expected), length(strings)))
})
