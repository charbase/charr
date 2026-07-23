# Charr-owned tests for Reader-backed regular-expression replacement.
# These are not imported from stringr.

regex_replace_marked_string <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_regex_replace_unmaterialized <- function(x) {
  expect_true(charport::is_charvec(x))
  expect_false(charport::charport_info(x)$is_materialized)
}

regex_replace_condition_events <- function(expr) {
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

test_that("regex replacement keeps the copied vectorized traversal", {
  values <- c("abcabc", "a1b2", "\u00e9\U0001f600x", NA_character_)
  patterns <- c("(b)(c)", "[0-9]", "(.)", "x")
  replacements <- c("<$2$1>", "#", "[$0:$1]", "!")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  replacement <- charport::as_charvec(replacements)

  expected_all <- stringi::stri_replace_all_regex(
    values, patterns, replacements
  )
  actual_all <- with_altrep(
    TRUE,
    charr:::ci_replace_all_regex(subject, pattern, replacement)
  )
  expect_identical(actual_all, expected_all)
  expect_regex_replace_unmaterialized(actual_all)

  expected_first <- stringi::stri_replace_first_regex(
    values, patterns, replacements
  )
  actual_first <- with_altrep(
    TRUE,
    charr:::ci_replace_first_regex(subject, pattern, replacement)
  )
  expect_identical(actual_first, expected_first)
  expect_regex_replace_unmaterialized(actual_first)

  chained <- with_altrep(
    TRUE,
    charr:::ci_count_regex(actual_all, charport::as_charvec("[#<]"))
  )
  expect_identical(
    chained,
    stringi::stri_count_regex(expected_all, "[#<]")
  )

  expect_regex_replace_unmaterialized(subject)
  expect_regex_replace_unmaterialized(pattern)
  expect_regex_replace_unmaterialized(replacement)
})

test_that("regex replacement accepts exact aliases for all inputs", {
  values <- c("a", "b", "c")
  shared <- charport::as_charvec(values)

  actual <- with_altrep(
    TRUE,
    charr:::ci_replace_all_regex(shared, shared, shared)
  )
  expect_identical(
    actual,
    stringi::stri_replace_all_regex(values, values, values)
  )
  expect_regex_replace_unmaterialized(actual)
  expect_regex_replace_unmaterialized(shared)
})

test_that("regex replacement preserves captures and missing values", {
  values <- c("ababab", "xyz", "", "aa", NA_character_)
  subject <- charport::as_charvec(values)

  expect_identical(
    with_altrep(
      TRUE,
      charr:::ci_replace_all_regex(subject, "(a)", "[$0:$1]")
    ),
    stringi::stri_replace_all_regex(values, "(a)", "[$0:$1]")
  )
  expect_identical(
    with_altrep(TRUE, charr:::ci_replace_first_regex(subject, "a", "Z")),
    stringi::stri_replace_first_regex(values, "a", "Z")
  )
  expect_identical(
    with_altrep(
      TRUE,
      charr:::ci_replace_all_regex(subject, "a", NA_character_)
    ),
    stringi::stri_replace_all_regex(values, "a", NA_character_)
  )
  expect_identical(
    with_altrep(TRUE, charr:::ci_replace_all_regex(subject, ".*", "x")),
    stringi::stri_replace_all_regex(values, ".*", "x")
  )

  expected_events <- regex_replace_condition_events(
    stringi::stri_replace_all_regex("abc", "(a)", "$9")
  )
  actual_events <- regex_replace_condition_events(
    with_altrep(
      TRUE,
      charr:::ci_replace_all_regex(
        charport::as_charvec("abc"),
        charport::as_charvec("(a)"),
        charport::as_charvec("$9")
      )
    )
  )
  expect_identical(actual_events, expected_events)
  expect_match(actual_events, "error:")
  expect_regex_replace_unmaterialized(subject)
})

test_that("non-vectorized regex replacement applies patterns in sequence", {
  values <- c("a1b2", "cccc", NA_character_)
  patterns <- c("[0-9]", "c")
  replacements <- c("_", "C")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  replacement <- charport::as_charvec(replacements)

  actual <- with_altrep(
    TRUE,
    charr:::ci_replace_all_regex(
      subject, pattern, replacement, vectorize_all = FALSE
    )
  )
  expect_identical(
    actual,
    stringi::stri_replace_all_regex(
      values, patterns, replacements, vectorize_all = FALSE
    )
  )
  expect_regex_replace_unmaterialized(actual)

  missing_pattern <- charport::as_charvec(c(NA_character_, "a"))
  missing_actual <- with_altrep(
    TRUE,
    charr:::ci_replace_all_regex(
      subject, missing_pattern, replacement, vectorize_all = FALSE
    )
  )
  expect_identical(
    missing_actual,
    stringi::stri_replace_all_regex(
      values, c(NA_character_, "a"), replacements,
      vectorize_all = FALSE
    )
  )
  expect_regex_replace_unmaterialized(missing_actual)
  expect_regex_replace_unmaterialized(subject)
  expect_regex_replace_unmaterialized(pattern)
  expect_regex_replace_unmaterialized(replacement)
  expect_regex_replace_unmaterialized(missing_pattern)
})

test_that("regex compilation stays lazy on replacement paths", {
  missing <- charport::as_charvec(NA_character_)
  present <- charport::as_charvec("a")
  invalid <- charport::as_charvec("[")
  replacement <- charport::as_charvec("x")

  expect_identical(
    with_altrep(
      TRUE,
      charr:::ci_replace_all_regex(missing, invalid, replacement)
    ),
    stringi::stri_replace_all_regex(NA_character_, "[", "x")
  )
  expect_identical(
    regex_replace_condition_events(
      with_altrep(
        TRUE,
        charr:::ci_replace_all_regex(present, invalid, replacement)
      )
    ),
    regex_replace_condition_events(
      stringi::stri_replace_all_regex("a", "[", "x")
    )
  )

  sequential_pattern <- charport::as_charvec(c("a", "["))
  sequential_replacement <- charport::as_charvec(c("A", "x"))
  expect_identical(
    regex_replace_condition_events(
      with_altrep(
        TRUE,
        charr:::ci_replace_all_regex(
          present, sequential_pattern, sequential_replacement,
          vectorize_all = FALSE
        )
      )
    ),
    regex_replace_condition_events(
      stringi::stri_replace_all_regex(
        "a", c("a", "["), c("A", "x"), vectorize_all = FALSE
      )
    )
  )

  expect_regex_replace_unmaterialized(missing)
  expect_regex_replace_unmaterialized(present)
  expect_regex_replace_unmaterialized(invalid)
  expect_regex_replace_unmaterialized(replacement)
  expect_regex_replace_unmaterialized(sequential_pattern)
  expect_regex_replace_unmaterialized(sequential_replacement)
})

test_that("regex replacement preserves warning and error order", {
  values <- c("a", "b", "c")
  patterns <- c("a", "b")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  replacement <- charport::as_charvec("x")
  opts_regex <- list(bogus = TRUE)

  expect_identical(
    regex_replace_condition_events(
      with_altrep(
        TRUE,
        charr:::ci_replace_all_regex(
          subject, pattern, replacement, opts_regex = opts_regex
        )
      )
    ),
    regex_replace_condition_events(
      stringi::stri_replace_all_regex(
        values, patterns, "x", opts_regex = opts_regex
      )
    )
  )

  expect_identical(
    regex_replace_condition_events(
      with_altrep(
        TRUE,
        charr:::ci_replace_all_regex(
          charport::as_charvec("a"),
          charport::as_charvec(c("a", "b")),
          charport::as_charvec(c("x", "y", "z")),
          vectorize_all = FALSE,
          opts_regex = opts_regex
        )
      )
    ),
    regex_replace_condition_events(
      stringi::stri_replace_all_regex(
        "a", c("a", "b"), c("x", "y", "z"),
        vectorize_all = FALSE,
        opts_regex = opts_regex
      )
    )
  )

  expect_identical(
    regex_replace_condition_events(
      with_altrep(
        TRUE,
        charr:::ci_replace_all_regex(
          charport::as_charvec("a"),
          charport::as_charvec("a"),
          charport::as_charvec("x"),
          vectorize_all = FALSE,
          opts_regex = opts_regex
        )
      )
    ),
    regex_replace_condition_events(
      stringi::stri_replace_all_regex(
        "a", "a", "x", vectorize_all = FALSE,
        opts_regex = opts_regex
      )
    )
  )

  expect_regex_replace_unmaterialized(subject)
  expect_regex_replace_unmaterialized(pattern)
  expect_regex_replace_unmaterialized(replacement)
})

test_that("non-vectorized empty patterns preserve warning multiplicity", {
  cases <- list(
    list(pattern = "", replacement = "x"),
    list(pattern = c("", "a"), replacement = c("x", "y")),
    list(pattern = c("", ""), replacement = c("x", "y"))
  )

  for (case in cases) {
    subject <- charport::as_charvec("a")
    pattern <- charport::as_charvec(case$pattern)
    replacement <- charport::as_charvec(case$replacement)
    actual_events <- regex_replace_condition_events(
      with_altrep(
        TRUE,
        charr:::ci_replace_all_regex(
          subject, pattern, replacement, vectorize_all = FALSE
        )
      )
    )
    expected_events <- regex_replace_condition_events(
      stringi::stri_replace_all_regex(
        "a", case$pattern, case$replacement, vectorize_all = FALSE
      )
    )
    expect_identical(actual_events, expected_events)
    expect_regex_replace_unmaterialized(subject)
    expect_regex_replace_unmaterialized(pattern)
    expect_regex_replace_unmaterialized(replacement)
  }
})

test_that("regex replacement preserves marks and exact UTF-8 bytes", {
  latin1 <- regex_replace_marked_string(
    c(0x63, 0x61, 0x66, 0xe9), "latin1"
  )
  malformed <- regex_replace_marked_string(c(0x61, 0xff, 0x62), "UTF-8")
  values <- c(latin1, "\ufeffabc", malformed, "abc")
  patterns <- c("\u00e9", "^a", "\ufffd", "b")
  replacements <- c("\u00c9", "A", "X", "B")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  replacement <- charport::as_charvec(replacements)

  actual <- with_altrep(
    TRUE,
    charr:::ci_replace_all_regex(subject, pattern, replacement)
  )
  expected <- stringi::stri_replace_all_regex(
    values, patterns, replacements
  )
  expect_identical(actual, expected)
  expect_regex_replace_unmaterialized(actual)
  expect_identical(
    lapply(as.character(actual), charToRaw),
    lapply(expected, charToRaw)
  )
  expect_identical(Encoding(actual), Encoding(expected))

  expect_regex_replace_unmaterialized(subject)
  expect_regex_replace_unmaterialized(pattern)
  expect_regex_replace_unmaterialized(replacement)
})

test_that("regex replacement validates bytes inputs in construction order", {
  bytes <- regex_replace_marked_string(c(0xff, 0xfe), "bytes")
  cases <- list(
    list(str = bytes, pattern = bytes, replacement = bytes),
    list(str = "a", pattern = bytes, replacement = bytes),
    list(str = "a", pattern = "z", replacement = bytes)
  )

  for (case in cases) {
    subject <- charport::as_charvec(case$str)
    pattern <- charport::as_charvec(case$pattern)
    replacement <- charport::as_charvec(case$replacement)
    expect_identical(
      regex_replace_condition_events(
        with_altrep(
          TRUE,
          charr:::ci_replace_all_regex(subject, pattern, replacement)
        )
      ),
      regex_replace_condition_events(
        stringi::stri_replace_all_regex(
          case$str, case$pattern, case$replacement
        )
      )
    )
    expect_regex_replace_unmaterialized(subject)
    expect_regex_replace_unmaterialized(pattern)
    expect_regex_replace_unmaterialized(replacement)
  }
})

test_that("empty sequential subjects do not inspect later arguments", {
  bytes <- regex_replace_marked_string(c(0xff, 0xfe), "bytes")
  subject <- charport::as_charvec(character())
  pattern <- charport::as_charvec(bytes)
  replacement <- charport::as_charvec(bytes)
  opts_regex <- list(bogus = TRUE)

  expected_events <- regex_replace_condition_events(
    expected <- stringi::stri_replace_all_regex(
      character(), bytes, bytes, vectorize_all = FALSE,
      opts_regex = opts_regex
    )
  )
  actual_events <- regex_replace_condition_events(
    actual <- with_altrep(
      TRUE,
      charr:::ci_replace_all_regex(
        subject, pattern, replacement, vectorize_all = FALSE,
        opts_regex = opts_regex
      )
    )
  )
  expect_identical(actual_events, expected_events)
  expect_identical(actual, expected)
  expect_regex_replace_unmaterialized(actual)
  expect_regex_replace_unmaterialized(subject)
  expect_regex_replace_unmaterialized(pattern)
  expect_regex_replace_unmaterialized(replacement)
})
