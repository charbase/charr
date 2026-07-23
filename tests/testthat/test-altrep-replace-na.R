# Charr-owned tests for Reader-backed missing-value replacement.
# These are not imported from stringr.

replace_na_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_replace_na_unmaterialized <- function(x) {
  expect_true(charport::is_charvec(x))
  expect_false(charport::charport_info(x)$is_materialized)
}

test_that("replace_na returns unmaterialized normalized output", {
  latin1 <- replace_na_marked(c(0x63, 0x61, 0x66, 0xe9), "latin1")
  values <- c("plain", NA_character_, latin1, "\ufeffbom", NA_character_)
  replacement_value <- "\ufeff\u00e9"
  subject <- charport::as_charvec(values)
  replacement <- charport::as_charvec(replacement_value)

  actual <- with_altrep(
    TRUE,
    charr:::ci_replace_na(subject, replacement)
  )
  expected <- with_altrep(
    FALSE,
    charr:::ci_replace_na(values, replacement_value)
  )
  expect_replace_na_unmaterialized(actual)
  expect_identical(actual, expected)
  expect_identical(
    with_altrep(TRUE, charr:::ci_count_fixed(actual, "\u00e9")),
    stringi::stri_count_fixed(expected, "\u00e9")
  )
  expect_replace_na_unmaterialized(actual)
  expect_replace_na_unmaterialized(subject)
  expect_replace_na_unmaterialized(replacement)
})

test_that("replace_na handles missing replacement and exact aliases", {
  values <- c("a", NA_character_, "b")
  subject <- charport::as_charvec(values)
  replacement <- charport::as_charvec(NA_character_)

  actual <- with_altrep(
    TRUE,
    charr:::ci_replace_na(subject, replacement)
  )
  expected <- with_altrep(
    FALSE,
    charr:::ci_replace_na(values, NA_character_)
  )
  expect_identical(actual, expected)
  expect_replace_na_unmaterialized(actual)
  expect_replace_na_unmaterialized(subject)
  expect_replace_na_unmaterialized(replacement)

  shared <- charport::as_charvec(NA_character_)
  expect_identical(
    with_altrep(TRUE, charr:::ci_replace_na(shared, shared)),
    with_altrep(
      FALSE,
      charr:::ci_replace_na(NA_character_, NA_character_)
    )
  )
  expect_replace_na_unmaterialized(shared)
})

test_that("replace_na preserves malformed declared UTF-8 bytes", {
  malformed <- replace_na_marked(c(0x61, 0xff), "UTF-8")
  subject <- charport::as_charvec(c(malformed, NA_character_))
  replacement <- charport::as_charvec(malformed)

  actual <- with_altrep(
    TRUE,
    charr:::ci_replace_na(subject, replacement)
  )
  expect_replace_na_unmaterialized(actual)
  expect_identical(charToRaw(actual[[1]]), as.raw(c(0x61, 0xff)))
  expect_identical(charToRaw(actual[[2]]), as.raw(c(0x61, 0xff)))
  expect_replace_na_unmaterialized(subject)
  expect_replace_na_unmaterialized(replacement)
})

test_that("replace_na validates bytes eagerly in copied container order", {
  malformed <- replace_na_marked(c(0x61, 0xff), "UTF-8")
  bytes <- malformed
  Encoding(bytes) <- "bytes"
  bytes_subject <- charport::as_charvec(bytes)
  bytes_replacement <- charport::as_charvec(bytes)
  plain_subject <- charport::as_charvec("plain")
  empty_subject <- charport::as_charvec(character())

  empty_result <- with_altrep(
    TRUE,
    charr:::ci_replace_na(empty_subject, "x")
  )
  expect_replace_na_unmaterialized(empty_result)
  expect_identical(empty_result, character())
  expect_error(
    with_altrep(
      TRUE,
      charr:::ci_replace_na(bytes_subject, "x")
    ),
    "bytes encoding"
  )
  expect_error(
    with_altrep(
      TRUE,
      charr:::ci_replace_na(plain_subject, bytes_replacement)
    ),
    "bytes encoding"
  )
  expect_error(
    with_altrep(
      TRUE,
      charr:::ci_replace_na(empty_subject, bytes_replacement)
    ),
    "bytes encoding"
  )
  expect_replace_na_unmaterialized(bytes_subject)
  expect_replace_na_unmaterialized(bytes_replacement)
  expect_replace_na_unmaterialized(plain_subject)
  expect_replace_na_unmaterialized(empty_subject)
})

test_that("replace_na preserves scalar replacement preparation", {
  values <- c(NA_character_, "a")
  subject <- charport::as_charvec(values)
  replacement_values <- c("x", "y")
  replacement <- charport::as_charvec(replacement_values)
  warning <- paste0(
    "^argument `replacement` should be a single character string; ",
    "only the first element is used$"
  )

  expected <- NULL
  actual <- NULL
  expect_warning(
    expected <- with_altrep(
      FALSE,
      charr:::ci_replace_na(values, replacement_values)
    ),
    warning
  )
  expect_warning(
    actual <- with_altrep(
      TRUE,
      charr:::ci_replace_na(subject, replacement)
    ),
    warning
  )
  expect_identical(actual, expected)
  expect_replace_na_unmaterialized(actual)
  expect_replace_na_unmaterialized(subject)
  expect_replace_na_unmaterialized(replacement)

  expect_error(
    with_altrep(TRUE, charr:::ci_replace_na(subject, character())),
    "should be a non-empty vector"
  )
})
