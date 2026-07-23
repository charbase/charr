# Charr-owned tests for Reader-backed character-class search.
# These are not imported from stringr.

class_input <- function(x) {
  charport::as_charvec(x)
}

expect_class_unmaterialized <- function(x) {
  expect_true(charport::is_charvec(x))
  expect_false(charport::charport_info(x)$is_materialized)
}

expect_class_output <- function(x) {
  expect_true(charport::is_charvec(x))
  expect_false(charport::charport_info(x)$is_materialized)
}

class_latin1 <- function() {
  value <- rawToChar(as.raw(c(0x63, 0x61, 0x66, 0xe9)))
  Encoding(value) <- "latin1"
  value
}

test_that("character-class replace-all stays lazy and preserves vectorization", {
  values <- c("a12β", "é!", "none", NA_character_, "")
  patterns <- c("\\p{L}", "\\p{L}", "[0-9]", "\\p{L}", "[a]")
  replacements <- c("X", "λ", "#", "?", "!")
  subject <- class_input(values)
  pattern <- class_input(patterns)
  replacement <- class_input(replacements)

  all <- with_altrep(
    TRUE,
    charr:::ci_replace_all_charclass(
      subject, pattern, replacement, merge = FALSE
    )
  )
  expect_class_output(all)
  expect_identical(
    all,
    stringi::stri_replace_all_charclass(
      values, patterns, replacements, merge = FALSE
    )
  )

  sequential_values <- c("a1", "b2", NA_character_)
  sequential_subject <- class_input(sequential_values)
  sequential <- with_altrep(
    TRUE,
    charr:::ci_replace_all_charclass(
      sequential_subject,
      class_input(c("[a-z]", "[0-9]")),
      class_input(c("L", "N")),
      vectorize_all = FALSE
    )
  )
  expect_class_output(sequential)
  expect_identical(
    sequential,
    stringi::stri_replace_all_charclass(
      sequential_values, c("[a-z]", "[0-9]"), c("L", "N"),
      vectorize_all = FALSE
    )
  )
  expect_class_unmaterialized(subject)
  expect_class_unmaterialized(pattern)
  expect_class_unmaterialized(replacement)
  expect_class_unmaterialized(sequential_subject)
})

test_that("character-class trim routes through lazy backend output", {
  values <- c("  alpha  ", " β ", "", NA_character_, class_latin1())
  subject <- class_input(values)
  pattern <- class_input("\\P{Wspace}")

  for (name in c("ci_trim_left", "ci_trim_right", "ci_trim_both")) {
    actual <- with_altrep(
      TRUE,
      get(name, envir = asNamespace("charr"))(subject, pattern)
    )
    expected <- do.call(
      get(sub("^ci_", "stri_", name), envir = asNamespace("stringi")),
      list(values, "\\P{Wspace}")
    )
    expect_class_output(actual)
    expect_identical(actual, expected)
  }

  trimmed <- with_altrep(TRUE, charr:::ci_trim_both(subject, pattern))
  expect_identical(
    with_altrep(TRUE, charr:::ci_length(trimmed)),
    stringi::stri_length(stringi::stri_trim_both(values, "\\P{Wspace}"))
  )

  invalid_edge <- rawToChar(as.raw(c(0xc3, 0x28, 0x20)))
  Encoding(invalid_edge) <- "UTF-8"
  invalid_interior <- rawToChar(as.raw(c(0x61, 0xc3, 0x28, 0x20)))
  Encoding(invalid_interior) <- "UTF-8"
  edge_input <- class_input(invalid_edge)
  interior_input <- class_input(invalid_interior)
  expect_error(
    with_altrep(TRUE, charr:::ci_trim_both(edge_input)),
    "invalid UTF-8 byte sequence"
  )
  interior <- with_altrep(TRUE, charr:::ci_trim_both(interior_input))
  expect_class_output(interior)
  expect_identical(charToRaw(interior), as.raw(c(0x61, 0xc3, 0x28)))

  bytes <- rawToChar(as.raw(c(0xe9, 0x20)))
  Encoding(bytes) <- "bytes"
  bytes_input <- class_input(bytes)
  expect_error(
    with_altrep(TRUE, charr:::ci_trim_both(bytes_input)),
    "bytes encoding"
  )

  invalid_pattern <- class_input("[")
  expect_error(
    stringi::stri_trim_both("x", "["),
    "UnicodeSet pattern"
  )
  expect_error(
    with_altrep(TRUE, charr:::ci_trim_both(class_input("x"), invalid_pattern)),
    "UnicodeSet pattern"
  )
  expect_class_unmaterialized(subject)
  expect_class_unmaterialized(pattern)
  expect_class_unmaterialized(edge_input)
  expect_class_unmaterialized(interior_input)
  expect_class_unmaterialized(bytes_input)
  expect_class_unmaterialized(invalid_pattern)
})

test_that("character-class trim preserves vectorized pattern recycling", {
  values <- rep(
    c("  alpha  ", "\tbeta\t", "中 ", NA_character_),
    8L
  )
  patterns <- c("\\P{Wspace}", "[[:alpha:]]")
  subject <- class_input(values)
  pattern <- class_input(patterns)

  actual <- with_altrep(
    TRUE,
    charr:::ci_trim_both(subject, pattern)
  )
  expected <- stringi::stri_trim_both(values, patterns)

  expect_class_output(actual)
  expect_identical(actual, expected)
  expect_class_unmaterialized(subject)
  expect_class_unmaterialized(pattern)
})
