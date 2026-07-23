# charr-owned file: targeted equivalence tests for Reader-backed joins.

dup_strings <- function(x, times) charr:::ci_dup(x, times)
join_strings <- function(..., sep = "", collapse = NULL, ignore_null = FALSE) {
  charr:::ci_c(..., sep = sep, collapse = collapse, ignore_null = ignore_null)
}
flatten_strings <- function(x, collapse = "", na_empty = FALSE,
                            omit_empty = FALSE) {
  charr:::ci_flatten(x, collapse, na_empty, omit_empty)
}
join_two <- function(x, y) charr:::`%s+%`(x, y)
join_list <- function(x, sep = "", collapse = NULL) {
  charr:::ci_join_list(x, sep, collapse)
}


test_that("dup preserves recycling and times edge cases", {
  strings <- charr:::ci_trim_both(c(" aé🙂 ", " x ", " ", NA))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    dup_strings(strings, c(2L, 0L, -1L, NA_integer_)),
    c("aé🙂aé🙂", "", NA, NA)
  )
  expect_identical(dup_strings(strings, integer()), character())
  expect_warning(
    recycled <- dup_strings(strings, c(1L, 2L, 3L)),
    "longer object length is not a multiple"
  )
  expect_identical(recycled, c("aé🙂", "xx", "", NA))
})


test_that("binary and variadic joins recycle Reader records exactly", {
  left <- charr:::ci_trim_both(c(" a ", " é ", NA, " z "))
  right <- charr:::ci_trim_both(c(" 🙂 ", " x "))
  expect_identical(charport::is_charvec(left), charr_altrep())
  expect_identical(charport::is_charvec(right), charr_altrep())

  expect_identical(join_two(left, right), c("a🙂", "éx", NA, "zx"))
  expect_identical(
    join_strings(left, right, sep = "|"),
    c("a|🙂", "é|x", NA, "z|x")
  )
  expect_identical(
    join_strings(left, right, sep = "|", collapse = ";"),
    NA_character_
  )
})


test_that("join keeps empty-vector and collapsed special cases", {
  strings <- charr:::ci_trim_both(c(" a ", " é ", " 🙂 "))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(join_strings(strings, character()), character())
  expect_identical(
    join_strings(strings, character(), ignore_null = TRUE),
    strings
  )
  expect_identical(join_strings(), character())
  expect_identical(join_strings(collapse = ","), "")
  # With one argument and collapse, the copied fast path ignores sep.
  expect_identical(
    join_strings(strings, sep = NA_character_, collapse = ","),
    "a,é,🙂"
  )
  expect_warning(
    result <- join_strings(strings, c("1", "2"), sep = "-", collapse = ";"),
    "longer object length is not a multiple"
  )
  expect_identical(result, "a-1;é-2;🙂-1")
})


test_that("flatten preserves NA and empty-element controls", {
  strings <- charr:::ci_replace_all_fixed(
    c(NA, "", "A", "", "B", NA, "C"), "not present", ""
  )
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(flatten_strings(strings, ","), NA_character_)
  expect_identical(
    flatten_strings(strings, ",", na_empty = TRUE),
    ",,A,,B,,C"
  )
  expect_identical(
    flatten_strings(strings, ",", na_empty = NA),
    ",A,,B,C"
  )
  expect_identical(
    flatten_strings(strings, ",", na_empty = TRUE, omit_empty = TRUE),
    "A,B,C"
  )
  expect_identical(flatten_strings(character(), ","), "")
  expect_identical(flatten_strings(strings, NA_character_), NA_character_)
})


test_that("join_list flattens each nonempty list element", {
  first <- charr:::ci_trim_both(c(" a ", " é "))
  second <- charr:::ci_trim_both(c(" 🙂 ", " x ", " "))
  third <- charr:::ci_trim_both(c(" z ", NA))
  expect_identical(charport::is_charvec(first), charr_altrep())
  expect_identical(charport::is_charvec(second), charr_altrep())
  expect_identical(charport::is_charvec(third), charr_altrep())

  result <- join_list(list(first, character(), second, third), sep = "|")
  expect_identical(result, c("a|é", "🙂|x|", NA))
  expect_true(charport::is_charvec(result))
  expect_identical(
    join_list(list(first, character(), second), sep = "|", collapse = ";"),
    "a|é;🙂|x|"
  )
  expect_identical(join_list(list(character()), sep = "|"), character())
})


test_that("join copies malformed declared UTF-8 without validating", {
  malformed_byte <- rawToChar(as.raw(0xc3))
  Encoding(malformed_byte) <- "UTF-8"
  malformed <- charr:::ci_replace_all_fixed("a", "a", malformed_byte)
  expect_identical(charport::is_charvec(malformed), charr_altrep())

  joined <- join_strings(malformed, "x")
  expect_identical(charToRaw(joined), as.raw(c(0xc3, 0x78)))

  bytes <- "\xff"
  Encoding(bytes) <- "bytes"
  expect_error(dup_strings(bytes, 2L), "bytes encoding")
  expect_error(join_strings(bytes, "x"), "bytes encoding")
  expect_error(flatten_strings(bytes, ","), "bytes encoding")
})
