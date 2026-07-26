# charr-owned file: targeted equivalence tests for Reader-backed positional sub.

sub_pos <- function(...) charr:::ci_sub(...)
sub_all_pos <- function(...) charr:::ci_sub_all(...)
sub_replace <- function(..., replacement) {
  charr:::ci_sub_replace(..., replacement = replacement)
}
sub_replace_all <- function(..., replacement) {
  charr:::ci_sub_replace_all(..., replacement = replacement)
}


test_that("positional sub slices multibyte Reader records by code point", {
  strings <- charr:::ci_trim_both(c(" aé🙂üz ", " 🙂x🙂 ", " üü ", " abc "))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  result <- sub_pos(strings, from = c(2L, -2L, 2L, 2L), to = c(5L, -1L, 3L, 2L))
  expect_identical(result, c("é🙂ü", "x🙂", "̈u", "b"))
  expect_identical(charport::is_charvec(result), charr_altrep())
  expect_identical(Encoding(result), c("UTF-8", "UTF-8", "UTF-8", "unknown"))
})


test_that("positional sub preserves negative and out-of-range clamping", {
  strings <- charr:::ci_trim_both(rep(" aé🙂üz ", 10L))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    sub_pos(
      strings,
      from = c(-20L, -5L, -2L, -1L, 0L, 1L, 2L, 5L, 6L, 20L),
      to = c(-20L, -5L, -1L, -2L, 0L, 20L, 1L, 5L, 20L, 20L)
    ),
    c("", "é", "̈z", "", "", "aé🙂üz", "", "̈", "z", "")
  )
})


test_that("positional sub preserves length and matrix forms", {
  strings <- charr:::ci_trim_both(c(" aé🙂üz ", " abc ", " 🙂x🙂 "))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    sub_pos(strings, from = c(2L, -2L, 2L), length = c(3L, 1L, 0L)),
    c("é🙂u", "b", "")
  )
  expect_identical(
    sub_pos(strings, from = c(1L, 1L, 1L), length = c(-1L, 2L, -2L)),
    c(NA, "ab", NA)
  )
  expect_identical(
    sub_pos(
      strings, from = c(1L, 1L, 1L),
      length = c(-1L, 2L, -2L), ignore_negative_length = TRUE
    ),
    "ab"
  )

  # The copied container groups recycled source elements during its
  # negative-length compaction pass; this observable order is intentional.
  recycled <- charr:::ci_trim_both(c(" üü ", " aé🙂üz ", NA, NA, NA))
  expect_warning(
    compacted <- sub_pos(
      recycled, from = c(3L, 0L, 20L, -1L),
      length = c(-7L, 3L, 20L, 3L, 2L, -1L, 2L),
      ignore_negative_length = TRUE
    ),
    "longer object length is not a multiple"
  )
  expect_identical(compacted, c("aé", "", NA, NA, NA))

  from_to <- cbind(start = c(-2L, 2L, 1L), end = c(-1L, 20L, 2L))
  expect_identical(sub_pos(strings, from_to), c("̈z", "bc", "🙂x"))

  from_length <- cbind(start = c(-2L, 2L, 1L), length = c(1L, 20L, 2L))
  expect_identical(sub_pos(strings, from_length), c("̈", "bc", "🙂x"))
})


test_that("positional sub stays inside short and long adjacent records", {
  strings <- charr:::ci_trim_both(c(" a ", " very_long_🙂_record ", " z ", " é🙂 "))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    sub_pos(strings, from = c(1L, 6L, -1L, 2L), length = c(1L, 6L, 1L, 1L)),
    c("a", "long_🙂", "z", "🙂")
  )
})


test_that("single positional replacement preserves insertion and omission rules", {
  strings <- charr:::ci_trim_both(rep(" aé🙂üz ", 8L))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  replaced <- sub_replace(
    strings,
    from = c(-20L, -2L, -1L, 0L, 1L, 6L, 20L, NA_integer_),
    to = c(-20L, -1L, -2L, 0L, 0L, 20L, -20L, 1L),
    replacement = "X"
  )
  expect_identical(
    replaced,
    c(
      "Xaé🙂üz", "aé🙂uX", "aé🙂üXz", "Xaé🙂üz",
      "Xaé🙂üz", "aé🙂üX", "aé🙂üzX", NA
    )
  )
  expect_identical(charport::is_charvec(replaced), charr_altrep())

  expect_identical(
    sub_replace(
      strings[1:4], from = c(1L, 2L, -1L, 20L),
      length = c(-1L, 0L, 1L, 2L), replacement = c("X", "Y")
    ),
    c("aé🙂üz", "aYé🙂üz", "aé🙂üX", "aé🙂üzY")
  )

  expect_identical(
    sub_replace(
      strings[1:3], from = c(NA_integer_, 1L, 1L), to = 2L,
      omit_na = TRUE, replacement = c("X", NA, "X")
    ),
    c("aé🙂üz", "aé🙂üz", "X🙂üz")
  )
})


test_that("sub_all preserves inner shapes, matrices, and negative lengths", {
  strings <- charr:::ci_trim_both(c(" aé🙂üz ", " abc ", NA_character_))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    sub_all_pos(
      strings,
      from = list(c(-2L, 1L, 20L), c(3L, 1L), c(1L, -1L)),
      to = list(c(-1L, 2L, 20L), c(2L, 3L), c(2L, -1L))
    ),
    list(c("̈z", "aé", ""), c("", "abc"), rep(NA_character_, 2L))
  )

  expect_identical(
    sub_all_pos(
      strings,
      from = list(c(1L, 2L, 3L), c(1L, -1L), 1L),
      length = list(c(2L, -1L, 0L), c(20L, 2L), -1L),
      ignore_negative_length = TRUE
    ),
    list(c("aé", ""), c("abc", "c"), NA_character_)
  )

  locations <- list(
    cbind(start = c(1L, 3L), length = c(1L, 2L)),
    cbind(start = c(1L, 2L), end = c(1L, 3L)),
    cbind(start = 1L, length = 1L)
  )
  expect_identical(
    sub_all_pos(strings, locations),
    list(c("a", "🙂u"), c("a", "bc"), NA_character_)
  )
})


test_that("sub_all replacement splices sorted disjoint code-point ranges", {
  strings <- charr:::ci_trim_both(c(" aé🙂üz ", " abcdef "))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  result <- sub_replace_all(
    strings,
    from = list(c(1L, 3L, 6L), c(2L, 5L)),
    to = list(c(1L, 3L, 6L), c(3L, 6L)),
    replacement = list(c("A", "B", "C"), c("X", "Y"))
  )
  expect_identical(result, c("AéBüC", "aXdY"))
  expect_identical(charport::is_charvec(result), charr_altrep())

  expect_identical(
    sub_replace_all(
      strings[1], c(1L, 3L, 6L), length = c(1L, 0L, 20L),
      replacement = c("A", "B", "C")
    ),
    "AéB🙂üC"
  )
  expect_identical(
    sub_replace_all(
      strings[1], c(-5L, -2L), c(-5L, -1L),
      replacement = c("A", "B")
    ),
    "aA🙂uB"
  )
  expect_error(
    sub_replace_all(
      strings[1], c(1L, 2L), c(3L, 2L), replacement = c("A", "B")
    ),
    "index ranges must be sorted and mutually disjoint",
    fixed = TRUE
  )
})


test_that("positional sub is lenient for malformed UTF-8 Reader records", {
  malformed <- rawToChar(as.raw(c(0x61, 0xff, 0x62, 0x63)))
  Encoding(malformed) <- "UTF-8"
  strings <- charr:::ci_replace_all_fixed(malformed, "z", "z")
  expect_identical(charport::is_charvec(strings), charr_altrep())

  extracted <- sub_pos(strings, 2L, 3L)
  expect_identical(charToRaw(extracted), as.raw(c(0xff, 0x62)))
  replaced <- sub_replace(strings, 2L, 2L, replacement = "X")
  expect_identical(charToRaw(replaced), charToRaw("aXbc"))
})
