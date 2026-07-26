# Charr-owned tests for Reader-backed collation replacement.
# These are not imported from stringr.

coll_replace_marked_string <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_coll_replace_unmaterialized <- function(x) {
  expect_true(charport::is_charvec(x))
  expect_false(charport::charport_info(x)$is_materialized)
}

coll_replace_condition_events <- function(expr) {
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

test_that("collation replacement preserves all vectorized traversals", {
  values <- c(
    "\U0001f600\u00e4-a-A", "u\u0308x\u00dc", "\u00e5-aa-\u00c5",
    "none", "", NA_character_
  )
  patterns <- c("a", "\u00fc", "aa", "a", "x", "a")
  replacements <- c("$1", "Z", NA_character_)
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  replacement <- charport::as_charvec(replacements)
  opts <- list(locale = "de", strength = 1L)

  actual_all <- with_altrep(
    TRUE,
    charr:::ci_replace_all_coll(
      subject, pattern, replacement, opts_collator = opts
    )
  )
  expected_all <- stringi::stri_replace_all_coll(
    values, patterns, replacements, opts_collator = opts
  )
  expect_identical(actual_all, expected_all)

  actual_first <- with_altrep(
    TRUE,
    charr:::ci_replace_first_coll(
      subject, pattern, replacement, opts_collator = opts
    )
  )
  expect_identical(
    actual_first,
    stringi::stri_replace_first_coll(
      values, patterns, replacements, opts_collator = opts
    )
  )

  expect_coll_replace_unmaterialized(actual_all)
  expect_coll_replace_unmaterialized(actual_first)

  chained <- with_altrep(
    TRUE,
    charr:::ci_count_coll(
      actual_all, charport::as_charvec("Z"),
      opts_collator = opts
    )
  )
  expect_identical(
    chained,
    stringi::stri_count_coll(expected_all, "Z", opts_collator = opts)
  )
  expect_coll_replace_unmaterialized(actual_all)

  expect_coll_replace_unmaterialized(subject)
  expect_coll_replace_unmaterialized(pattern)
  expect_coll_replace_unmaterialized(replacement)

  empty_input <- charport::as_charvec(character())
  empty_pattern <- charport::as_charvec("a")
  empty_replacement <- charport::as_charvec("x")
  empty_results <- list(
    with_altrep(
      TRUE,
      charr:::ci_replace_all_coll(
        empty_input, empty_pattern, empty_replacement
      )
    ),
    with_altrep(
      TRUE,
      charr:::ci_replace_first_coll(
        empty_input, empty_pattern, empty_replacement
      )
    )
  )
  expect_identical(
    empty_results,
    list(
      stringi::stri_replace_all_coll(character(), "a", "x"),
      stringi::stri_replace_first_coll(character(), "a", "x")
    )
  )
  for (result in empty_results)
    expect_coll_replace_unmaterialized(result)
})

test_that("collation replacement accepts aliases and sequential patterns", {
  shared_values <- c("a", "b", "c", NA_character_)
  shared <- charport::as_charvec(shared_values)
  aliased <- with_altrep(
    TRUE,
    charr:::ci_replace_all_coll(shared, shared, shared)
  )
  expect_identical(
    aliased,
    stringi::stri_replace_all_coll(
      shared_values, shared_values, shared_values
    )
  )
  expect_coll_replace_unmaterialized(aliased)
  expect_coll_replace_unmaterialized(shared)

  values <- c("\u00e4-a-A", "u\u0308-\u00dc", "\u00e5-aa", "none", "", NA_character_)
  patterns <- c("\u00e4", "a", "\u00dc")
  replacements <- c("X", "Y", "Z")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  replacement <- charport::as_charvec(replacements)
  opts <- list(locale = "de", strength = 3L)

  sequential <- with_altrep(
    TRUE,
    charr:::ci_replace_all_coll(
      subject, pattern, replacement,
      vectorize_all = FALSE, opts_collator = opts
    )
  )
  expect_identical(
    sequential,
    stringi::stri_replace_all_coll(
      values, patterns, replacements,
      vectorize_all = FALSE, opts_collator = opts
    )
  )
  expect_coll_replace_unmaterialized(sequential)

  missing_pattern <- charport::as_charvec(c("a", NA_character_))
  missing <- with_altrep(
    TRUE,
    charr:::ci_replace_all_coll(
      subject, missing_pattern, charport::as_charvec(c("A", "X")),
      vectorize_all = FALSE
    )
  )
  expect_identical(
    missing,
    stringi::stri_replace_all_coll(
      values, c("a", NA_character_), c("A", "X"),
      vectorize_all = FALSE
    )
  )
  expect_coll_replace_unmaterialized(missing)
  expect_coll_replace_unmaterialized(missing_pattern)

  na_replacement <- charport::as_charvec(c(NA_character_, "A"))
  na_actual <- with_altrep(
    TRUE,
    charr:::ci_replace_all_coll(
      subject, charport::as_charvec(c("z", "a")), na_replacement,
      vectorize_all = FALSE
    )
  )
  expect_identical(
    na_actual,
    stringi::stri_replace_all_coll(
      values, c("z", "a"), c(NA_character_, "A"),
      vectorize_all = FALSE
    )
  )
  expect_coll_replace_unmaterialized(na_actual)
  expect_coll_replace_unmaterialized(na_replacement)
  expect_coll_replace_unmaterialized(subject)
  expect_coll_replace_unmaterialized(pattern)
  expect_coll_replace_unmaterialized(replacement)
})

test_that("collation replacement honors locale and strength", {
  danish_values <- c("Aarhus", "\u00c5rhus", "blaa", "bl\u00e5")
  danish_subject <- charport::as_charvec(danish_values)
  danish_pattern <- charport::as_charvec(c("\u00c5", "aa"))
  replacement <- charport::as_charvec("X")
  danish_opts <- list(locale = "da", strength = 1L)

  expect_identical(
    with_altrep(
      TRUE,
      charr:::ci_replace_all_coll(
        danish_subject, danish_pattern, replacement,
        opts_collator = danish_opts
      )
    ),
    stringi::stri_replace_all_coll(
      danish_values, c("\u00c5", "aa"), "X",
      opts_collator = danish_opts
    )
  )

  german_values <- c("\u00e4aA", "u\u0308\u00dc")
  german_subject <- charport::as_charvec(german_values)
  for (strength in c(1L, 3L)) {
    opts <- list(locale = "de", strength = strength, normalization = TRUE)
    expect_identical(
      with_altrep(
        TRUE,
        charr:::ci_replace_all_coll(
          german_subject, charport::as_charvec("a"), replacement,
          opts_collator = opts
        )
      ),
      stringi::stri_replace_all_coll(
        german_values, "a", "X", opts_collator = opts
      )
    )
  }

  numeric_opts <- list(locale = "en", numeric = TRUE)
  expect_identical(
    coll_replace_condition_events(
      with_altrep(
        TRUE,
        charr:::ci_replace_all_coll(
          german_subject, charport::as_charvec("a"), replacement,
          opts_collator = numeric_opts
        )
      )
    )$events,
    coll_replace_condition_events(
      stringi::stri_replace_all_coll(
        german_values, "a", "X", opts_collator = numeric_opts
      )
    )$events
  )

  expect_coll_replace_unmaterialized(danish_subject)
  expect_coll_replace_unmaterialized(german_subject)
  expect_coll_replace_unmaterialized(danish_pattern)
  expect_coll_replace_unmaterialized(replacement)
})

test_that("collation replacement preserves empty-pattern warnings", {
  cases <- list(
    list(pattern = "", replacement = "x"),
    list(pattern = c("", "a"), replacement = c("x", "y")),
    list(pattern = c("a", ""), replacement = c("x", "y")),
    list(pattern = c("", ""), replacement = c("x", "y"))
  )

  for (case in cases) {
    subject <- charport::as_charvec("a")
    pattern <- charport::as_charvec(case$pattern)
    replacement <- charport::as_charvec(case$replacement)
    actual <- coll_replace_condition_events(
      with_altrep(
        TRUE,
        charr:::ci_replace_all_coll(
          subject, pattern, replacement, vectorize_all = FALSE
        )
      )
    )
    expected <- coll_replace_condition_events(
      stringi::stri_replace_all_coll(
        "a", case$pattern, case$replacement,
        vectorize_all = FALSE
      )
    )
    expect_identical(actual, expected)
    expect_coll_replace_unmaterialized(subject)
    expect_coll_replace_unmaterialized(pattern)
    expect_coll_replace_unmaterialized(replacement)
  }

  vectorized <- coll_replace_condition_events(
    with_altrep(
      TRUE,
      charr:::ci_replace_all_coll(
        charport::as_charvec(c("", NA_character_, "a")),
        charport::as_charvec(""),
        charport::as_charvec(NA_character_)
      )
    )
  )
  expect_identical(
    vectorized,
    coll_replace_condition_events(
      stringi::stri_replace_all_coll(
        c("", NA_character_, "a"), "", NA_character_
      )
    )
  )
  expect_true(all(is.na(vectorized$value)))
})

test_that("collation replacement preserves validation and option order", {
  vectorized_prepare <- coll_replace_condition_events(
    with_altrep(
      TRUE,
      charr:::ci_replace_all_coll("a", new.env(), new.env())
    )
  )
  expect_identical(
    vectorized_prepare,
    coll_replace_condition_events(
      stringi::stri_replace_all_coll("a", new.env(), new.env())
    )
  )

  sequential_prepare <- coll_replace_condition_events(
    with_altrep(
      TRUE,
      charr:::ci_replace_all_coll(
        "a", new.env(), new.env(), vectorize_all = FALSE
      )
    )
  )
  expect_identical(
    sequential_prepare,
    coll_replace_condition_events(
      stringi::stri_replace_all_coll(
        "a", new.env(), new.env(), vectorize_all = FALSE
      )
    )
  )

  vectorized_actual <- coll_replace_condition_events(
    with_altrep(
      TRUE,
      charr:::ci_replace_all_coll(
        charport::as_charvec(c("a", "b", "c")),
        charport::as_charvec(c("a", "b")),
        charport::as_charvec("x"),
        opts_collator = list(bogus = TRUE)
      )
    )
  )
  vectorized_expected <- coll_replace_condition_events(
    stringi::stri_replace_all_coll(
      c("a", "b", "c"), c("a", "b"), "x",
      opts_collator = list(bogus = TRUE)
    )
  )
  expect_identical(vectorized_actual, vectorized_expected)

  sequential_actual <- coll_replace_condition_events(
    with_altrep(
      TRUE,
      charr:::ci_replace_all_coll(
        charport::as_charvec("a"),
        charport::as_charvec(c("a", "b", "c")),
        charport::as_charvec(c("x", "y")),
        vectorize_all = FALSE,
        opts_collator = list(bogus = TRUE)
      )
    )
  )
  sequential_expected <- coll_replace_condition_events(
    stringi::stri_replace_all_coll(
      "a", c("a", "b", "c"), c("x", "y"),
      vectorize_all = FALSE,
      opts_collator = list(bogus = TRUE)
    )
  )
  expect_identical(sequential_actual, sequential_expected)

  invalid_actual <- coll_replace_condition_events(
    with_altrep(
      TRUE,
      charr:::ci_replace_all_coll(
        charport::as_charvec("a"),
        charport::as_charvec(c("a", "b")),
        charport::as_charvec(c("x", "y", "z")),
        vectorize_all = FALSE,
        opts_collator = list(bogus = TRUE)
      )
    )
  )
  invalid_expected <- coll_replace_condition_events(
    stringi::stri_replace_all_coll(
      "a", c("a", "b"), c("x", "y", "z"),
      vectorize_all = FALSE,
      opts_collator = list(bogus = TRUE)
    )
  )
  expect_identical(invalid_actual, invalid_expected)
  expect_match(invalid_actual$events, "^error:")
})

test_that("empty sequential subjects ignore later arguments", {
  bytes <- coll_replace_marked_string(c(0xff, 0xfe), "bytes")
  subject <- charport::as_charvec(character())
  pattern <- charport::as_charvec(bytes)
  replacement <- charport::as_charvec(bytes)

  actual <- coll_replace_condition_events(
    with_altrep(
      TRUE,
      charr:::ci_replace_all_coll(
        subject, pattern, replacement,
        vectorize_all = FALSE,
        opts_collator = list(bogus = TRUE)
      )
    )
  )
  expected <- coll_replace_condition_events(
    stringi::stri_replace_all_coll(
      character(), bytes, bytes,
      vectorize_all = FALSE,
      opts_collator = list(bogus = TRUE)
    )
  )
  expect_identical(actual, expected)
  expect_identical(actual$value, character())
  expect_coll_replace_unmaterialized(actual$value)
  expect_coll_replace_unmaterialized(subject)
  expect_coll_replace_unmaterialized(pattern)
  expect_coll_replace_unmaterialized(replacement)
})

test_that("collation replacement preserves marked and malformed strings", {
  latin1 <- coll_replace_marked_string(
    c(0x63, 0x61, 0x66, 0xe9), "latin1"
  )
  malformed <- coll_replace_marked_string(c(0x61, 0xff, 0x62), "UTF-8")
  native <- coll_replace_marked_string(
    c(0x63, 0x61, 0x66, 0xc3, 0xa9), "unknown"
  )
  values <- c(latin1, "\ufeffabc", malformed, "ab", native)
  patterns <- c("\u00e9", "a", "b", "b", "\u00e9")
  replacements <- c("E", "X", "Y", malformed, "N")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  replacement <- charport::as_charvec(replacements)

  actual <- with_altrep(
    TRUE,
    charr:::ci_replace_all_coll(subject, pattern, replacement)
  )
  expected <- stringi::stri_replace_all_coll(
    values, patterns, replacements
  )
  expect_identical(actual, expected)
  expect_identical(
    lapply(as.character(actual), charToRaw),
    lapply(expected, charToRaw)
  )
  expect_identical(Encoding(actual), Encoding(expected))
  expect_coll_replace_unmaterialized(actual)
  expect_coll_replace_unmaterialized(subject)
  expect_coll_replace_unmaterialized(pattern)
  expect_coll_replace_unmaterialized(replacement)
})

test_that("collation replacement rejects bytes in container order", {
  bytes <- coll_replace_marked_string(c(0xff, 0xfe), "bytes")
  cases <- list(
    list(str = bytes, pattern = bytes, replacement = bytes),
    list(str = "a", pattern = bytes, replacement = bytes),
    list(str = "a", pattern = "z", replacement = bytes)
  )

  for (case in cases) {
    subject <- charport::as_charvec(case$str)
    pattern <- charport::as_charvec(case$pattern)
    replacement <- charport::as_charvec(case$replacement)
    actual <- coll_replace_condition_events(
      with_altrep(
        TRUE,
        charr:::ci_replace_all_coll(subject, pattern, replacement)
      )
    )
    expected <- coll_replace_condition_events(
      stringi::stri_replace_all_coll(
        case$str, case$pattern, case$replacement
      )
    )
    expect_identical(actual, expected)
    expect_match(actual$events, "bytes encoding is not supported")
    expect_coll_replace_unmaterialized(subject)
    expect_coll_replace_unmaterialized(pattern)
    expect_coll_replace_unmaterialized(replacement)
  }

  option_subject <- charport::as_charvec(bytes)
  option_pattern <- charport::as_charvec(c("a", "b"))
  option_replacement <- charport::as_charvec("x")
  for (vectorize_all in c(TRUE, FALSE)) {
    actual <- coll_replace_condition_events(
      with_altrep(
        TRUE,
        charr:::ci_replace_all_coll(
          option_subject, option_pattern, option_replacement,
          vectorize_all = vectorize_all,
          opts_collator = list(bogus = TRUE)
        )
      )
    )
    expected <- coll_replace_condition_events(
      stringi::stri_replace_all_coll(
        bytes, c("a", "b"), "x",
        vectorize_all = vectorize_all,
        opts_collator = list(bogus = TRUE)
      )
    )
    expect_identical(actual, expected)
    expect_identical(
      sub(":.*", "", actual$events),
      c("warning", "error")
    )
  }
  expect_coll_replace_unmaterialized(option_subject)
  expect_coll_replace_unmaterialized(option_pattern)
  expect_coll_replace_unmaterialized(option_replacement)

  warning_patterns <- list("", c("", "a"))
  for (vectorize_all in c(TRUE, FALSE)) {
    current_patterns <- warning_patterns[[if (vectorize_all) 1L else 2L]]
    actual <- coll_replace_condition_events(
      with_altrep(
        TRUE,
        charr:::ci_replace_all_coll(
          charport::as_charvec("a"),
          charport::as_charvec(current_patterns),
          charport::as_charvec(bytes),
          vectorize_all = vectorize_all
        )
      )
    )
    expected <- coll_replace_condition_events(
      stringi::stri_replace_all_coll(
        "a", current_patterns, bytes,
        vectorize_all = vectorize_all
      )
    )
    expect_identical(actual, expected)
    expect_identical(
      sub(":.*", "", actual$events),
      c("warning", "error")
    )
  }
})
