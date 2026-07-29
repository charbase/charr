# charr-owned targeted equivalence tests for Reader-backed regex extract.

extract_first_regex <- function(...) charr:::ci_extract_first_regex(...)
extract_all_regex <- function(...) charr:::ci_extract_all_regex(...)


test_that("regex extract emits UTF-16 matches with exact encodings", {
  strings <- charr:::ci_trim_both(c(
    " 😀a😀 ", " 𐐷Z𐐷 ", " é😀b ", " üü ", " abc "
  ))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  first <- extract_first_regex(strings, c("😀a", "𐐷Z", "é😀", "ü", "b"))
  expect_identical(first, c("😀a", "𐐷Z", "é😀", "ü", "b"))
  expect_identical(charport::is_charvec(first), charr_altrep())
  expect_identical(
    Encoding(first), c("UTF-8", "UTF-8", "UTF-8", "UTF-8", "unknown")
  )
})


test_that("regex extract all preserves zero-length ICU advancement", {
  strings <- charr:::ci_trim_both(c(" 😀a ", " 𐐷 ", "", "a"))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    extract_all_regex(strings, ".*?", omit_no_match = TRUE),
    list(c("", "", ""), c("", ""), "", c("", ""))
  )
  expect_identical(
    extract_all_regex(strings, c("(?=a)", "(?=𐐷)", "^", "$")),
    list("", "", "", "")
  )
})


test_that("regex extract preserves NA, omit, simplify, and recycling shapes", {
  strings <- charr:::ci_trim_both(c(" aa ", " none ", "", NA_character_))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    extract_all_regex(strings, "a", omit_no_match = FALSE),
    list(c("a", "a"), NA_character_, NA_character_, NA_character_)
  )
  expect_identical(
    extract_all_regex(strings, "a", omit_no_match = TRUE),
    list(c("a", "a"), character(), character(), NA_character_)
  )
  expect_identical(
    extract_all_regex(strings, "a", omit_no_match = TRUE, simplify = TRUE),
    structure(c("a", "", "", NA, "a", "", "", ""), dim = c(4L, 2L))
  )
  expect_identical(
    extract_all_regex(strings, "a", omit_no_match = TRUE, simplify = NA),
    structure(c("a", NA, NA, NA, "a", NA, NA, NA), dim = c(4L, 2L))
  )

  expect_warning(
    recycled <- extract_first_regex(strings[1:3], c("a", "n")),
    "longer object length is not a multiple"
  )
  expect_identical(recycled, c("a", "n", NA_character_))
})


test_that("regex extract preserves malformed declared UTF-8 like stringi", {
  malformed <- rawToChar(as.raw(0x80))
  Encoding(malformed) <- "UTF-8"
  with_altrep(TRUE, {
    malformed <- charport::as_charvec(malformed)
    expect_true(charport::is_charvec(malformed))

    # R and stringi retain the declared UTF-8 byte here. UText substitutes
    # while matching, but the copied extraction path returns the source slice.
    replacement <- rawToChar(as.raw(0x80))
    Encoding(replacement) <- "UTF-8"
    expect_identical(extract_first_regex(malformed, "."), replacement)
    expect_identical(
      extract_all_regex(malformed, ".", omit_no_match = TRUE),
      list(replacement)
    )
  })
})


test_that("regex extract compiles only patterns reached by the copied loop", {
  missing <- charr:::ci_trim_both(NA_character_)
  present <- charr:::ci_trim_both(" abc ")
  expect_identical(charport::is_charvec(missing), charr_altrep())
  expect_identical(charport::is_charvec(present), charr_altrep())

  expect_identical(extract_first_regex(missing, "["), NA_character_)
  expect_identical(
    extract_all_regex(missing, "[", omit_no_match = TRUE),
    list(NA_character_)
  )
  expect_error(
    extract_first_regex(present, "["),
    "Missing closing bracket.*U_REGEX_MISSING_CLOSE_BRACKET.*context=`\\["
  )
})
