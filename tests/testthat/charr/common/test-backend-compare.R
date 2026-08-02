# Charr-owned tests for Reader-backed string comparison.
# These are not imported from stringr.

compare_marked_string <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_compare_unmaterialized <- function(x) {
  expect_altrep_unmaterialized(x)
}

compare_condition_events <- function(expr) {
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

test_that("collated equivalence matches stringi", {
  left_values <- c("ä", "a", "2", "é", "u\u0308", "", NA_character_)
  right_values <- c("a", "ä", "10", "e\u0301", "ü", "x", "a")
  left <- charport::as_charvec(left_values)
  right <- charport::as_charvec(right_values)
  opts <- list(
    locale = "de", strength = 1L, numeric = TRUE,
    normalization = TRUE
  )

  expect_identical(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_cmp_equiv")(left, right, opts_collator = opts)
    ),
    stringi::stri_cmp_equiv(left_values, right_values, opts_collator = opts)
  )
  expect_compare_unmaterialized(left)
  expect_compare_unmaterialized(right)
})

test_that("equivalence containers share an exact aliased input safely", {
  values <- c("a", "ä", "same", "", NA_character_)
  shared <- charport::as_charvec(values)
  opts <- list(locale = "de", strength = 2L)

  expect_identical(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_cmp_equiv")(shared, shared, opts_collator = opts)
    ),
    stringi::stri_cmp_equiv(values, values, opts_collator = opts)
  )
  expect_compare_unmaterialized(shared)
})

test_that("equivalence consumes a previous unmaterialized result", {
  source <- charport::as_charvec(c("ä", NA_character_, "A"))
  replacement <- charport::as_charvec("a")
  left <- with_test_backend(
    TRUE,
    charr_test_leaf("ci_replace_na")(source, replacement)
  )
  right <- charport::as_charvec(c("a", "a", "a"))
  expect_compare_unmaterialized(left)

  opts <- list(locale = "de", strength = 1L)
  expect_identical(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_cmp_equiv")(left, right, opts_collator = opts)
    ),
    stringi::stri_cmp_equiv(
      c("ä", "a", "A"), c("a", "a", "a"),
      opts_collator = opts
    )
  )
  expect_compare_unmaterialized(left)
  expect_compare_unmaterialized(right)
  expect_compare_unmaterialized(source)
  expect_compare_unmaterialized(replacement)
})

test_that("equivalence preserves UTF-8 normalization and byte lengths", {
  latin1 <- compare_marked_string(c(0x63, 0x61, 0x66, 0xe9), "latin1")
  malformed <- compare_marked_string(c(0x61, 0xff, 0x62), "UTF-8")
  native <- compare_marked_string(
    c(0x63, 0x61, 0x66, 0xc3, 0xa9), "unknown"
  )
  left_values <- c(latin1, "\ufeffabc", malformed, native)
  right_values <- c("café", "abc", "a�b", "café")
  left <- charport::as_charvec(left_values)
  right <- charport::as_charvec(right_values)
  opts <- list(locale = "en", strength = 3L)

  expect_identical(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_cmp_equiv")(left, right, opts_collator = opts)
    ),
    stringi::stri_cmp_equiv(
      left_values, right_values, opts_collator = opts
    )
  )
  expect_compare_unmaterialized(left)
  expect_compare_unmaterialized(right)
})

test_that("equivalence eagerly rejects bytes records", {
  bytes <- compare_marked_string(c(0xff, 0xfe), "bytes")
  left <- charport::as_charvec(c(NA_character_, bytes))
  right <- charport::as_charvec(c("a", "b"))
  opts <- list(bogus = TRUE)

  expect_identical(
    compare_condition_events(
      with_test_backend(
        TRUE,
        charr_test_leaf("ci_cmp_equiv")(left, right, opts_collator = opts)
      )
    ),
    compare_condition_events(
      stringi::stri_cmp_equiv(
        c(NA_character_, bytes), c("a", "b"),
        opts_collator = opts
      )
    )
  )
  expect_compare_unmaterialized(left)
  expect_compare_unmaterialized(right)
})

test_that("equivalence preserves recycling and collator option order", {
  left_values <- c("a", "b", "c")
  right_values <- c("a", "b")
  left <- charport::as_charvec(left_values)
  right <- charport::as_charvec(right_values)
  opts <- list(bogus = TRUE)

  expect_identical(
    compare_condition_events(
      with_test_backend(
        TRUE,
        charr_test_leaf("ci_cmp_equiv")(left, right, opts_collator = opts)
      )
    ),
    compare_condition_events(
      stringi::stri_cmp_equiv(
        left_values, right_values, opts_collator = opts
      )
    )
  )
  expect_identical(
    suppressWarnings(
      with_test_backend(
        TRUE,
        charr_test_leaf("ci_cmp_equiv")(left, right, opts_collator = opts)
      )
    ),
    suppressWarnings(
      stringi::stri_cmp_equiv(
        left_values, right_values, opts_collator = opts
      )
    )
  )
  expect_compare_unmaterialized(left)
  expect_compare_unmaterialized(right)
})

test_that("empty equivalence inputs preserve zero-length recycling", {
  empty <- charport::as_charvec(character())
  other <- charport::as_charvec("a")

  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_cmp_equiv")(empty, other)),
    stringi::stri_cmp_equiv(character(), "a")
  )
  expect_compare_unmaterialized(empty)
  expect_compare_unmaterialized(other)
})
