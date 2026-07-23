# Charr-owned tests for Reader-backed collation locate operations.
# These are not imported from stringr.

coll_locate_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_coll_locate_unmaterialized <- function(x) {
  expect_true(charport::is_charvec(x))
  expect_false(charport::charport_info(x)$is_materialized)
}

coll_locate_events <- function(expr) {
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

test_that("collation locate converts UTF-16 offsets to code-point positions", {
  values <- c(
    "\U0001f600\u00e4-a-A", "u\u0308x\u00dc", "\u00e5-aa-\u00c5",
    "none", "", NA_character_
  )
  patterns <- c("a", "\u00fc", "aa", "a", "x", NA_character_)
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  opts <- list(locale = "de", strength = 1L)

  first <- with_altrep(
    TRUE,
    charr:::ci_locate_first_coll(
      subject, pattern, opts_collator = opts
    )
  )
  expect_identical(
    first,
    stringi::stri_locate_first_coll(
      values, patterns, opts_collator = opts
    )
  )

  expect_identical(colnames(first), c("start", "end"))
  expect_coll_locate_unmaterialized(subject)
  expect_coll_locate_unmaterialized(pattern)
})

test_that("collation locate all preserves every result shape", {
  values <- c("\U0001f600\u00e4-a-A", "u\u0308x\u00dc", "none", "", NA_character_)
  patterns <- c("a", "\u00fc", "a", "x", "a")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  opts <- list(locale = "de", strength = 1L)

  for (omit_no_match in c(FALSE, TRUE)) {
    for (get_length in c(FALSE, TRUE)) {
      actual <- with_altrep(
        TRUE,
        charr:::ci_locate_all_coll(
          subject, pattern,
          omit_no_match = omit_no_match,
          get_length = get_length,
          opts_collator = opts
        )
      )
      expected <- stringi::stri_locate_all_coll(
        values, patterns,
        omit_no_match = omit_no_match,
        get_length = get_length,
        opts_collator = opts
      )
      expect_identical(actual, expected)
      expect_true(all(vapply(actual, is.matrix, logical(1))))
      expected_names <- if (get_length) {
        c("start", "length")
      } else {
        c("start", "end")
      }
      expect_true(all(vapply(
        actual,
        function(x) identical(colnames(x), expected_names),
        logical(1)
      )))
    }
  }

  expect_coll_locate_unmaterialized(subject)
  expect_coll_locate_unmaterialized(pattern)
})

test_that("collation locate supports exact aliases and recycling", {
  shared_values <- c("same", "two", "caf\u00e9")
  shared <- charport::as_charvec(shared_values)

  expect_identical(
    with_altrep(TRUE, charr:::ci_locate_first_coll(shared, shared)),
    stringi::stri_locate_first_coll(shared_values, shared_values)
  )
  expect_identical(
    with_altrep(TRUE, charr:::ci_locate_all_coll(shared, shared)),
    stringi::stri_locate_all_coll(shared_values, shared_values)
  )

  values <- c("Aarhus", "\u00c5rhus", "blaa", "bl\u00e5", NA_character_)
  patterns <- c("\u00c5", "aa")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  opts <- list(locale = "da", strength = 1L)

  expect_warning(
    actual <- with_altrep(
      TRUE,
      charr:::ci_locate_first_coll(
        subject, pattern, opts_collator = opts
      )
    ),
    "longer object length is not a multiple"
  )
  expect_warning(
    expected <- stringi::stri_locate_first_coll(
      values, patterns, opts_collator = opts
    ),
    "longer object length is not a multiple"
  )
  expect_identical(actual, expected)

  expect_coll_locate_unmaterialized(shared)
  expect_coll_locate_unmaterialized(subject)
  expect_coll_locate_unmaterialized(pattern)
})

test_that("collation locate preserves option and warning order", {
  values <- c("a", "b", "c")
  patterns <- c("", "x")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)
  opts <- list(bogus = TRUE)

  expect_identical(
    coll_locate_events(
      stringi::stri_locate_first_coll(
        values, patterns, opts_collator = opts
      )
    ),
    coll_locate_events(
      with_altrep(
        TRUE,
        charr:::ci_locate_first_coll(
          subject, pattern, opts_collator = opts
        )
      )
    )
  )
  expect_identical(
    coll_locate_events(
      stringi::stri_locate_all_coll(
        values, patterns, opts_collator = opts
      )
    ),
    coll_locate_events(
      with_altrep(
        TRUE,
        charr:::ci_locate_all_coll(
          subject, pattern, opts_collator = opts
        )
      )
    )
  )

  expect_coll_locate_unmaterialized(subject)
  expect_coll_locate_unmaterialized(pattern)
})

test_that("collation locate preserves marked and malformed inputs", {
  latin1 <- coll_locate_marked(c(0x63, 0x61, 0x66, 0xe9), "latin1")
  malformed <- coll_locate_marked(c(0x61, 0xff, 0x62), "UTF-8")
  values <- c(latin1, "\ufeffabc", malformed, malformed, "abc")
  patterns <- c("\u00e9", "a", "b", "\ufffd", "\ufeffa")
  subject <- charport::as_charvec(values)
  pattern <- charport::as_charvec(patterns)

  first <- with_altrep(
    TRUE, charr:::ci_locate_first_coll(subject, pattern)
  )
  expect_identical(
    first,
    stringi::stri_locate_first_coll(values, patterns)
  )
  all <- with_altrep(
    TRUE, charr:::ci_locate_all_coll(subject, pattern)
  )
  expect_identical(
    all,
    stringi::stri_locate_all_coll(values, patterns)
  )

  expect_identical(first[2L, ], c(start = 2L, end = 2L))
  expect_identical(first[3L, ], c(start = 3L, end = 3L))
  expect_identical(first[4L, ], c(start = 2L, end = 2L))
  expect_identical(first[5L, ], c(start = 1L, end = 1L))

  expect_coll_locate_unmaterialized(subject)
  expect_coll_locate_unmaterialized(pattern)
})

test_that("collation locate preserves zero recycling and bytes errors", {
  bytes <- coll_locate_marked(c(0xff, 0xfe), "bytes")
  bytes_input <- charport::as_charvec(bytes)
  empty_pattern <- charport::as_charvec(character())

  first_empty <- with_altrep(
    TRUE, charr:::ci_locate_first_coll(bytes_input, empty_pattern)
  )
  all_empty <- with_altrep(
    TRUE, charr:::ci_locate_all_coll(bytes_input, empty_pattern)
  )
  expect_identical(
    first_empty,
    stringi::stri_locate_first_coll(bytes, character())
  )
  expect_identical(
    all_empty,
    stringi::stri_locate_all_coll(bytes, character())
  )

  expect_error(
    with_altrep(TRUE, charr:::ci_locate_first_coll(bytes_input, "x")),
    "bytes encoding"
  )
  expect_error(
    with_altrep(TRUE, charr:::ci_locate_first_coll("x", bytes_input)),
    "bytes encoding"
  )
  expect_error(
    with_altrep(TRUE, charr:::ci_locate_all_coll(bytes_input, "x")),
    "bytes encoding"
  )
  expect_error(
    with_altrep(TRUE, charr:::ci_locate_all_coll("x", bytes_input)),
    "bytes encoding"
  )

  values <- c(bytes, "x", "y")
  patterns <- c("x", "y")
  expect_identical(
    coll_locate_events(
      stringi::stri_locate_all_coll(
        values, patterns, opts_collator = list(bogus = TRUE)
      )
    ),
    coll_locate_events(
      with_altrep(
        TRUE,
        charr:::ci_locate_all_coll(
          charport::as_charvec(values),
          charport::as_charvec(patterns),
          opts_collator = list(bogus = TRUE)
        )
      )
    )
  )

  expect_coll_locate_unmaterialized(bytes_input)
  expect_coll_locate_unmaterialized(empty_pattern)
})
