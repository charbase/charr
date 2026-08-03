# Charr-owned tests for Reader-backed wrapping. These are not imported from
# stringr.

wrap_backend_call <- function(on, string, ...) {
  with_test_backend(
    on,
    charr_test_leaf("ci_wrap")(
      string, ..., simplify = FALSE, normalize = FALSE
    )
  )
}

wrap_backend_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_wrap_backend_unmaterialized <- function(x) {
  expect_altrep_unmaterialized(x)
}

wrap_backend_events <- function(on, string, ...) {
  events <- character()
  tryCatch(
    withCallingHandlers(
      wrap_backend_call(on, string, ...),
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

wrap_backend_event_types <- function(events) {
  sub(":.*$", "", events)
}

wrap_backend_normalize_events <- function(events) {
  gsub("(stri|ci)_enc_toutf8", "enc_toutf8", events, perl = TRUE)
}

test_that("wrap preserves serial line breaking and list shape", {
  values <- c(
    "alpha beta gamma delta epsilon", "a/b/c/d", "\u65e5\u672c\u8a9e \u0e20\u0e32\u0e29\u0e32\u0e44\u0e17\u0e22",
    "\U0001f469\u200d\U0001f469\u200d\U0001f467\u200d\U0001f466 e\u0301 family", "", NA_character_
  )
  subject <- charport::as_charvec(values)
  prefix <- charport::as_charvec("\u03bb> ")
  initial <- charport::as_charvec("\u521d> ")

  for (cost_exponent in c(-1, 2)) {
    for (use_length in c(FALSE, TRUE)) {
      actual <- suppressWarnings(wrap_backend_call(
        TRUE, subject, width = 9L, cost_exponent = cost_exponent,
        indent = 2L, exdent = 1L, prefix = prefix, initial = initial,
        whitespace_only = FALSE, use_length = use_length, locale = "th_TH"
      ))
      expected <- suppressWarnings(wrap_backend_call(
        FALSE, values, width = 9L, cost_exponent = cost_exponent,
        indent = 2L, exdent = 1L, prefix = "\u03bb> ", initial = "\u521d> ",
        whitespace_only = FALSE, use_length = use_length, locale = "th_TH"
      ))

      expect_type(actual, "list")
      expect_length(actual, length(values))
      expect_altrep_unmaterialized_list(actual)
      expect_identical(actual, expected)
    }
  }

  expect_wrap_backend_unmaterialized(subject)
  expect_wrap_backend_unmaterialized(prefix)
  expect_wrap_backend_unmaterialized(initial)
})

test_that("wrap preserves whitespace-only, width-zero, and adornment rules", {
  values <- c("a/b", "one  two three", "wide \uff21 bb")
  subject <- charport::as_charvec(values)

  for (whitespace_only in c(FALSE, TRUE)) {
    actual <- wrap_backend_call(
      TRUE, subject, width = 0L, cost_exponent = -1,
      indent = 1L, exdent = 2L, prefix = "> ", initial = "* ",
      whitespace_only = whitespace_only, use_length = FALSE,
      locale = "en_US"
    )
    expected <- wrap_backend_call(
      FALSE, values, width = 0L, cost_exponent = -1,
      indent = 1L, exdent = 2L, prefix = "> ", initial = "* ",
      whitespace_only = whitespace_only, use_length = FALSE,
      locale = "en_US"
    )

    expect_altrep_charvec_list(actual)
    expect_identical(actual, expected)
  }

  expect_wrap_backend_unmaterialized(subject)
})

test_that("wrap reuses exact aliases and consumes its lazy line output", {
  value <- "\u03bb> alpha beta"
  shared <- charport::as_charvec(value)
  first <- wrap_backend_call(
    TRUE, shared, width = 8L, cost_exponent = -1,
    prefix = shared, initial = shared, whitespace_only = FALSE,
    use_length = TRUE
  )
  expected_first <- wrap_backend_call(
    FALSE, value, width = 8L, cost_exponent = -1,
    prefix = value, initial = value, whitespace_only = FALSE,
    use_length = TRUE
  )

  expect_length(first, 1L)
  expect_wrap_backend_unmaterialized(first[[1L]])
  expect_wrap_backend_unmaterialized(shared)

  second <- wrap_backend_call(
    TRUE, first[[1L]], width = 6L, cost_exponent = -1,
    whitespace_only = FALSE, use_length = TRUE
  )
  expected_second <- wrap_backend_call(
    FALSE, expected_first[[1L]], width = 6L, cost_exponent = -1,
    whitespace_only = FALSE, use_length = TRUE
  )

  expect_altrep_charvec_list(second)
  expect_wrap_backend_unmaterialized(first[[1L]])
  expect_identical(first, expected_first)
  expect_identical(second, expected_second)
})

test_that("wrap preserves encoding normalization and large staged output", {
  latin1 <- wrap_backend_marked(c(0x63, 0x61, 0x66, 0xe9), "latin1")
  values <- c(latin1, "\ufeffone two", "\u0e20\u0e32\u0e29\u0e32\u0e44\u0e17\u0e22", "")
  subject <- charport::as_charvec(values)
  actual <- wrap_backend_call(
    TRUE, subject, width = 4L, cost_exponent = -1,
    whitespace_only = FALSE, locale = "en_US"
  )
  expected <- wrap_backend_call(
    FALSE, values, width = 4L, cost_exponent = -1,
    whitespace_only = FALSE, locale = "en_US"
  )

  expect_altrep_unmaterialized_list(actual)
  expect_identical(actual, expected)
  expect_wrap_backend_unmaterialized(subject)

  large_value <- paste(rep("ab", 5000L), collapse = " ")
  large_subject <- charport::as_charvec(large_value)
  large_actual <- wrap_backend_call(
    TRUE, large_subject, width = 7L, cost_exponent = -1,
    whitespace_only = TRUE, use_length = TRUE
  )
  expect_wrap_backend_unmaterialized(large_actual[[1L]])
  expect_identical(
    large_actual,
    wrap_backend_call(
      FALSE, large_value, width = 7L, cost_exponent = -1,
      whitespace_only = TRUE, use_length = TRUE
    )
  )
  expect_wrap_backend_unmaterialized(large_subject)
})

test_that("wrap preserves missing, empty, bytes, and malformed behavior", {
  values <- c("", " ", NA_character_, "one two")
  subject <- charport::as_charvec(values)
  actual <- wrap_backend_call(TRUE, subject, width = 4L)
  expected <- wrap_backend_call(FALSE, values, width = 4L)

  expect_identical(actual, expected)
  expect_altrep_charvec_list(actual)
  expect_wrap_backend_unmaterialized(subject)

  empty <- charport::as_charvec(character())
  expect_identical(wrap_backend_call(TRUE, empty), list())
  expect_wrap_backend_unmaterialized(empty)

  bytes <- wrap_backend_marked(
    c(0x6f, 0x6e, 0x65, 0x20, 0xff), "bytes"
  )
  bytes_subject <- charport::as_charvec(bytes)
  expect_identical(
    wrap_backend_events(TRUE, bytes_subject),
    wrap_backend_events(FALSE, bytes)
  )
  expect_wrap_backend_unmaterialized(bytes_subject)

  malformed <- wrap_backend_marked(c(0x61, 0xc3, 0x28, 0x62), "UTF-8")
  malformed_subject <- charport::as_charvec(malformed)
  expect_identical(
    wrap_backend_normalize_events(
      wrap_backend_events(TRUE, malformed_subject, width = 2L)
    ),
    wrap_backend_normalize_events(
      wrap_backend_events(FALSE, malformed, width = 2L)
    )
  )
  expect_wrap_backend_unmaterialized(malformed_subject)
})

test_that("wrap preserves locale-warning and argument-error order", {
  newline <- "alpha\nbeta"
  subject <- charport::as_charvec(newline)
  actual <- wrap_backend_events(
    TRUE, subject, width = 4L, locale = "zz_ZZ"
  )
  expected <- wrap_backend_events(
    FALSE, newline, width = 4L, locale = "zz_ZZ"
  )

  expect_identical(wrap_backend_event_types(actual), c("warning", "error"))
  expect_identical(actual, expected)
  expect_wrap_backend_unmaterialized(subject)

  option_actual <- wrap_backend_events(
    TRUE, subject, indent = -1L, locale = "zz_ZZ"
  )
  option_expected <- wrap_backend_events(
    FALSE, newline, indent = -1L, locale = "zz_ZZ"
  )
  expect_identical(wrap_backend_event_types(option_actual), "error")
  expect_identical(option_actual, option_expected)
})
