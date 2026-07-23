# Charr-owned tests for Reader-backed collation predicates.
# These are not imported from stringr.

coll_pred_marked_string <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_coll_pred_unmaterialized <- function(x) {
  expect_true(charport::is_charvec(x))
  expect_false(charport::charport_info(x)$is_materialized)
}

coll_pred_condition_events <- function(expr) {
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

test_that("collation detect and count consume unmaterialized UTF-16 inputs", {
  values <- c(
    "\u00e4pfel", "Apfel", "bl\u00e5b\u00e6r", "blaa",
    "\U0001f600\u00e4", "", NA_character_
  )
  patterns <- c("a", "a", "blaa", "bl\u00e5", "\u00e4", "x", "a")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  primary <- list(locale = "de", strength = 1L)
  tertiary <- list(locale = "de", strength = 3L)

  expect_identical(
    with_altrep(
      TRUE,
      charr:::ci_detect_coll(subject, pattern, opts_collator = primary)
    ),
    stringi::stri_detect_coll(values, patterns, opts_collator = primary)
  )
  expect_identical(
    with_altrep(
      TRUE,
      charr:::ci_detect_coll(subject, "A", opts_collator = tertiary)
    ),
    stringi::stri_detect_coll(values, "A", opts_collator = tertiary)
  )
  expect_identical(
    with_altrep(
      TRUE,
      charr:::ci_count_coll(subject, pattern, opts_collator = primary)
    ),
    stringi::stri_count_coll(values, patterns, opts_collator = primary)
  )

  expect_coll_pred_unmaterialized(subject)
  expect_coll_pred_unmaterialized(pattern)
})

test_that("collation predicates accept an exact subject-pattern alias", {
  values <- c("a", "\u00e4", "same", NA_character_)
  shared <- charport::as_charvec(values)
  opts <- list(locale = "de", strength = 2L)

  expect_identical(
    with_altrep(
      TRUE,
      charr:::ci_detect_coll(shared, shared, opts_collator = opts)
    ),
    stringi::stri_detect_coll(values, values, opts_collator = opts)
  )
  expect_identical(
    with_altrep(
      TRUE,
      charr:::ci_count_coll(shared, shared, opts_collator = opts)
    ),
    stringi::stri_count_coll(values, values, opts_collator = opts)
  )
  expect_identical(
    with_altrep(
      TRUE,
      charr:::ci_startswith_coll(shared, shared, opts_collator = opts)
    ),
    stringi::stri_startswith_coll(values, values, opts_collator = opts)
  )
  expect_identical(
    with_altrep(
      TRUE,
      charr:::ci_endswith_coll(shared, shared, opts_collator = opts)
    ),
    stringi::stri_endswith_coll(values, values, opts_collator = opts)
  )

  expect_coll_pred_unmaterialized(shared)
})

test_that("collation detect preserves serial max_count traversal", {
  values <- c(
    "Aarhus", "\u00c5rhus", "blaa", "bl\u00e5",
    "none", "", NA_character_, "Aalborg"
  )
  patterns <- c("\u00c5", "aa")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  opts <- list(locale = "da", strength = 1L)

  for (negate in c(FALSE, TRUE)) {
    for (max_count in c(-1L, 0L, 2L)) {
      expect_identical(
        with_altrep(
          TRUE,
          charr:::ci_detect_coll(
            subject, pattern, negate = negate, max_count = max_count,
            opts_collator = opts
          )
        ),
        stringi::stri_detect_coll(
          values, patterns, negate = negate, max_count = max_count,
          opts_collator = opts
        )
      )
    }
  }

  expect_coll_pred_unmaterialized(subject)
  expect_coll_pred_unmaterialized(pattern)
})

test_that("collation counting keeps UStringSearch match advancement", {
  values <- c("aaaa", "Aarhus Aalborg", "bl\u00e5 blaa", "", NA_character_)
  patterns <- c("aa", "\u00c5", "blaa", "x", "a")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  opts <- list(locale = "da", strength = 1L)

  expect_identical(
    with_altrep(
      TRUE,
      charr:::ci_count_coll(subject, pattern, opts_collator = opts)
    ),
    stringi::stri_count_coll(values, patterns, opts_collator = opts)
  )
  expect_coll_pred_unmaterialized(subject)
  expect_coll_pred_unmaterialized(pattern)
})

test_that("collation starts and ends use code-point indexes", {
  values <- rep("a\u00e9\U0001f600bc\u00e4", 10L)
  patterns <- c("a", "\u00e9", "\U0001f600", "b", "c", "a", "\u00e4", "x", "a", "a")
  from <- c(1L, 2L, 3L, -3L, 5L, 9L, -1L, NA_integer_, 0L, -9L)
  to <- c(1L, 2L, 3L, -3L, 5L, 0L, -1L, NA_integer_, 99L, -99L)
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  opts <- list(locale = "de", strength = 1L)

  for (negate in c(FALSE, TRUE)) {
    expect_identical(
      with_altrep(
        TRUE,
        charr:::ci_startswith_coll(
          subject, pattern, from = from, negate = negate,
          opts_collator = opts
        )
      ),
      stringi::stri_startswith_coll(
        values, patterns, from = from, negate = negate,
        opts_collator = opts
      )
    )
    expect_identical(
      with_altrep(
        TRUE,
        charr:::ci_endswith_coll(
          subject, pattern, to = to, negate = negate,
          opts_collator = opts
        )
      ),
      stringi::stri_endswith_coll(
        values, patterns, to = to, negate = negate,
        opts_collator = opts
      )
    )
  }

  expect_coll_pred_unmaterialized(subject)
  expect_coll_pred_unmaterialized(pattern)
})

test_that("collation predicates consume a previous charvec result", {
  source <- charport::as_charvec(c("\u00e4pfel", NA_character_, "Apfel"))
  replacement <- charport::as_charvec("apfel")
  subject <- with_altrep(
    TRUE,
    charr:::ci_replace_na(source, replacement)
  )
  expect_coll_pred_unmaterialized(subject)
  opts <- list(locale = "de", strength = 1L)

  expect_identical(
    with_altrep(
      TRUE,
      charr:::ci_detect_coll(subject, "a", opts_collator = opts)
    ),
    stringi::stri_detect_coll(
      c("\u00e4pfel", "apfel", "Apfel"), "a", opts_collator = opts
    )
  )
  expect_coll_pred_unmaterialized(subject)
  expect_coll_pred_unmaterialized(source)
  expect_coll_pred_unmaterialized(replacement)
})

test_that("collation option warnings precede recycling warnings", {
  values <- c("a", "b", "c")
  patterns <- c("a", "b")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  opts <- list(bogus = TRUE)

  calls <- list(
    detect = list(
      actual = quote(charr:::ci_detect_coll(
        subject, pattern, opts_collator = opts
      )),
      expected = quote(stringi::stri_detect_coll(
        values, patterns, opts_collator = opts
      ))
    ),
    count = list(
      actual = quote(charr:::ci_count_coll(
        subject, pattern, opts_collator = opts
      )),
      expected = quote(stringi::stri_count_coll(
        values, patterns, opts_collator = opts
      ))
    ),
    starts = list(
      actual = quote(charr:::ci_startswith_coll(
        subject, pattern, opts_collator = opts
      )),
      expected = quote(stringi::stri_startswith_coll(
        values, patterns, opts_collator = opts
      ))
    ),
    ends = list(
      actual = quote(charr:::ci_endswith_coll(
        subject, pattern, opts_collator = opts
      )),
      expected = quote(stringi::stri_endswith_coll(
        values, patterns, opts_collator = opts
      ))
    )
  )

  for (call in calls) {
    expect_identical(
      coll_pred_condition_events(
        with_altrep(TRUE, eval(call$actual))
      ),
      coll_pred_condition_events(eval(call$expected))
    )
  }

  expect_coll_pred_unmaterialized(subject)
  expect_coll_pred_unmaterialized(pattern)
})

test_that("empty-pattern warnings precede collation search errors", {
  values <- c("a", "x02")
  patterns <- c("", "x2")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  opts <- list(numeric = TRUE)

  expect_identical(
    coll_pred_condition_events(
      with_altrep(
        TRUE,
        charr:::ci_detect_coll(subject, pattern, opts_collator = opts)
      )
    ),
    coll_pred_condition_events(
      stringi::stri_detect_coll(values, patterns, opts_collator = opts)
    )
  )
  expect_identical(
    coll_pred_condition_events(
      with_altrep(
        TRUE,
        charr:::ci_count_coll(subject, pattern, opts_collator = opts)
      )
    ),
    coll_pred_condition_events(
      stringi::stri_count_coll(values, patterns, opts_collator = opts)
    )
  )

  maxed_subject <- charport::as_charvec(c("abc", "", NA_character_))
  empty_pattern <- charport::as_charvec("")
  expect_identical(
    coll_pred_condition_events(
      with_altrep(
        TRUE,
        charr:::ci_detect_coll(
          maxed_subject, empty_pattern, max_count = 0L
        )
      )
    ),
    coll_pred_condition_events(
      stringi::stri_detect_coll(
        c("abc", "", NA_character_), "", max_count = 0L
      )
    )
  )

  expect_identical(
    coll_pred_condition_events(
      with_altrep(
        TRUE,
        charr:::ci_startswith_coll(maxed_subject, empty_pattern)
      )
    ),
    coll_pred_condition_events(
      stringi::stri_startswith_coll(c("abc", "", NA_character_), "")
    )
  )
  expect_identical(
    suppressWarnings(
      with_altrep(
        TRUE,
        charr:::ci_startswith_coll(maxed_subject, empty_pattern)
      )
    ),
    suppressWarnings(
      stringi::stri_startswith_coll(c("abc", "", NA_character_), "")
    )
  )
  expect_identical(
    coll_pred_condition_events(
      with_altrep(
        TRUE,
        charr:::ci_endswith_coll(maxed_subject, empty_pattern)
      )
    ),
    coll_pred_condition_events(
      stringi::stri_endswith_coll(c("abc", "", NA_character_), "")
    )
  )
  expect_identical(
    suppressWarnings(
      with_altrep(
        TRUE,
        charr:::ci_endswith_coll(maxed_subject, empty_pattern)
      )
    ),
    suppressWarnings(
      stringi::stri_endswith_coll(c("abc", "", NA_character_), "")
    )
  )

  two_empty <- charport::as_charvec(c("", ""))
  expect_identical(
    coll_pred_condition_events(
      with_altrep(TRUE, charr:::ci_count_coll("a", two_empty))
    ),
    coll_pred_condition_events(stringi::stri_count_coll("a", c("", "")))
  )

  expect_coll_pred_unmaterialized(subject)
  expect_coll_pred_unmaterialized(pattern)
  expect_coll_pred_unmaterialized(maxed_subject)
  expect_coll_pred_unmaterialized(empty_pattern)
  expect_coll_pred_unmaterialized(two_empty)
})

test_that("collation predicates preserve encoding normalization", {
  latin1 <- coll_pred_marked_string(c(0x63, 0x61, 0x66, 0xe9), "latin1")
  malformed <- coll_pred_marked_string(c(0x61, 0xff, 0x62), "UTF-8")
  native <- coll_pred_marked_string(c(0x63, 0x61, 0x66, 0xc3, 0xa9), "unknown")
  values <- c(latin1, "\ufeffabc", malformed, native)
  patterns <- c("\u00e9", "a", "\ufffd", "\u00e9")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  opts <- list(locale = "en", strength = 3L)

  expect_identical(
    with_altrep(
      TRUE,
      charr:::ci_detect_coll(subject, pattern, opts_collator = opts)
    ),
    stringi::stri_detect_coll(values, patterns, opts_collator = opts)
  )
  expect_identical(
    with_altrep(
      TRUE,
      charr:::ci_count_coll(subject, pattern, opts_collator = opts)
    ),
    stringi::stri_count_coll(values, patterns, opts_collator = opts)
  )

  expect_coll_pred_unmaterialized(subject)
  expect_coll_pred_unmaterialized(pattern)
})

test_that("collation predicates eagerly reject bytes inputs", {
  bytes <- coll_pred_marked_string(c(0xff, 0xfe), "bytes")
  plain_subject <- charport::as_charvec("a")
  bytes_subject <- charport::as_charvec(bytes)
  bytes_pattern <- charport::as_charvec(bytes)

  cases <- list(
    list(
      actual = quote(charr:::ci_detect_coll(
        bytes_subject, "a", max_count = 0L
      )),
      expected = quote(stringi::stri_detect_coll(
        bytes, "a", max_count = 0L
      ))
    ),
    list(
      actual = quote(charr:::ci_count_coll(plain_subject, bytes_pattern)),
      expected = quote(stringi::stri_count_coll("a", bytes))
    ),
    list(
      actual = quote(charr:::ci_startswith_coll(
        plain_subject, bytes_pattern, from = NA_integer_
      )),
      expected = quote(stringi::stri_startswith_coll(
        "a", bytes, from = NA_integer_
      ))
    ),
    list(
      actual = quote(charr:::ci_endswith_coll(
        bytes_subject, "a", to = 0L
      )),
      expected = quote(stringi::stri_endswith_coll(
        bytes, "a", to = 0L
      ))
    )
  )

  for (case in cases) {
    expect_identical(
      coll_pred_condition_events(
        with_altrep(TRUE, eval(case$actual))
      ),
      coll_pred_condition_events(eval(case$expected))
    )
  }

  expect_coll_pred_unmaterialized(plain_subject)
  expect_coll_pred_unmaterialized(bytes_subject)
  expect_coll_pred_unmaterialized(bytes_pattern)
})

test_that("empty collation inputs preserve zero-length recycling", {
  subject <- charport::as_charvec(character())
  pattern <- charport::as_charvec("a")

  expect_identical(
    with_altrep(TRUE, charr:::ci_detect_coll(subject, pattern)),
    stringi::stri_detect_coll(character(), "a")
  )
  expect_identical(
    with_altrep(TRUE, charr:::ci_count_coll(subject, pattern)),
    stringi::stri_count_coll(character(), "a")
  )
  expect_identical(
    with_altrep(TRUE, charr:::ci_startswith_coll(subject, pattern)),
    stringi::stri_startswith_coll(character(), "a")
  )
  expect_identical(
    with_altrep(TRUE, charr:::ci_endswith_coll(subject, pattern)),
    stringi::stri_endswith_coll(character(), "a")
  )

  expect_coll_pred_unmaterialized(subject)
  expect_coll_pred_unmaterialized(pattern)
})
