test_that("fixed replacement preserves byte-search semantics", {
  malformed <- rawToChar(as.raw(c(0x61, 0xff, 0x62)))
  Encoding(malformed) <- "UTF-8"
  bytes <- rawToChar(as.raw(c(0x61, 0xff, 0x62)))
  Encoding(bytes) <- "bytes"

  replaced <- charr_test_leaf("ci_replace_all_fixed")(malformed, "a", "x")
  expect_equal(charToRaw(replaced), as.raw(c(0x78, 0xff, 0x62)))
  expect_equal(
    charToRaw(charr_test_leaf("ci_replace_first_fixed")("a", "a", malformed)),
    as.raw(c(0x61, 0xff, 0x62))
  )
  expect_equal(
    charr_test_leaf("ci_replace_first_fixed")("a", "z", NA_character_),
    "a"
  )
  expect_true(is.na(
    charr_test_leaf("ci_replace_first_fixed")("a", "a", NA_character_)
  ))
  expect_warning(
    expect_true(is.na(charr_test_leaf("ci_replace_all_fixed")("a", "", "x"))),
    "empty search patterns"
  )
  expect_error(
    charr_test_leaf("ci_replace_first_fixed")(bytes, "z", "x"),
    "bytes encoding"
  )
  expect_error(
    charr_test_leaf("ci_replace_first_fixed")("a", "z", bytes),
    "bytes encoding"
  )
})

test_that("fixed replacement sequential mode preserves copied order", {
  strings <- rep(c("ababa", "café", "😀a", "", NA_character_), 200)
  run <- function() {
    list(
      first = charr_test_leaf("ci_replace_first_fixed")(strings, "a", "X"),
      all = charr_test_leaf("ci_replace_all_fixed")(strings, "a", "X"),
      sequential = charr_test_leaf("ci_replace_all_fixed")(
        strings, c("a", "X"), c("X", "!"), vectorize_all = FALSE
      )
    )
  }

  result <- run()
  expect_identical(
    result$first,
    stringi::stri_replace_first_fixed(strings, "a", "X")
  )
  expect_identical(
    result$all,
    stringi::stri_replace_all_fixed(strings, "a", "X")
  )
  expect_identical(
    result$sequential,
    stringi::stri_replace_all_fixed(
      strings, c("a", "X"), c("X", "!"), vectorize_all = FALSE
    )
  )
  expect_equal(
    charr_test_leaf("ci_replace_all_fixed")(
      "ababa", c("ab", "X"), c("X", "!"), vectorize_all = FALSE
    ),
    "!!a"
  )
  expect_identical(charport::is_charvec(result$all), charr_altrep())
})

test_that("fixed replacement bounds forward Reader scans", {
  strings <- charr_test_leaf("ci_trim_both")(c("aaa", "z", "aaaa", "xyz", "abcx"))
  expect_identical(charport::is_charvec(strings), charr_altrep())

  expect_identical(
    charr_test_leaf("ci_replace_first_fixed")(strings, "xy", "!"),
    c("aaa", "z", "aaaa", "!z", "abcx")
  )
})

test_that("fixed replacement borrows stable values and owns conversions", {
  large <- paste(rep("é🙂xyz", 256L), collapse = "")
  stable <- charr_test_leaf("ci_replace_all_fixed")(large, "~", "~")
  expect_identical(charport::is_charvec(stable), charr_altrep())
  expect_identical(
    charr_test_leaf("ci_replace_all_fixed")("a-a", "a", stable),
    paste0(stable, "-", stable)
  )

  latin1 <- rawToChar(as.raw(0xe9))
  Encoding(latin1) <- "latin1"
  converted <- charr_test_leaf("ci_replace_all_fixed")("a-a", "a", latin1)
  expect_identical(charToRaw(converted), as.raw(c(0xc3, 0xa9, 0x2d, 0xc3, 0xa9)))
  expect_identical(Encoding(converted), "UTF-8")
})
