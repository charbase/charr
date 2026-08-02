# Charr-owned tests for Reader-backed regex detection and counting.
# These are not imported from stringr.

regex_dc_marked_string <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_regex_dc_unmaterialized <- function(x) {
  expect_altrep_unmaterialized(x)
}

regex_dc_condition_events <- function(expr) {
  events <- character()
  tryCatch(
    withCallingHandlers(
      force(expr),
      warning = function(condition) {
        events <<- c(events, paste0("warning:", conditionMessage(condition)))
        invokeRestart("muffleWarning")
      }
    ),
    error = function(condition) {
      events <<- c(events, paste0("error:", conditionMessage(condition)))
    }
  )
  events
}

test_that("regex predicates consume unmaterialized inputs", {
  latin1 <- regex_dc_marked_string(
    c(0x63, 0x61, 0x66, 0xe9), "latin1"
  )
  malformed <- regex_dc_marked_string(c(0x61, 0xff, 0x62), "UTF-8")
  values <- c(
    "caf\u00e9", "\U0001f600a\U0001f600", "u\u0308", "",
    NA_character_, latin1, "\ufeffabc", malformed
  )
  patterns <- c(
    "\u00e9", "\U0001f600", "\\p{M}", "^$",
    "x", "\u00e9", "^a", "\\ufffd"
  )
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)

  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_detect_regex")(subject, pattern)),
    stringi::stri_detect_regex(values, patterns)
  )
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_count_regex")(subject, pattern)),
    stringi::stri_count_regex(values, patterns)
  )
  expect_regex_dc_unmaterialized(subject)
  expect_regex_dc_unmaterialized(pattern)
})

test_that("regex predicates handle exact aliases and recycling", {
  values <- c("same", "x", "\u00e9")
  alias <- charport::as_charvec(values)

  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_detect_regex")(alias, alias)),
    stringi::stri_detect_regex(values, values)
  )
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_count_regex")(alias, alias)),
    stringi::stri_count_regex(values, values)
  )
  expect_regex_dc_unmaterialized(alias)

  values <- c("a", "b", "aa")
  patterns <- c("a", "b")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  warning <- "^longer object length is not a multiple of shorter object length$"

  expected <- actual <- NULL
  expect_warning(
    expected <- stringi::stri_detect_regex(values, patterns),
    warning
  )
  expect_warning(
    actual <- with_test_backend(TRUE, charr_test_leaf("ci_detect_regex")(subject, pattern)),
    warning
  )
  expect_identical(actual, expected)

  expect_warning(
    expected <- stringi::stri_count_regex(values, patterns),
    warning
  )
  expect_warning(
    actual <- with_test_backend(TRUE, charr_test_leaf("ci_count_regex")(subject, pattern)),
    warning
  )
  expect_identical(actual, expected)

  opts_regex <- list(bogus = TRUE)
  expect_identical(
    regex_dc_condition_events(
      stringi::stri_detect_regex(
        values, patterns, opts_regex = opts_regex
      )
    ),
    regex_dc_condition_events(
      with_test_backend(
        TRUE,
        charr_test_leaf("ci_detect_regex")(
          subject, pattern, opts_regex = opts_regex
        )
      )
    )
  )
  expect_identical(
    regex_dc_condition_events(
      stringi::stri_count_regex(
        values, patterns, opts_regex = opts_regex
      )
    ),
    regex_dc_condition_events(
      with_test_backend(
        TRUE,
        charr_test_leaf("ci_count_regex")(
          subject, pattern, opts_regex = opts_regex
        )
      )
    )
  )
  expect_regex_dc_unmaterialized(subject)
  expect_regex_dc_unmaterialized(pattern)
})

test_that("regex predicates preserve empty and missing semantics", {
  values <- c("", "a", NA_character_)
  subject <- charport::as_charvec(values)
  empty_pattern <- charport::as_charvec("")
  warning <- "^empty search patterns are not supported$"

  expected <- actual <- NULL
  expect_warning(
    expected <- stringi::stri_detect_regex(values, ""),
    warning
  )
  expect_warning(
    actual <- with_test_backend(
      TRUE, charr_test_leaf("ci_detect_regex")(subject, empty_pattern)
    ),
    warning
  )
  expect_identical(actual, expected)

  expect_warning(
    expected <- stringi::stri_count_regex(values, ""),
    warning
  )
  expect_warning(
    actual <- with_test_backend(
      TRUE, charr_test_leaf("ci_count_regex")(subject, empty_pattern)
    ),
    warning
  )
  expect_identical(actual, expected)

  anchors <- charport::as_charvec(c("", "a", NA_character_))
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_detect_regex")(anchors, "^$")),
    stringi::stri_detect_regex(c("", "a", NA_character_), "^$")
  )
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_count_regex")(anchors, "^$")),
    stringi::stri_count_regex(c("", "a", NA_character_), "^$")
  )
  expect_regex_dc_unmaterialized(subject)
  expect_regex_dc_unmaterialized(empty_pattern)
  expect_regex_dc_unmaterialized(anchors)
})

test_that("regex detection preserves negate and max_count traversal", {
  values <- c("a", "b", "a", "c")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec("a")

  expect_identical(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_detect_regex")(
        subject, pattern, negate = FALSE, max_count = 1L
      )
    ),
    stringi::stri_detect_regex(values, "a", max_count = 1L)
  )
  expect_identical(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_detect_regex")(
        subject, pattern, negate = TRUE, max_count = 2L
      )
    ),
    stringi::stri_detect_regex(
      values, "a", negate = TRUE, max_count = 2L
    )
  )
  expect_regex_dc_unmaterialized(subject)
  expect_regex_dc_unmaterialized(pattern)
})

test_that("regex count preserves ICU zero-length advancement", {
  values <- c("", "a", "\U0001f600a", "\U0001f600")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(".*?")

  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_count_regex")(subject, pattern)),
    stringi::stri_count_regex(values, ".*?")
  )
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_count_regex")(subject, "(?=a)")),
    stringi::stri_count_regex(values, "(?=a)")
  )
  expect_regex_dc_unmaterialized(subject)
  expect_regex_dc_unmaterialized(pattern)
})

test_that("regex compilation stays lazy after eager input conversion", {
  present <- charport::as_charvec("x")
  missing <- charport::as_charvec(NA_character_)
  exhausted <- charport::as_charvec(c("x", "x"))
  invalid <- charport::as_charvec("[")

  expect_error(
    with_test_backend(TRUE, charr_test_leaf("ci_detect_regex")(present, invalid)),
    "U_REGEX_MISSING_CLOSE_BRACKET"
  )
  expect_error(
    with_test_backend(TRUE, charr_test_leaf("ci_count_regex")(present, invalid)),
    "U_REGEX_MISSING_CLOSE_BRACKET"
  )
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_detect_regex")(missing, invalid)),
    stringi::stri_detect_regex(NA_character_, "[")
  )
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_count_regex")(missing, invalid)),
    stringi::stri_count_regex(NA_character_, "[")
  )
  expect_identical(
    with_test_backend(
      TRUE, charr_test_leaf("ci_detect_regex")(present, invalid, max_count = 0L)
    ),
    stringi::stri_detect_regex("x", "[", max_count = 0L)
  )
  expect_identical(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_detect_regex")(
        exhausted, charport::as_charvec(c("x", "[")), max_count = 1L
      )
    ),
    stringi::stri_detect_regex(c("x", "x"), c("x", "["), max_count = 1L)
  )
  expect_regex_dc_unmaterialized(present)
  expect_regex_dc_unmaterialized(missing)
  expect_regex_dc_unmaterialized(exhausted)
  expect_regex_dc_unmaterialized(invalid)
})

test_that("regex predicates preserve bytes validation and event order", {
  bytes <- regex_dc_marked_string(c(0xff, 0xfe), "bytes")
  bytes_subject <- charport::as_charvec(bytes)
  bytes_pattern <- charport::as_charvec(bytes)

  expect_error(
    with_test_backend(TRUE, charr_test_leaf("ci_detect_regex")(bytes_subject, "a")),
    "bytes encoding"
  )
  expect_error(
    with_test_backend(TRUE, charr_test_leaf("ci_count_regex")(bytes_subject, "a")),
    "bytes encoding"
  )
  expect_error(
    with_test_backend(TRUE, charr_test_leaf("ci_detect_regex")("a", bytes_pattern)),
    "bytes encoding"
  )
  expect_error(
    with_test_backend(TRUE, charr_test_leaf("ci_count_regex")("a", bytes_pattern)),
    "bytes encoding"
  )
  expect_error(
    with_test_backend(TRUE, charr_test_leaf("ci_detect_regex")(NA_character_, bytes_pattern)),
    "bytes encoding"
  )
  expect_error(
    with_test_backend(TRUE, charr_test_leaf("ci_count_regex")(NA_character_, bytes_pattern)),
    "bytes encoding"
  )
  expect_error(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_detect_regex")(bytes_subject, "a", max_count = 0L)
    ),
    "bytes encoding"
  )

  values <- c(bytes, "a", "b")
  patterns <- c("a", "b")
  actual_subject <- charport::as_charvec(values)
  actual_pattern <- charport::as_charvec(patterns)
  expect_identical(
    regex_dc_condition_events(
      stringi::stri_detect_regex(values, patterns)
    ),
    regex_dc_condition_events(
      with_test_backend(
        TRUE, charr_test_leaf("ci_detect_regex")(actual_subject, actual_pattern)
      )
    )
  )
  expect_identical(
    regex_dc_condition_events(
      stringi::stri_count_regex(values, patterns)
    ),
    regex_dc_condition_events(
      with_test_backend(
        TRUE, charr_test_leaf("ci_count_regex")(actual_subject, actual_pattern)
      )
    )
  )

  expect_regex_dc_unmaterialized(bytes_subject)
  expect_regex_dc_unmaterialized(bytes_pattern)
  expect_regex_dc_unmaterialized(actual_subject)
  expect_regex_dc_unmaterialized(actual_pattern)
})

test_that("empty-pattern warnings precede lazy regex errors", {
  values <- c("x", "x")
  patterns <- c("", "[")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)

  expect_identical(
    regex_dc_condition_events(
      stringi::stri_detect_regex(values, patterns)
    ),
    regex_dc_condition_events(
      with_test_backend(TRUE, charr_test_leaf("ci_detect_regex")(subject, pattern))
    )
  )
  expect_identical(
    regex_dc_condition_events(
      stringi::stri_count_regex(values, patterns)
    ),
    regex_dc_condition_events(
      with_test_backend(TRUE, charr_test_leaf("ci_count_regex")(subject, pattern))
    )
  )
  expect_regex_dc_unmaterialized(subject)
  expect_regex_dc_unmaterialized(pattern)
})
