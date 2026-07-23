# Charr-owned tests for Reader-backed boundary character results.
# These are not imported from stringr.

boundary_char_marked_string <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_boundary_char_unmaterialized <- function(x) {
  expect_true(charport::is_charvec(x))
  expect_false(charport::charport_info(x)$is_materialized)
}

expect_boundary_char_list_unmaterialized <- function(x) {
  expect_true(all(vapply(x, charport::is_charvec, logical(1))))
  invisible(lapply(x, expect_boundary_char_unmaterialized))
}

boundary_char_condition_events <- function(expr) {
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

test_that("boundary character results preserve every iterator traversal", {
  cases <- list(
    list(
      values = c("\U0001f469\u200d\U0001f469\u200d\U0001f467\u200d\U0001f466e\u0301", "a\U0001f600b"),
      opts = list(type = "character")
    ),
    list(
      values = c("\u65e5\u672c\u8a9e\u306e\u6587\u7ae0\u3067\u3059", "\u0e20\u0e32\u0e29\u0e32\u0e44\u0e17\u0e22\u0e20\u0e32\u0e29\u0e32\u0e44\u0e17\u0e22"),
      opts = list(type = "word", skip_word_none = TRUE, locale = "en")
    ),
    list(
      values = c("One. Two! Three?", "Last sentence."),
      opts = list(type = "sentence", locale = "en")
    ),
    list(
      values = c("alpha beta", "\u65e5\u672c\u8a9e"),
      opts = list(type = "line_break", locale = "ja")
    )
  )

  for (case in cases) {
    subject <- charport::as_charvec(case$values)

    first <- with_altrep(
      TRUE,
      charr:::ci_extract_first_boundaries(
        subject, opts_brkiter = case$opts
      )
    )
    expect_identical(
      first,
      stringi::stri_extract_first_boundaries(
        case$values, opts_brkiter = case$opts
      )
    )
    expect_boundary_char_unmaterialized(first)

    all <- with_altrep(
      TRUE,
      charr:::ci_extract_all_boundaries(
        subject, opts_brkiter = case$opts
      )
    )
    expect_identical(
      all,
      stringi::stri_extract_all_boundaries(
        case$values, opts_brkiter = case$opts
      )
    )
    expect_boundary_char_list_unmaterialized(all)

    split <- with_altrep(
      TRUE,
      charr:::ci_split_boundaries(
        subject, opts_brkiter = case$opts
      )
    )
    expect_identical(
      split,
      stringi::stri_split_boundaries(
        case$values, opts_brkiter = case$opts
      )
    )
    expect_boundary_char_list_unmaterialized(split)
    expect_boundary_char_unmaterialized(subject)
  }
})

test_that("boundary character results preserve skip rules and custom rules", {
  value <- "abc 123 \u65e5\u672c\u8a9e \u30ab\u30ca"
  subject <- charport::as_charvec(value)
  option_cases <- list(
    list(type = "word", skip_word_none = TRUE),
    list(
      type = "word", skip_word_none = TRUE,
      skip_word_letter = TRUE
    ),
    list(
      type = "word", skip_word_none = TRUE,
      skip_word_number = TRUE
    ),
    list(
      type = "word", skip_word_none = TRUE,
      skip_word_ideo = TRUE
    )
  )

  for (opts in option_cases) {
    extracted <- with_altrep(
      TRUE,
      charr:::ci_extract_all_boundaries(
        subject, opts_brkiter = opts
      )
    )
    expect_identical(
      extracted,
      stringi::stri_extract_all_boundaries(value, opts_brkiter = opts)
    )
    expect_boundary_char_list_unmaterialized(extracted)

    split <- with_altrep(
      TRUE,
      charr:::ci_split_boundaries(subject, opts_brkiter = opts)
    )
    expect_identical(
      split,
      stringi::stri_split_boundaries(value, opts_brkiter = opts)
    )
    expect_boundary_char_list_unmaterialized(split)
  }

  rules <- paste0(
    "$letters = [[:L:]]; $numbers = [[:N:]]; ",
    "$letters+; $numbers+; .;"
  )
  custom_values <- c("abc 123", "\u00e942")
  custom_subject <- charport::as_charvec(custom_values)
  custom_opts <- list(type = rules)
  custom_extract <- with_altrep(
    TRUE,
    charr:::ci_extract_all_boundaries(
      custom_subject, opts_brkiter = custom_opts
    )
  )
  expect_identical(
    custom_extract,
    stringi::stri_extract_all_boundaries(
      custom_values, opts_brkiter = custom_opts
    )
  )
  expect_boundary_char_list_unmaterialized(custom_extract)

  custom_split <- with_altrep(
    TRUE,
    charr:::ci_split_boundaries(
      custom_subject, opts_brkiter = custom_opts
    )
  )
  expect_identical(
    custom_split,
    stringi::stri_split_boundaries(
      custom_values, opts_brkiter = custom_opts
    )
  )
  expect_boundary_char_list_unmaterialized(custom_split)
  expect_boundary_char_unmaterialized(subject)
  expect_boundary_char_unmaterialized(custom_subject)
})

test_that("boundary extraction preserves NA, omit, and simplify shapes", {
  values <- c("", NA_character_, "abc", "123", "abc 123")
  subject <- charport::as_charvec(values)
  opts <- list(
    type = "word", skip_word_none = TRUE,
    skip_word_letter = TRUE, skip_word_number = TRUE
  )

  first <- with_altrep(
    TRUE,
    charr:::ci_extract_first_boundaries(
      subject, opts_brkiter = opts
    )
  )
  expect_identical(
    first,
    stringi::stri_extract_first_boundaries(values, opts_brkiter = opts)
  )
  expect_boundary_char_unmaterialized(first)

  for (omit_no_match in c(FALSE, TRUE)) {
    all <- with_altrep(
      TRUE,
      charr:::ci_extract_all_boundaries(
        subject, omit_no_match = omit_no_match,
        opts_brkiter = opts
      )
    )
    expect_identical(
      all,
      stringi::stri_extract_all_boundaries(
        values, omit_no_match = omit_no_match,
        opts_brkiter = opts
      )
    )
    expect_boundary_char_list_unmaterialized(all)
  }

  mixed_values <- c("abc 123", "abc", NA_character_, "")
  mixed <- charport::as_charvec(mixed_values)
  words <- list(type = "word", skip_word_none = TRUE)
  for (simplify in list(TRUE, NA)) {
    matrix_result <- with_altrep(
      TRUE,
      charr:::ci_extract_all_boundaries(
        mixed, simplify = simplify, opts_brkiter = words
      )
    )
    expect_identical(
      matrix_result,
      stringi::stri_extract_all_boundaries(
        mixed_values, simplify = simplify, opts_brkiter = words
      )
    )
    expect_boundary_char_unmaterialized(matrix_result)
  }

  empty <- charport::as_charvec(character())
  empty_first <- with_altrep(
    TRUE, charr:::ci_extract_first_boundaries(empty, opts_brkiter = words)
  )
  empty_all <- with_altrep(
    TRUE, charr:::ci_extract_all_boundaries(empty, opts_brkiter = words)
  )
  empty_matrix <- with_altrep(
    TRUE,
    charr:::ci_extract_all_boundaries(
      empty, simplify = TRUE, opts_brkiter = words
    )
  )
  expect_identical(empty_first, character())
  expect_identical(empty_all, list())
  expect_identical(
    empty_matrix,
    stringi::stri_extract_all_boundaries(
      character(), simplify = TRUE, opts_brkiter = words
    )
  )
  expect_boundary_char_unmaterialized(empty_first)
  expect_boundary_char_unmaterialized(empty_matrix)
  expect_boundary_char_unmaterialized(subject)
  expect_boundary_char_unmaterialized(mixed)
  expect_boundary_char_unmaterialized(empty)
})

test_that("boundary split preserves n, tokens_only, recycling, and simplify", {
  values <- c("abc def", "", NA_character_, "123 456")
  subject <- charport::as_charvec(values)
  opts <- list(type = "word", skip_word_none = TRUE)
  cases <- list(
    list(n = -1L, tokens_only = FALSE),
    list(n = 0L, tokens_only = FALSE),
    list(n = 1L, tokens_only = FALSE),
    list(n = 1L, tokens_only = TRUE),
    list(n = 2L, tokens_only = FALSE),
    list(n = 2L, tokens_only = TRUE),
    list(n = c(1L, 2L, NA_integer_), tokens_only = FALSE)
  )

  for (case in cases) {
    actual <- boundary_char_condition_events(
      with_altrep(
        TRUE,
        charr:::ci_split_boundaries(
          subject, n = case$n, tokens_only = case$tokens_only,
          opts_brkiter = opts
        )
      )
    )
    expected <- boundary_char_condition_events(
      stringi::stri_split_boundaries(
        values, n = case$n, tokens_only = case$tokens_only,
        opts_brkiter = opts
      )
    )
    expect_identical(actual, expected)
    expect_boundary_char_list_unmaterialized(actual$value)
  }

  for (simplify in list(TRUE, NA)) {
    matrix_result <- with_altrep(
      TRUE,
      charr:::ci_split_boundaries(
        subject, n = 3L, simplify = simplify,
        opts_brkiter = opts
      )
    )
    expect_identical(
      matrix_result,
      stringi::stri_split_boundaries(
        values, n = 3L, simplify = simplify,
        opts_brkiter = opts
      )
    )
    expect_boundary_char_unmaterialized(matrix_result)
  }

  empty <- with_altrep(
    TRUE,
    charr:::ci_split_boundaries(
      charport::as_charvec(character()), n = integer(),
      opts_brkiter = opts
    )
  )
  expect_identical(
    empty,
    stringi::stri_split_boundaries(character(), n = integer(),
      opts_brkiter = opts)
  )
  expect_identical(empty, list())
  expect_boundary_char_unmaterialized(subject)
})

test_that("boundary character output can be consumed without materializing", {
  values <- c("alpha beta", "\u65e5\u672c\u8a9e \u6587\u7ae0")
  subject <- charport::as_charvec(values)
  words <- list(type = "word", skip_word_none = TRUE)
  extracted <- with_altrep(
    TRUE,
    charr:::ci_extract_all_boundaries(subject, opts_brkiter = words)
  )
  expect_boundary_char_list_unmaterialized(extracted)

  chained <- with_altrep(
    TRUE,
    charr:::ci_split_boundaries(
      extracted[[1L]], opts_brkiter = list(type = "character")
    )
  )
  expect_identical(
    chained,
    stringi::stri_split_boundaries(
      stringi::stri_extract_all_boundaries(
        values, opts_brkiter = words
      )[[1L]],
      opts_brkiter = list(type = "character")
    )
  )
  expect_boundary_char_list_unmaterialized(chained)
  expect_boundary_char_unmaterialized(extracted[[1L]])
  expect_boundary_char_unmaterialized(subject)
})

test_that("boundary iterators preserve lazy-open warning and error order", {
  fallback <- list(type = "word", locale = "xx_YY")
  subject <- charport::as_charvec(c("abc", "def"))

  calls <- list(
    list(
      actual = quote(with_altrep(
        TRUE,
        charr:::ci_extract_first_boundaries(
          subject, opts_brkiter = fallback
        )
      )),
      expected = quote(stringi::stri_extract_first_boundaries(
        c("abc", "def"), opts_brkiter = fallback
      ))
    ),
    list(
      actual = quote(with_altrep(
        TRUE,
        charr:::ci_extract_all_boundaries(
          subject, opts_brkiter = fallback
        )
      )),
      expected = quote(stringi::stri_extract_all_boundaries(
        c("abc", "def"), opts_brkiter = fallback
      ))
    ),
    list(
      actual = quote(with_altrep(
        TRUE,
        charr:::ci_split_boundaries(
          subject, opts_brkiter = fallback
        )
      )),
      expected = quote(stringi::stri_split_boundaries(
        c("abc", "def"), opts_brkiter = fallback
      ))
    )
  )
  for (call in calls) {
    actual <- boundary_char_condition_events(eval(call$actual))
    expected <- boundary_char_condition_events(eval(call$expected))
    expect_identical(actual, expected)
    expect_match(actual$events, "^warning:")
  }

  bad_rules <- list(type = "[")
  inactive <- charport::as_charvec(c("", NA_character_))
  expect_identical(
    boundary_char_condition_events(
      with_altrep(
        TRUE,
        charr:::ci_extract_first_boundaries(
          inactive, opts_brkiter = bad_rules
        )
      )
    ),
    boundary_char_condition_events(
      stringi::stri_extract_first_boundaries(
        c("", NA_character_), opts_brkiter = bad_rules
      )
    )
  )
  expect_identical(
    boundary_char_condition_events(
      with_altrep(
        TRUE,
        charr:::ci_extract_all_boundaries(
          inactive, opts_brkiter = bad_rules
        )
      )
    ),
    boundary_char_condition_events(
      stringi::stri_extract_all_boundaries(
        c("", NA_character_), opts_brkiter = bad_rules
      )
    )
  )

  split_cases <- list(
    list(str = "", n = 0L, opts = bad_rules),
    list(str = "abc", n = .Machine$integer.max, opts = bad_rules),
    list(
      str = c("abc", "def"),
      n = c(1L, .Machine$integer.max), opts = bad_rules
    ),
    list(
      str = c("abc", "def"),
      n = c(1L, .Machine$integer.max), opts = fallback
    ),
    list(
      str = c("abc", "def", "ghi"),
      n = c(1L, 2L), opts = bad_rules
    )
  )
  for (case in split_cases) {
    actual <- boundary_char_condition_events(
      with_altrep(
        TRUE,
        charr:::ci_split_boundaries(
          charport::as_charvec(case$str), n = case$n,
          opts_brkiter = case$opts
        )
      )
    )
    expected <- boundary_char_condition_events(
      stringi::stri_split_boundaries(
        case$str, n = case$n, opts_brkiter = case$opts
      )
    )
    expect_identical(actual, expected)
  }

  expect_boundary_char_unmaterialized(subject)
  expect_boundary_char_unmaterialized(inactive)
})

test_that("boundary character results preserve marked input semantics", {
  latin1 <- boundary_char_marked_string(
    c(0x63, 0x61, 0x66, 0xe9), "latin1"
  )
  native <- boundary_char_marked_string(
    c(0x63, 0x61, 0x66, 0xc3, 0xa9), "unknown"
  )
  malformed <- boundary_char_marked_string(
    c(0x61, 0xc3, 0x28, 0x62), "UTF-8"
  )
  values <- c(latin1, native, "\ufeffabc", malformed, NA_character_, "")
  subject <- charport::as_charvec(values)
  opts <- list(type = "character")

  first <- with_altrep(
    TRUE,
    charr:::ci_extract_first_boundaries(subject, opts_brkiter = opts)
  )
  expected_first <- stringi::stri_extract_first_boundaries(
    values, opts_brkiter = opts
  )
  expect_identical(first, expected_first)
  expect_identical(
    lapply(as.character(first), charToRaw),
    lapply(expected_first, charToRaw)
  )
  expect_identical(Encoding(first), Encoding(expected_first))
  expect_boundary_char_unmaterialized(first)

  all <- with_altrep(
    TRUE,
    charr:::ci_extract_all_boundaries(subject, opts_brkiter = opts)
  )
  expect_identical(
    all,
    stringi::stri_extract_all_boundaries(values, opts_brkiter = opts)
  )
  expect_boundary_char_list_unmaterialized(all)

  split <- with_altrep(
    TRUE,
    charr:::ci_split_boundaries(subject, opts_brkiter = opts)
  )
  expect_identical(
    split,
    stringi::stri_split_boundaries(values, opts_brkiter = opts)
  )
  expect_boundary_char_list_unmaterialized(split)
  expect_boundary_char_unmaterialized(subject)

  bytes <- boundary_char_marked_string(c(0xff, 0xfe), "bytes")
  bytes_subject <- charport::as_charvec(bytes)
  byte_calls <- list(
    list(
      actual = quote(with_altrep(
        TRUE,
        charr:::ci_extract_first_boundaries(
          bytes_subject, opts_brkiter = opts
        )
      )),
      expected = quote(stringi::stri_extract_first_boundaries(
        bytes, opts_brkiter = opts
      ))
    ),
    list(
      actual = quote(with_altrep(
        TRUE,
        charr:::ci_extract_all_boundaries(
          bytes_subject, opts_brkiter = opts
        )
      )),
      expected = quote(stringi::stri_extract_all_boundaries(
        bytes, opts_brkiter = opts
      ))
    ),
    list(
      actual = quote(with_altrep(
        TRUE,
        charr:::ci_split_boundaries(
          bytes_subject, opts_brkiter = opts
        )
      )),
      expected = quote(stringi::stri_split_boundaries(
        bytes, opts_brkiter = opts
      ))
    )
  )
  for (call in byte_calls) {
    actual <- boundary_char_condition_events(eval(call$actual))
    expected <- boundary_char_condition_events(eval(call$expected))
    expect_identical(actual, expected)
    expect_match(actual$events, "bytes encoding is not supported")
  }
  expect_boundary_char_unmaterialized(bytes_subject)
})
