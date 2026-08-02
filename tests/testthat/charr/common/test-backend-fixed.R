# charr-owned tests for Reader-backed fixed search. These are not imported
# from stringr.

fixed_marked_string <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_fixed_input_unmaterialized <- function(x) {
  expect_altrep_unmaterialized(x)
}

test_that("fixed detection matches stringi on unmaterialized inputs", {
  latin1 <- fixed_marked_string(c(0x63, 0x61, 0x66, 0xe9), "latin1")
  bom_pattern <- enc2utf8("\ufeffa")
  values <- c(
    "alpha", "caf\u00e9", NA_character_, "", "\u00c9clair", "z", latin1, "a"
  )
  patterns <- c(
    "ph", "\u00e9", "x", "q", "\u00c9", "z", "\u00e9", bom_pattern
  )
  expected <- with_test_backend(
    FALSE,
    str_detect(values, fixed(patterns))
  )
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  fixed_pattern <- fixed(pattern)

  expect_fixed_input_unmaterialized(fixed_pattern)
  expect_identical(
    with_test_backend(TRUE, str_detect(subject, fixed_pattern)),
    expected
  )
  expect_fixed_input_unmaterialized(subject)
  expect_fixed_input_unmaterialized(pattern)
  expect_fixed_input_unmaterialized(fixed_pattern)

  long_value <- "prefix-0123456789abcdef-suffix"
  long_pattern_value <- "0123456789abcdef"
  long_subject <- charport::as_charvec(long_value)
  long_pattern <- charport::as_charvec(long_pattern_value)

  expect_identical(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_detect_fixed")(long_subject, long_pattern)
    ),
    with_test_backend(
      FALSE,
      charr_test_leaf("ci_detect_fixed")(long_value, long_pattern_value)
    )
  )
  expect_fixed_input_unmaterialized(long_subject)
  expect_fixed_input_unmaterialized(long_pattern)
})

test_that("fixed detection handles aliases and case-insensitive matching", {
  alias <- charport::as_charvec(c("same", "x", "\u00e9"))

  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_detect_fixed")(alias, alias)),
    with_test_backend(
      FALSE,
      charr_test_leaf("ci_detect_fixed")(c("same", "x", "\u00e9"), c("same", "x", "\u00e9"))
    )
  )
  expect_fixed_input_unmaterialized(alias)

  values <- c("\u00c9", "e\u0301", "\u00e9")
  pattern_value <- "\u00e9"
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(pattern_value)

  expect_identical(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_detect_fixed")(
        subject, pattern, case_insensitive = TRUE
      )
    ),
    with_test_backend(
      FALSE,
      charr_test_leaf("ci_detect_fixed")(
        values, pattern_value, case_insensitive = TRUE
      )
    )
  )
  expect_fixed_input_unmaterialized(subject)
  expect_fixed_input_unmaterialized(pattern)
})

test_that("fixed detection preserves negate and max_count behavior", {
  values <- c("a", "a", "a", "b")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec("a")
  expected <- with_test_backend(
    FALSE,
    charr_test_leaf("ci_detect_fixed")(values, "a", max_count = 2L)
  )

  expect_identical(expected, c(TRUE, TRUE, NA, NA))
  expect_identical(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_detect_fixed")(subject, pattern, max_count = 2L)
    ),
    expected
  )
  expect_fixed_input_unmaterialized(subject)
  expect_fixed_input_unmaterialized(pattern)

  values <- c("a", "b", "c")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec("a")
  expected <- with_test_backend(
    FALSE,
    charr_test_leaf("ci_detect_fixed")(values, "a", negate = TRUE, max_count = 1L)
  )

  expect_identical(expected, c(FALSE, TRUE, NA))
  expect_identical(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_detect_fixed")(
        subject, pattern, negate = TRUE, max_count = 1L
      )
    ),
    expected
  )
  expect_fixed_input_unmaterialized(subject)
  expect_fixed_input_unmaterialized(pattern)
})

test_that("fixed detection preserves native recycling warnings", {
  values <- c("a", "b", "c")
  patterns <- c("a", "b")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  warning <- paste0(
    "^longer object length is not a multiple of shorter object length$"
  )

  expected <- NULL
  actual <- NULL
  expect_warning(
    expected <- with_test_backend(FALSE, charr_test_leaf("ci_detect_fixed")(values, patterns)),
    warning
  )
  expect_warning(
    actual <- with_test_backend(TRUE, charr_test_leaf("ci_detect_fixed")(subject, pattern)),
    warning
  )

  expect_identical(actual, expected)
  expect_fixed_input_unmaterialized(subject)
  expect_fixed_input_unmaterialized(pattern)
})

test_that("empty fixed patterns warn and return missing values", {
  values <- c("", "a", NA_character_)
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec("")
  warning <- "^empty search patterns are not supported$"

  expected <- NULL
  actual <- NULL
  expect_warning(
    expected <- with_test_backend(FALSE, charr_test_leaf("ci_detect_fixed")(values, "")),
    warning
  )
  expect_warning(
    actual <- with_test_backend(TRUE, charr_test_leaf("ci_detect_fixed")(subject, pattern)),
    warning
  )

  expect_identical(expected, rep(NA, 3))
  expect_identical(actual, expected)
  expect_fixed_input_unmaterialized(subject)
  expect_fixed_input_unmaterialized(pattern)
})

test_that("zero recycling avoids fixed input readers", {
  bytes_value <- fixed_marked_string(c(0xff, 0xfe), "bytes")
  subject <- charport::as_charvec(bytes_value)
  pattern <- charport::as_charvec(character())

  expected <- expect_no_warning(
    with_test_backend(
      FALSE,
      charr_test_leaf("ci_detect_fixed")(bytes_value, character())
    )
  )
  actual <- expect_no_warning(
    with_test_backend(TRUE, charr_test_leaf("ci_detect_fixed")(subject, pattern))
  )

  expect_identical(expected, logical())
  expect_identical(actual, expected)
  expect_fixed_input_unmaterialized(subject)
  expect_fixed_input_unmaterialized(pattern)
})

test_that("fixed detection validates subjects before loop shortcuts", {
  bytes_value <- fixed_marked_string(c(0xff, 0xfe), "bytes")
  subject <- charport::as_charvec(bytes_value)
  pattern <- charport::as_charvec("a")

  expect_error(
    with_test_backend(
      FALSE,
      charr_test_leaf("ci_detect_fixed")(bytes_value, "a", max_count = 0L)
    ),
    "bytes encoding"
  )
  expect_error(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_detect_fixed")(subject, pattern, max_count = 0L)
    ),
    "bytes encoding"
  )
  expect_fixed_input_unmaterialized(subject)
  expect_fixed_input_unmaterialized(pattern)

  subject <- charport::as_charvec(bytes_value)
  pattern <- charport::as_charvec("")
  reference_warnings <- 0L
  actual_warnings <- 0L

  expect_error(
    withCallingHandlers(
      with_test_backend(FALSE, charr_test_leaf("ci_detect_fixed")(bytes_value, "")),
      warning = function(cnd) {
        reference_warnings <<- reference_warnings + 1L
        invokeRestart("muffleWarning")
      }
    ),
    "bytes encoding"
  )
  expect_error(
    withCallingHandlers(
      with_test_backend(TRUE, charr_test_leaf("ci_detect_fixed")(subject, pattern)),
      warning = function(cnd) {
        actual_warnings <<- actual_warnings + 1L
        invokeRestart("muffleWarning")
      }
    ),
    "bytes encoding"
  )

  expect_identical(reference_warnings, 0L)
  expect_identical(actual_warnings, reference_warnings)
  expect_fixed_input_unmaterialized(subject)
  expect_fixed_input_unmaterialized(pattern)
})

test_that("fixed counting matches stringi on unmaterialized inputs", {
  latin1 <- fixed_marked_string(c(0x63, 0x61, 0x66, 0xe9), "latin1")
  bom_pattern <- enc2utf8("\ufeffa")
  values <- c("banana", "caf\u00e9", NA_character_, "", latin1, "aa")
  patterns <- c("a", "\u00e9", "x", "a", "\u00e9", bom_pattern)
  expected <- with_test_backend(FALSE, str_count(values, fixed(patterns)))
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  fixed_pattern <- fixed(pattern)

  expect_identical(expected, c(3L, 1L, NA_integer_, 0L, 1L, 2L))
  expect_fixed_input_unmaterialized(fixed_pattern)
  expect_identical(
    with_test_backend(TRUE, str_count(subject, fixed_pattern)),
    expected
  )
  expect_fixed_input_unmaterialized(subject)
  expect_fixed_input_unmaterialized(pattern)
  expect_fixed_input_unmaterialized(fixed_pattern)
})

test_that("fixed counting handles aliases and each matcher lane", {
  values <- c("same", "x", "\u00e9")
  shared <- charport::as_charvec(values)

  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_count_fixed")(shared, shared)),
    with_test_backend(FALSE, charr_test_leaf("ci_count_fixed")(values, values))
  )
  expect_fixed_input_unmaterialized(shared)

  long_pattern_value <- "0123456789abcdef"
  values <- c(
    "aaaaa", "abcabcabc",
    paste0(long_pattern_value, long_pattern_value)
  )
  patterns <- c("a", "abc", long_pattern_value)
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  expected <- with_test_backend(FALSE, charr_test_leaf("ci_count_fixed")(values, patterns))

  expect_identical(expected, c(5L, 3L, 2L))
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_count_fixed")(subject, pattern)),
    expected
  )
  expect_fixed_input_unmaterialized(subject)
  expect_fixed_input_unmaterialized(pattern)
})

test_that("fixed counting preserves overlap options", {
  values <- c("aaaa", "ababa", "\u00e9\u00e9\u00e9")
  patterns <- c("aa", "aba", "\u00e9\u00e9")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)

  expected <- with_test_backend(FALSE, charr_test_leaf("ci_count_fixed")(values, patterns))
  expect_identical(expected, c(2L, 1L, 1L))
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_count_fixed")(subject, pattern)),
    expected
  )

  expected <- with_test_backend(
    FALSE,
    charr_test_leaf("ci_count_fixed")(values, patterns, overlap = TRUE)
  )
  expect_identical(expected, c(3L, 2L, 2L))
  expect_identical(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_count_fixed")(subject, pattern, overlap = TRUE)
    ),
    expected
  )
  expect_fixed_input_unmaterialized(subject)
  expect_fixed_input_unmaterialized(pattern)

  value <- "\u00c9\u00e9\u00c9"
  pattern_value <- "\u00e9\u00e9"
  subject <- charport::as_charvec(value)
  pattern <- charport::as_charvec(pattern_value)
  expected <- with_test_backend(
    FALSE,
    charr_test_leaf("ci_count_fixed")(
      value, pattern_value, case_insensitive = TRUE, overlap = TRUE
    )
  )

  expect_identical(expected, 2L)
  expect_identical(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_count_fixed")(
        subject, pattern, case_insensitive = TRUE, overlap = TRUE
      )
    ),
    expected
  )
  expect_fixed_input_unmaterialized(subject)
  expect_fixed_input_unmaterialized(pattern)
})

test_that("fixed counting preserves native and empty-pattern warnings", {
  values <- c("a", "b", "c")
  patterns <- c("a", "b")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  warning <- paste0(
    "^longer object length is not a multiple of shorter object length$"
  )
  expected <- NULL
  actual <- NULL

  expect_warning(
    expected <- with_test_backend(FALSE, charr_test_leaf("ci_count_fixed")(values, patterns)),
    warning
  )
  expect_warning(
    actual <- with_test_backend(TRUE, charr_test_leaf("ci_count_fixed")(subject, pattern)),
    warning
  )
  expect_identical(actual, expected)
  expect_fixed_input_unmaterialized(subject)
  expect_fixed_input_unmaterialized(pattern)

  values <- c("", "a", NA_character_)
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec("")
  warning <- "^empty search patterns are not supported$"

  expect_warning(
    expected <- with_test_backend(FALSE, charr_test_leaf("ci_count_fixed")(values, "")),
    warning
  )
  expect_warning(
    actual <- with_test_backend(TRUE, charr_test_leaf("ci_count_fixed")(subject, pattern)),
    warning
  )
  expect_identical(expected, rep(NA_integer_, 3))
  expect_identical(actual, expected)
  expect_fixed_input_unmaterialized(subject)
  expect_fixed_input_unmaterialized(pattern)
})

test_that("zero recycling avoids fixed count input readers", {
  bytes_value <- fixed_marked_string(c(0xff, 0xfe), "bytes")
  subject <- charport::as_charvec(bytes_value)
  pattern <- charport::as_charvec(character())

  expected <- expect_no_warning(
    with_test_backend(FALSE, charr_test_leaf("ci_count_fixed")(bytes_value, character()))
  )
  actual <- expect_no_warning(
    with_test_backend(TRUE, charr_test_leaf("ci_count_fixed")(subject, pattern))
  )

  expect_identical(expected, integer())
  expect_identical(actual, expected)
  expect_fixed_input_unmaterialized(subject)
  expect_fixed_input_unmaterialized(pattern)
})

test_that("fixed counting preserves bytes error order", {
  bytes_value <- fixed_marked_string(c(0xff, 0xfe), "bytes")
  subject <- charport::as_charvec(bytes_value)
  pattern <- charport::as_charvec("a")

  expect_error(
    with_test_backend(FALSE, charr_test_leaf("ci_count_fixed")(bytes_value, "a")),
    "bytes encoding"
  )
  expect_error(
    with_test_backend(TRUE, charr_test_leaf("ci_count_fixed")(subject, pattern)),
    "bytes encoding"
  )
  expect_fixed_input_unmaterialized(subject)
  expect_fixed_input_unmaterialized(pattern)

  subject <- charport::as_charvec("abc")
  pattern <- charport::as_charvec(bytes_value)
  expect_error(
    with_test_backend(FALSE, charr_test_leaf("ci_count_fixed")("abc", bytes_value)),
    "bytes encoding"
  )
  expect_error(
    with_test_backend(TRUE, charr_test_leaf("ci_count_fixed")(subject, pattern)),
    "bytes encoding"
  )
  expect_fixed_input_unmaterialized(subject)
  expect_fixed_input_unmaterialized(pattern)

  subject <- charport::as_charvec(bytes_value)
  pattern <- charport::as_charvec("")
  reference_warnings <- 0L
  actual_warnings <- 0L

  expect_error(
    withCallingHandlers(
      with_test_backend(FALSE, charr_test_leaf("ci_count_fixed")(bytes_value, "")),
      warning = function(cnd) {
        reference_warnings <<- reference_warnings + 1L
        invokeRestart("muffleWarning")
      }
    ),
    "bytes encoding"
  )
  expect_error(
    withCallingHandlers(
      with_test_backend(TRUE, charr_test_leaf("ci_count_fixed")(subject, pattern)),
      warning = function(cnd) {
        actual_warnings <<- actual_warnings + 1L
        invokeRestart("muffleWarning")
      }
    ),
    "bytes encoding"
  )
  expect_identical(reference_warnings, 0L)
  expect_identical(actual_warnings, reference_warnings)
  expect_fixed_input_unmaterialized(subject)
  expect_fixed_input_unmaterialized(pattern)
})

test_that("fixed counting passes malformed declared UTF-8 through", {
  value <- fixed_marked_string(c(0x61, 0xff, 0x62, 0xff), "UTF-8")
  pattern_value <- fixed_marked_string(0xff, "UTF-8")
  subject <- charport::as_charvec(value)
  pattern <- charport::as_charvec(pattern_value)
  expected <- with_test_backend(
    FALSE,
    charr_test_leaf("ci_count_fixed")(value, pattern_value)
  )

  expect_identical(expected, 2L)
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_count_fixed")(subject, pattern)),
    expected
  )
  expect_fixed_input_unmaterialized(subject)
  expect_fixed_input_unmaterialized(pattern)
})
