# charr-owned tests for Reader-backed fixed position operations. These are not
# imported from stringr.

position_marked_string <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_position_input_unmaterialized <- function(x) {
  expect_altrep_unmaterialized(x)
}

test_that("fixed starts and ends match stringi on unmaterialized inputs", {
  latin1 <- position_marked_string(c(0x63, 0x61, 0x66, 0xe9), "latin1")
  bom <- enc2utf8("\ufeffalpha")
  values <- c("alpha", "caf\u00e9", "\U0001f642x", "", NA_character_, latin1, bom)
  starts <- c("al", "caf", "\U0001f642", "x", "x", "caf", "al")
  ends <- c("ha", "\u00e9", "x", "x", "x", "\u00e9", "ha")
  subject <- charport::as_charvec(values)
  start_pattern <- charport::as_charvec(starts)
  end_pattern <- charport::as_charvec(ends)
  fixed_start <- fixed(start_pattern)
  fixed_end <- fixed(end_pattern)

  expected_start <- with_test_backend(FALSE, str_starts(values, fixed(starts)))
  expected_end <- with_test_backend(FALSE, str_ends(values, fixed(ends)))
  expect_identical(
    expected_start,
    c(TRUE, TRUE, TRUE, FALSE, NA, TRUE, TRUE)
  )
  expect_identical(expected_end, expected_start)
  expect_identical(
    with_test_backend(TRUE, str_starts(subject, fixed_start)),
    expected_start
  )
  expect_identical(
    with_test_backend(TRUE, str_ends(subject, fixed_end)),
    expected_end
  )
  expect_position_input_unmaterialized(subject)
  expect_position_input_unmaterialized(start_pattern)
  expect_position_input_unmaterialized(end_pattern)
  expect_position_input_unmaterialized(fixed_start)
  expect_position_input_unmaterialized(fixed_end)
})

test_that("fixed starts and ends preserve code-point offsets and aliases", {
  values <- c("same", "x", "\u00e9")
  shared <- charport::as_charvec(values)
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_startswith_fixed")(shared, shared)),
    with_test_backend(FALSE, charr_test_leaf("ci_startswith_fixed")(values, values))
  )
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_endswith_fixed")(shared, shared)),
    with_test_backend(FALSE, charr_test_leaf("ci_endswith_fixed")(values, values))
  )
  expect_position_input_unmaterialized(shared)

  value <- "a\u00e9\U0001f600bc\u00e9"
  start_values <- c("a", "\u00e9", "\u00e9", "b", "z", "a")
  from <- c(1L, -1L, 2L, -2L, 99L, 0L)
  subject <- charport::as_charvec(value)
  pattern <- charport::as_charvec(start_values)
  expected <- with_test_backend(
    FALSE,
    charr_test_leaf("ci_startswith_fixed")(value, start_values, from = from)
  )
  expect_identical(expected, c(TRUE, TRUE, TRUE, FALSE, FALSE, TRUE))
  expect_identical(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_startswith_fixed")(subject, pattern, from = from)
    ),
    expected
  )
  expect_position_input_unmaterialized(subject)
  expect_position_input_unmaterialized(pattern)

  end_values <- c("\u00e9", "a", "\u00e9", "c", "\u00e9", "z")
  to <- c(-1L, 1L, 2L, -2L, 99L, 0L)
  pattern <- charport::as_charvec(end_values)
  expected <- with_test_backend(
    FALSE,
    charr_test_leaf("ci_endswith_fixed")(value, end_values, to = to)
  )
  expect_identical(expected, c(TRUE, TRUE, TRUE, TRUE, TRUE, FALSE))
  expect_identical(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_endswith_fixed")(subject, pattern, to = to)
    ),
    expected
  )
  expect_position_input_unmaterialized(subject)
  expect_position_input_unmaterialized(pattern)

  pattern <- charport::as_charvec("z")
  expect_identical(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_startswith_fixed")(subject, pattern, negate = TRUE)
    ),
    with_test_backend(
      FALSE,
      charr_test_leaf("ci_startswith_fixed")(value, "z", negate = TRUE)
    )
  )
  expect_identical(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_endswith_fixed")(subject, pattern, negate = TRUE)
    ),
    with_test_backend(
      FALSE,
      charr_test_leaf("ci_endswith_fixed")(value, "z", negate = TRUE)
    )
  )
  expect_position_input_unmaterialized(subject)
  expect_position_input_unmaterialized(pattern)
})

test_that("fixed starts preserve native recycling warnings", {
  values <- c("a", "ba", "ca")
  patterns <- c("a", "b")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  warning <- "^longer object length is not a multiple of shorter object length$"
  expected <- NULL
  actual <- NULL

  expect_warning(
    expected <- with_test_backend(
      FALSE,
      charr_test_leaf("ci_startswith_fixed")(values, patterns)
    ),
    warning
  )
  expect_warning(
    actual <- with_test_backend(
      TRUE,
      charr_test_leaf("ci_startswith_fixed")(subject, pattern)
    ),
    warning
  )
  expect_identical(actual, expected)
  expect_position_input_unmaterialized(subject)
  expect_position_input_unmaterialized(pattern)
})

test_that("fixed locate first preserves code-point positions", {
  values <- c("\u00e9a\u00e9", "\U0001f642x\U0001f642", "u\u0308u\u0308", "", NA_character_)
  patterns <- c("\u00e9", "\U0001f642", "u\u0308", "x", "x")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  fixed_pattern <- fixed(pattern)

  expected_first <- with_test_backend(
    FALSE,
    str_locate(values, fixed(patterns))
  )
  actual_first <- with_test_backend(
    TRUE,
    str_locate(subject, fixed_pattern)
  )
  expect_identical(actual_first, expected_first)
  expect_identical(colnames(actual_first), c("start", "end"))

  expected_first <- with_test_backend(
    FALSE,
    charr_test_leaf("ci_locate_first_fixed")(values, patterns, get_length = TRUE)
  )
  actual_first <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_locate_first_fixed")(subject, pattern, get_length = TRUE)
  )
  expect_identical(actual_first, expected_first)
  expect_identical(colnames(actual_first), c("start", "length"))

  expect_position_input_unmaterialized(subject)
  expect_position_input_unmaterialized(pattern)
  expect_position_input_unmaterialized(fixed_pattern)

  shared_values <- c("same", "x", "\u00e9")
  shared <- charport::as_charvec(shared_values)
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_locate_first_fixed")(shared, shared)),
    with_test_backend(
      FALSE,
      charr_test_leaf("ci_locate_first_fixed")(shared_values, shared_values)
    )
  )
  expect_position_input_unmaterialized(shared)
})

test_that("fixed locate all preserves overlap and no-match shapes", {
  values <- c(
    "\u00e9\u00e9\u00e9", "\U0001f642x\U0001f642", "u\u0308u\u0308",
    "none", "", NA_character_
  )
  patterns <- c("\u00e9\u00e9", "\U0001f642", "u\u0308", "x", "x", "x")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)

  expected <- with_test_backend(
    FALSE,
    charr_test_leaf("ci_locate_all_fixed")(values, patterns, overlap = TRUE)
  )
  actual <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_locate_all_fixed")(subject, pattern, overlap = TRUE)
  )
  expect_identical(actual, expected)
  expect_identical(colnames(actual[[1]]), c("start", "end"))

  expected <- with_test_backend(
    FALSE,
    charr_test_leaf("ci_locate_all_fixed")(
      values, patterns, overlap = TRUE,
      omit_no_match = TRUE, get_length = TRUE
    )
  )
  actual <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_locate_all_fixed")(
      subject, pattern, overlap = TRUE,
      omit_no_match = TRUE, get_length = TRUE
    )
  )
  expect_identical(actual, expected)
  expect_identical(actual[[4]], cbind(start = integer(), length = integer()))
  expect_identical(actual[[5]], cbind(start = integer(), length = integer()))
  expect_identical(
    actual[[6]],
    cbind(start = NA_integer_, length = NA_integer_)
  )
  expect_position_input_unmaterialized(subject)
  expect_position_input_unmaterialized(pattern)

  shared_values <- c("same", "x", "\u00e9")
  shared <- charport::as_charvec(shared_values)
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_locate_all_fixed")(shared, shared)),
    with_test_backend(
      FALSE,
      charr_test_leaf("ci_locate_all_fixed")(shared_values, shared_values)
    )
  )
  expect_position_input_unmaterialized(shared)
})

test_that("fixed locate preserves recycling and lenient UTF-8 indexing", {
  values <- c("a", "ba", "ca")
  patterns <- c("a", "b")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  warning <- "^longer object length is not a multiple of shorter object length$"
  expected <- NULL
  actual <- NULL

  expect_warning(
    expected <- with_test_backend(
      FALSE,
      charr_test_leaf("ci_locate_first_fixed")(values, patterns)
    ),
    warning
  )
  expect_warning(
    actual <- with_test_backend(
      TRUE,
      charr_test_leaf("ci_locate_first_fixed")(subject, pattern)
    ),
    warning
  )
  expect_identical(actual, expected)
  expect_position_input_unmaterialized(subject)
  expect_position_input_unmaterialized(pattern)

  value <- position_marked_string(c(0x61, 0xc3, 0x28, 0x62), "UTF-8")
  subject <- charport::as_charvec(value)
  pattern <- charport::as_charvec("b")
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_locate_first_fixed")(subject, pattern)),
    with_test_backend(FALSE, charr_test_leaf("ci_locate_first_fixed")(value, "b"))
  )
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_locate_all_fixed")(subject, pattern)),
    with_test_backend(FALSE, charr_test_leaf("ci_locate_all_fixed")(value, "b"))
  )
  expect_position_input_unmaterialized(subject)
  expect_position_input_unmaterialized(pattern)
})

test_that("empty fixed position patterns warn and preserve NA shapes", {
  values <- c("abc", "", NA_character_)
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec("")
  warning <- "^empty search patterns are not supported$"
  expected <- NULL
  actual <- NULL

  expect_warning(
    expected <- with_test_backend(FALSE, charr_test_leaf("ci_startswith_fixed")(values, "")),
    warning
  )
  expect_warning(
    actual <- with_test_backend(TRUE, charr_test_leaf("ci_startswith_fixed")(subject, pattern)),
    warning
  )
  expect_identical(actual, expected)

  expect_warning(
    expected <- with_test_backend(
      FALSE,
      charr_test_leaf("ci_locate_first_fixed")(values, "")
    ),
    warning
  )
  expect_warning(
    actual <- with_test_backend(
      TRUE,
      charr_test_leaf("ci_locate_first_fixed")(subject, pattern)
    ),
    warning
  )
  expect_identical(actual, expected)

  expect_warning(
    expected <- with_test_backend(
      FALSE,
      charr_test_leaf("ci_locate_all_fixed")(values, "", omit_no_match = TRUE)
    ),
    warning
  )
  expect_warning(
    actual <- with_test_backend(
      TRUE,
      charr_test_leaf("ci_locate_all_fixed")(
        subject, pattern, omit_no_match = TRUE
      )
    ),
    warning
  )
  expect_identical(actual, expected)
  expect_position_input_unmaterialized(subject)
  expect_position_input_unmaterialized(pattern)
})

test_that("zero recycling avoids fixed position input readers", {
  bytes_value <- position_marked_string(c(0xff, 0xfe), "bytes")
  subject <- charport::as_charvec(bytes_value)
  pattern <- charport::as_charvec("a")
  empty_pattern <- charport::as_charvec(character())

  expect_identical(
    expect_no_warning(
      with_test_backend(
        TRUE,
        charr_test_leaf("ci_startswith_fixed")(subject, pattern, from = integer())
      )
    ),
    logical()
  )
  expect_identical(
    expect_no_warning(
      with_test_backend(TRUE, charr_test_leaf("ci_endswith_fixed")(subject, empty_pattern))
    ),
    logical()
  )
  expect_identical(
    expect_no_warning(
      with_test_backend(TRUE, charr_test_leaf("ci_locate_first_fixed")(subject, empty_pattern))
    ),
    cbind(start = integer(), end = integer())
  )
  expect_identical(
    expect_no_warning(
      with_test_backend(TRUE, charr_test_leaf("ci_locate_all_fixed")(subject, empty_pattern))
    ),
    list()
  )
  expect_position_input_unmaterialized(subject)
  expect_position_input_unmaterialized(pattern)
  expect_position_input_unmaterialized(empty_pattern)
})

test_that("fixed position operations preserve bytes error order", {
  bytes_value <- position_marked_string(c(0xff, 0xfe), "bytes")
  subject <- charport::as_charvec(bytes_value)
  pattern <- charport::as_charvec("a")

  expect_error(
    with_test_backend(TRUE, charr_test_leaf("ci_startswith_fixed")(subject, pattern)),
    "bytes encoding"
  )
  expect_error(
    with_test_backend(TRUE, charr_test_leaf("ci_endswith_fixed")(subject, pattern)),
    "bytes encoding"
  )
  expect_error(
    with_test_backend(TRUE, charr_test_leaf("ci_locate_first_fixed")(subject, pattern)),
    "bytes encoding"
  )
  expect_error(
    with_test_backend(TRUE, charr_test_leaf("ci_locate_all_fixed")(subject, pattern)),
    "bytes encoding"
  )
  expect_position_input_unmaterialized(subject)
  expect_position_input_unmaterialized(pattern)

  subject <- charport::as_charvec("abc")
  pattern <- charport::as_charvec(bytes_value)
  expect_error(
    with_test_backend(TRUE, charr_test_leaf("ci_startswith_fixed")(subject, pattern)),
    "bytes encoding"
  )
  expect_error(
    with_test_backend(TRUE, charr_test_leaf("ci_locate_all_fixed")(subject, pattern)),
    "bytes encoding"
  )
  expect_position_input_unmaterialized(subject)
  expect_position_input_unmaterialized(pattern)

  subject <- charport::as_charvec(bytes_value)
  pattern <- charport::as_charvec("")
  warning_count <- 0L
  expect_error(
    withCallingHandlers(
      with_test_backend(TRUE, charr_test_leaf("ci_startswith_fixed")(subject, pattern)),
      warning = function(cnd) {
        warning_count <<- warning_count + 1L
        invokeRestart("muffleWarning")
      }
    ),
    "bytes encoding"
  )
  expect_identical(warning_count, 0L)

  warning_count <- 0L
  expect_error(
    withCallingHandlers(
      with_test_backend(TRUE, charr_test_leaf("ci_locate_all_fixed")(subject, pattern)),
      warning = function(cnd) {
        warning_count <<- warning_count + 1L
        invokeRestart("muffleWarning")
      }
    ),
    "bytes encoding"
  )
  expect_identical(warning_count, 0L)
  expect_position_input_unmaterialized(subject)
  expect_position_input_unmaterialized(pattern)
})
