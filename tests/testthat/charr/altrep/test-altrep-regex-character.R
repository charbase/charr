# Charr-owned tests for Reader-backed regex character results.
# These are not imported from stringr.

regex_char_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_regex_char_unmaterialized <- function(x) {
  expect_true(charport::is_charvec(x))
  expect_false(charport::charport_info(x)$is_materialized)
}

regex_char_events <- function(expr) {
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

test_that("regex extraction keeps inputs and fixed outputs unmaterialized", {
  latin1 <- regex_char_marked(c(0x63, 0x61, 0x66, 0xe9), "latin1")
  malformed <- regex_char_marked(c(0x61, 0xff, 0x62), "UTF-8")
  values <- c(
    "ababa", "\U0001f600a\U0001f600", latin1,
    "\ufeffabc", malformed, NA_character_, "none"
  )
  patterns <- c("a", "\U0001f600", "\u00e9", ".", ".", "x", "z")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)

  first <- with_altrep(
    TRUE, charr:::ci_extract_first_regex(subject, pattern)
  )
  expect_regex_char_unmaterialized(first)
  expect_identical(
    first,
    stringi::stri_extract_first_regex(values, patterns)
  )

  shared_values <- c("same", "two", "caf\u00e9")
  shared <- charport::as_charvec(shared_values)
  aliased <- with_altrep(
    TRUE, charr:::ci_extract_first_regex(shared, shared)
  )
  expect_regex_char_unmaterialized(aliased)
  expect_identical(
    aliased,
    stringi::stri_extract_first_regex(shared_values, shared_values)
  )

  expect_regex_char_unmaterialized(subject)
  expect_regex_char_unmaterialized(pattern)
  expect_regex_char_unmaterialized(shared)
})

test_that("regex extract all preserves zero matches, omit, and simplify", {
  values <- c("aaaa", "ababa", "none", NA_character_, "")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec("a")

  actual <- with_altrep(
    TRUE,
    charr:::ci_extract_all_regex(
      subject, pattern, omit_no_match = TRUE
    )
  )
  expect_true(all(vapply(actual, charport::is_charvec, logical(1))))
  invisible(lapply(actual, expect_regex_char_unmaterialized))
  expect_identical(
    actual,
    stringi::stri_extract_all_regex(
      values, "a", omit_no_match = TRUE
    )
  )

  chained <- with_altrep(
    TRUE, charr:::ci_count_regex(actual[[1]], charport::as_charvec("a"))
  )
  expect_identical(chained, rep(1L, 4L))

  zero <- with_altrep(
    TRUE,
    charr:::ci_extract_all_regex(
      charport::as_charvec(c("", "a", "\U0001f600")),
      charport::as_charvec(".*?"), omit_no_match = TRUE
    )
  )
  expect_identical(
    zero,
    stringi::stri_extract_all_regex(
      c("", "a", "\U0001f600"), ".*?", omit_no_match = TRUE
    )
  )

  for (simplify in list(TRUE, NA)) {
    matrix_result <- with_altrep(
      TRUE,
      charr:::ci_extract_all_regex(
        subject, pattern, omit_no_match = TRUE, simplify = simplify
      )
    )
    expect_regex_char_unmaterialized(matrix_result)
    expect_identical(
      matrix_result,
      stringi::stri_extract_all_regex(
        values, "a", omit_no_match = TRUE, simplify = simplify
      )
    )
  }

  expect_regex_char_unmaterialized(subject)
  expect_regex_char_unmaterialized(pattern)
})

test_that("regex split preserves state transitions and lazy result shapes", {
  values <- c("a,,b,c", ",a,", "abc", NA_character_)
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(",")
  cases <- list(
    list(n = 2L, omit_empty = FALSE, tokens_only = FALSE),
    list(n = 2L, omit_empty = FALSE, tokens_only = TRUE),
    list(n = -1L, omit_empty = TRUE, tokens_only = FALSE),
    list(n = -1L, omit_empty = NA, tokens_only = FALSE),
    list(n = c(-1L, 0L, 1L, NA_integer_),
         omit_empty = FALSE, tokens_only = FALSE)
  )

  for (args in cases) {
    actual <- with_altrep(
      TRUE,
      do.call(
        charr:::ci_split_regex,
        c(list(str = subject, pattern = pattern), args)
      )
    )
    expect_true(all(vapply(actual, charport::is_charvec, logical(1))))
    invisible(lapply(actual, expect_regex_char_unmaterialized))
    expect_identical(
      actual,
      do.call(
        stringi::stri_split_regex,
        c(list(str = values, pattern = ","), args)
      )
    )
  }

  for (simplify in list(TRUE, NA)) {
    matrix_result <- with_altrep(
      TRUE,
      charr:::ci_split_regex(
        subject, pattern, n = 3L, simplify = simplify
      )
    )
    expect_regex_char_unmaterialized(matrix_result)
    expect_identical(
      matrix_result,
      stringi::stri_split_regex(
        values, ",", n = 3L, simplify = simplify
      )
    )
  }

  lookahead <- with_altrep(
    TRUE,
    charr:::ci_split_regex(
      charport::as_charvec("abc"), charport::as_charvec("(?=.)")
    )
  )
  expect_identical(lookahead, list(c("", "a", "b", "c")))

  shared_values <- c("same", "two", "caf\u00e9")
  shared <- charport::as_charvec(shared_values)
  aliased <- with_altrep(TRUE, charr:::ci_split_regex(shared, shared))
  expect_identical(
    aliased,
    stringi::stri_split_regex(shared_values, shared_values)
  )
  expect_regex_char_unmaterialized(shared)
  expect_regex_char_unmaterialized(subject)
  expect_regex_char_unmaterialized(pattern)
})

test_that("regex UTF-8 paths preserve Latin-1, BOM, malformed, and bytes", {
  latin1 <- regex_char_marked(
    c(0x63, 0x61, 0x66, 0xe9, 0x7c, 0x74, 0x68, 0xe9),
    "latin1"
  )
  malformed <- regex_char_marked(
    c(0x61, 0xff, 0x7c, 0x62), "UTF-8"
  )
  values <- c(latin1, "\ufeffa|b", malformed)
  subject <- charport::as_charvec(values)
  delimiter <- charport::as_charvec("\\|")

  split <- with_altrep(
    TRUE, charr:::ci_split_regex(subject, delimiter)
  )
  expect_true(all(vapply(split, charport::is_charvec, logical(1))))
  invisible(lapply(split, expect_regex_char_unmaterialized))
  expect_identical(split, stringi::stri_split_regex(values, "\\|"))
  expect_identical(
    lapply(split[[3]], charToRaw),
    list(as.raw(c(0x61, 0xff)), as.raw(0x62))
  )

  extracted <- with_altrep(
    TRUE,
    charr:::ci_extract_first_regex(
      charport::as_charvec(c("\ufeffabc", malformed)),
      charport::as_charvec(c(".", "."))
    )
  )
  expect_regex_char_unmaterialized(extracted)
  expect_identical(extracted[[1]], "a")
  expect_identical(charToRaw(extracted[[2]]), as.raw(0x61))

  bytes <- regex_char_marked(c(0xff, 0xfe), "bytes")
  bytes_input <- charport::as_charvec(bytes)
  expect_error(
    with_altrep(TRUE, charr:::ci_extract_first_regex(bytes_input, "x")),
    "bytes encoding"
  )
  expect_error(
    with_altrep(TRUE, charr:::ci_extract_first_regex("x", bytes_input)),
    "bytes encoding"
  )
  expect_error(
    with_altrep(TRUE, charr:::ci_split_regex(bytes_input, "x")),
    "bytes encoding"
  )
  expect_error(
    with_altrep(TRUE, charr:::ci_split_regex("x", bytes_input)),
    "bytes encoding"
  )

  expect_regex_char_unmaterialized(subject)
  expect_regex_char_unmaterialized(delimiter)
  expect_regex_char_unmaterialized(bytes_input)
})

test_that("regex character operations keep compilation lazy and event order", {
  missing <- charport::as_charvec(NA_character_)
  present <- charport::as_charvec("x")
  invalid <- charport::as_charvec("[")

  expect_identical(
    with_altrep(TRUE, charr:::ci_extract_first_regex(missing, invalid)),
    stringi::stri_extract_first_regex(NA_character_, "[")
  )
  expect_identical(
    with_altrep(TRUE, charr:::ci_extract_all_regex(missing, invalid)),
    stringi::stri_extract_all_regex(NA_character_, "[")
  )
  expect_identical(
    with_altrep(TRUE, charr:::ci_split_regex(missing, invalid)),
    stringi::stri_split_regex(NA_character_, "[")
  )
  expect_error(
    with_altrep(TRUE, charr:::ci_extract_first_regex(present, invalid)),
    "U_REGEX_MISSING_CLOSE_BRACKET"
  )
  expect_error(
    with_altrep(TRUE, charr:::ci_split_regex(present, invalid)),
    "U_REGEX_MISSING_CLOSE_BRACKET"
  )
  values <- c("x", "x", "x")
  patterns <- c("", "[")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  expect_identical(
    regex_char_events(
      stringi::stri_extract_first_regex(values, patterns)
    ),
    regex_char_events(
      with_altrep(
        TRUE, charr:::ci_extract_first_regex(subject, pattern)
      )
    )
  )
  expect_identical(
    regex_char_events(stringi::stri_split_regex(values, patterns)),
    regex_char_events(
      with_altrep(TRUE, charr:::ci_split_regex(subject, pattern))
    )
  )

  opts_regex <- list(bogus = TRUE)
  expect_identical(
    regex_char_events(
      stringi::stri_extract_first_regex(
        values, c("x", "y"), opts_regex = opts_regex
      )
    ),
    regex_char_events(
      with_altrep(
        TRUE,
        charr:::ci_extract_first_regex(
          subject, charport::as_charvec(c("x", "y")),
          opts_regex = opts_regex
        )
      )
    )
  )
  expect_identical(
    regex_char_events(
      stringi::stri_split_regex(
        values, c("x", "y"), opts_regex = opts_regex
      )
    ),
    regex_char_events(
      with_altrep(
        TRUE,
        charr:::ci_split_regex(
          subject, charport::as_charvec(c("x", "y")),
          opts_regex = opts_regex
        )
      )
    )
  )

  expect_regex_char_unmaterialized(missing)
  expect_regex_char_unmaterialized(present)
  expect_regex_char_unmaterialized(invalid)
  expect_regex_char_unmaterialized(subject)
  expect_regex_char_unmaterialized(pattern)
})
