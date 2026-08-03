# Charr-owned tests for Reader-backed encoding conversion.
# These are not imported from stringr.

encoding_conversion_marked_string <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_encoding_conversion_unmaterialized <- function(x) {
  expect_altrep_unmaterialized(x)
}

encoding_conversion_events <- function(expr) {
  events <- character()
  value <- tryCatch(
    withCallingHandlers(
      force(expr),
      warning = function(condition) {
        events <<- c(events, paste0("warning:", conditionMessage(condition)))
        invokeRestart("muffleWarning")
      }
    ),
    error = function(condition) {
      events <<- c(events, paste0("error:", conditionMessage(condition)))
      NULL
    }
  )
  list(events = events, value = value)
}

encoding_conversion_normalize_events <- function(events) {
  gsub("stri_", "ci_", events, fixed = TRUE)
}

expect_encoding_conversion_events <- function(actual, expected) {
  expect_identical(
    actual$events,
    encoding_conversion_normalize_events(expected$events)
  )
  expect_identical(actual$value, expected$value)
}

test_that("marked encode preserves converters, marks, and raw output", {
  malformed <- encoding_conversion_marked_string(
    c(0x61, 0xc3, 0x28), "UTF-8"
  )
  bom <- encoding_conversion_marked_string(
    c(0xef, 0xbb, 0xbf, 0x61), "UTF-8"
  )
  values <- c("abc", "\u00e9", "\U0001f642", malformed, bom, "", NA_character_)
  subject <- charport::as_charvec(values)

  for (to in c("UTF-8", "ISO-8859-1", "UTF-7")) {
    actual <- encoding_conversion_events(
      with_test_backend(TRUE, charr_test_leaf("ci_encode")(subject, NULL, to, FALSE))
    )
    expected <- encoding_conversion_events(
      stringi::stri_encode(values, NULL, to, FALSE)
    )
    expect_encoding_conversion_unmaterialized(actual$value)
    expect_encoding_conversion_events(actual, expected)
    expect_identical(Encoding(actual$value), Encoding(expected$value))
  }

  actual_raw <- encoding_conversion_events(
    with_test_backend(
      TRUE, charr_test_leaf("ci_encode")(subject, NULL, "UTF-16LE", TRUE)
    )
  )
  expected_raw <- encoding_conversion_events(
    stringi::stri_encode(values, NULL, "UTF-16LE", TRUE)
  )
  expect_encoding_conversion_events(actual_raw, expected_raw)

  bytes <- encoding_conversion_marked_string(c(0x61, 0xff), "bytes")
  bytes_subject <- charport::as_charvec(bytes)
  actual_bytes <- encoding_conversion_events(
    with_test_backend(
      TRUE, charr_test_leaf("ci_encode")(bytes_subject, NULL, "UTF-8", FALSE)
    )
  )
  expected_bytes <- encoding_conversion_events(
    stringi::stri_encode(bytes, NULL, "UTF-8", FALSE)
  )
  expect_encoding_conversion_events(actual_bytes, expected_bytes)
  expect_encoding_conversion_unmaterialized(subject)
  expect_encoding_conversion_unmaterialized(bytes_subject)
})
test_that("explicit encode preserves raw, list-raw, and character byte input", {
  input <- list(
    charToRaw("abc"),
    as.raw(c(0xc3, 0xa9)),
    as.raw(c(0xf0, 0x9f, 0x99, 0x82)),
    raw(),
    NULL
  )
  actual <- with_test_backend(
    TRUE, charr_test_leaf("ci_encode")(input, "UTF-8", "UTF-8", FALSE)
  )
  expect_encoding_conversion_unmaterialized(actual)
  expect_identical(
    actual,
    stringi::stri_encode(input, "UTF-8", "UTF-8", FALSE)
  )

  actual_raw <- with_test_backend(
    TRUE, charr_test_leaf("ci_encode")(input, "UTF-8", "UTF-16LE", TRUE)
  )
  expect_identical(
    actual_raw,
    stringi::stri_encode(input, "UTF-8", "UTF-16LE", TRUE)
  )

  single_raw <- as.raw(c(0x61, 0xc3, 0xa9))
  single <- with_test_backend(
    TRUE, charr_test_leaf("ci_encode")(single_raw, "UTF-8", "UTF-8", FALSE)
  )
  expect_encoding_conversion_unmaterialized(single)
  expect_identical(
    single,
    stringi::stri_encode(single_raw, "UTF-8", "UTF-8", FALSE)
  )

  marked_bytes <- encoding_conversion_marked_string(c(0x61, 0xff), "bytes")
  marked_subject <- charport::as_charvec(marked_bytes)
  explicit <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_encode")(marked_subject, "ISO-8859-1", "UTF-8", FALSE)
  )
  expect_encoding_conversion_unmaterialized(explicit)
  expect_identical(
    explicit,
    stringi::stri_encode(marked_bytes, "ISO-8859-1", "UTF-8", FALSE)
  )
  expect_encoding_conversion_unmaterialized(marked_subject)
})

test_that("converter warnings retain count and source-before-target order", {
  input <- list(
    as.raw(c(0x61, 0xff)),
    charToRaw("\U0001f642")
  )
  actual <- encoding_conversion_events(
    with_test_backend(
      TRUE, charr_test_leaf("ci_encode")(input, "UTF-8", "ISO-8859-1", FALSE)
    )
  )
  expected <- encoding_conversion_events(
    stringi::stri_encode(input, "UTF-8", "ISO-8859-1", FALSE)
  )
  expect_encoding_conversion_unmaterialized(actual$value)
  expect_encoding_conversion_events(actual, expected)
  expect_length(actual$events, 3L)
  expect_match(actual$events[[1L]], "input data", fixed = TRUE)
  expect_true(all(grepl("Unicode code point", actual$events[-1L], fixed = TRUE)))

  warning_then_nul <- as.raw(c(0xff, 0x41))
  actual_order <- encoding_conversion_events(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_encode")(
        warning_then_nul, "UTF-8", "UTF-16LE", FALSE
      )
    )
  )
  expected_order <- encoding_conversion_events(
    stringi::stri_encode(
      warning_then_nul, "UTF-8", "UTF-16LE", FALSE
    )
  )
  if (identical(selected_test_backend, "base")) {
    expect_identical(sub(":.*", "", actual_order$events), "error")
    expect_match(
      actual_order$events[[1L]], "embedded nul in string", fixed = TRUE
    )
  } else {
    expect_identical(
      sub(":.*", "", actual_order$events),
      sub(":.*", "", expected_order$events)
    )
    expect_identical(actual_order$events[[1L]], expected_order$events[[1L]])
    expect_match(
      actual_order$events[[2L]], "embedded nul in string", fixed = TRUE
    )
  }
})

test_that("converter callbacks release Reader and ICU state before warn=2", {
  subject <- charport::as_charvec("\U0001f642")
  old_warn <- getOption("warn")
  on.exit(options(warn = old_warn), add = TRUE)
  options(warn = 2)

  expect_error(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_encode")(subject, "UTF-8", "ISO-8859-1", FALSE)
    ),
    "Unicode code point",
    fixed = TRUE
  )
  expect_identical(
    with_test_backend(
      TRUE, charr_test_leaf("ci_encode")(subject, "UTF-8", "UTF-8", FALSE)
    ),
    "\U0001f642"
  )
  expect_encoding_conversion_unmaterialized(subject)
})

test_that("encoding conversion keeps each encode NUL policy", {
  marked <- encoding_conversion_events(
    with_test_backend(
      TRUE, charr_test_leaf("ci_encode")("A", NULL, "UTF-16LE", FALSE)
    )
  )
  expect_match(marked$events, "embedded nul in string", fixed = TRUE)

  explicit <- encoding_conversion_events(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_encode")(as.raw(0), "UTF-8", "UTF-8", FALSE)
    )
  )
  expect_match(explicit$events, "embedded nul in string", fixed = TRUE)

  expect_identical(
    with_test_backend(
      TRUE, charr_test_leaf("ci_encode")("A", NULL, "UTF-16LE", TRUE)
    ),
    stringi::stri_encode("A", NULL, "UTF-16LE", TRUE)
  )
  expect_identical(
    with_test_backend(
      TRUE, charr_test_leaf("ci_encode")(as.raw(0), "UTF-8", "UTF-8", TRUE)
    ),
    stringi::stri_encode(as.raw(0), "UTF-8", "UTF-8", TRUE)
  )
})

test_that("encode preserves all zero-length shapes", {
  empty <- charport::as_charvec(character())

  marked <- with_test_backend(
    TRUE, charr_test_leaf("ci_encode")(empty, NULL, "not-an-encoding", FALSE)
  )
  explicit <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_encode")(
      list(), "not-an-encoding", "also-not-an-encoding", FALSE
    )
  )

  for (output in list(marked, explicit)) {
    expect_encoding_conversion_unmaterialized(output)
    expect_identical(output, character())
  }
  expect_identical(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_encode")(
        list(), "not-an-encoding", "also-not-an-encoding", TRUE
      )
    ),
    list()
  )
  expect_identical(
    with_test_backend(
      TRUE, charr_test_leaf("ci_encode")(raw(), "UTF-8", "UTF-8", FALSE)
    ),
    ""
  )
  expect_identical(
    with_test_backend(
      TRUE, charr_test_leaf("ci_encode")(NULL, "UTF-8", "UTF-8", FALSE)
    ),
    NA_character_
  )
  expect_encoding_conversion_unmaterialized(empty)
})
