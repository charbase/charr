# Charr-owned tests for stringr's Reader-backed collation helpers.
# These are not imported from stringr.

sort_backend_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_sort_source_unmaterialized <- function(x) {
  expect_altrep_unmaterialized(x)
}

sort_backend_events <- function(expr) {
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

test_that("order and rank preserve stable collation and NA placement", {
  values <- c(
    "2", "02", "10", "1", "a", "A", "\u00e4", "", NA_character_,
    NA_character_
  )
  subject <- charport::as_charvec(values)
  opts <- list(locale = "de", strength = 1L, numeric = TRUE)

  for (decreasing in c(FALSE, TRUE)) {
    for (na_last in c(FALSE, TRUE, NA)) {
      expect_identical(
        with_test_backend(
          TRUE,
          charr_test_leaf("ci_order")(
            subject, decreasing = decreasing, na_last = na_last,
            opts_collator = opts
          )
        ),
        stringi::stri_order(
          values, decreasing = decreasing, na_last = na_last,
          opts_collator = opts
        )
      )
    }
  }

  actual_rank <- with_test_backend(
    TRUE, charr_test_leaf("ci_rank")(subject, opts_collator = opts)
  )
  expect_identical(
    actual_rank,
    stringi::stri_rank(values, opts_collator = opts)
  )
  expect_identical(actual_rank[1L], actual_rank[2L])

  all_na <- charport::as_charvec(rep(NA_character_, 3L))
  expect_identical(
    with_test_backend(
      TRUE, charr_test_leaf("ci_order")(all_na, na_last = NA, opts_collator = opts)
    ),
    stringi::stri_order(
      rep(NA_character_, 3L), na_last = NA, opts_collator = opts
    )
  )
  expect_sort_source_unmaterialized(all_na)
  expect_sort_source_unmaterialized(subject)
})

test_that("duplicated preserves collation ties and missing values", {
  values <- c(
    "\u00e4", "a\u0308", "A", "a", NA_character_, "z", NA_character_,
    "Z", "\u00e4"
  )
  subject <- charport::as_charvec(values)
  opts <- list(locale = "de", strength = 1L)

  for (from_last in c(FALSE, TRUE)) {
    expect_identical(
      with_test_backend(
        TRUE,
        charr_test_leaf("ci_duplicated")(
          subject, from_last = from_last, opts_collator = opts
        )
      ),
      stringi::stri_duplicated(
        values, from_last = from_last, opts_collator = opts
      )
    )
  }

  empty <- charport::as_charvec(character())
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_order")(empty)),
    stringi::stri_order(character())
  )
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_rank")(empty)),
    stringi::stri_rank(character())
  )
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_duplicated")(empty)),
    stringi::stri_duplicated(character())
  )
  expect_sort_source_unmaterialized(empty)
  expect_sort_source_unmaterialized(subject)
})

test_that("mapped collation helpers preserve marked and malformed input", {
  latin1 <- sort_backend_marked(c(0x63, 0x61, 0x66, 0xe9), "latin1")
  malformed1 <- sort_backend_marked(c(0x61, 0xff, 0x62), "UTF-8")
  malformed2 <- sort_backend_marked(c(0x61, 0xfe, 0x62), "UTF-8")
  values <- c(
    latin1, "\ufeffabc", malformed1, malformed2, "abc", NA_character_
  )
  subject <- charport::as_charvec(values)
  opts <- list(locale = "en", strength = 3L)

  expect_identical(
    with_test_backend(
      TRUE,
      charr_test_leaf("ci_order")(subject, na_last = NA, opts_collator = opts)
    ),
    stringi::stri_order(values, na_last = NA, opts_collator = opts)
  )
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_rank")(subject, opts_collator = opts)),
    stringi::stri_rank(values, opts_collator = opts)
  )
  expect_identical(
    with_test_backend(TRUE, charr_test_leaf("ci_duplicated")(subject, opts_collator = opts)),
    stringi::stri_duplicated(values, opts_collator = opts)
  )
  expect_sort_source_unmaterialized(subject)
})

test_that("mapped collation helpers retain validation order", {
  bytes <- sort_backend_marked(c(0xff, 0xfe), "bytes")
  subject <- charport::as_charvec(bytes)

  expected <- list(
    order = function() stringi::stri_order(bytes),
    rank = function() stringi::stri_rank(bytes),
    duplicated = function() stringi::stri_duplicated(bytes)
  )
  actual <- list(
    order = function() with_test_backend(TRUE, charr_test_leaf("ci_order")(subject)),
    rank = function() with_test_backend(TRUE, charr_test_leaf("ci_rank")(subject)),
    duplicated = function() with_test_backend(TRUE, charr_test_leaf("ci_duplicated")(subject))
  )

  for (name in names(expected)) {
    expect_identical(
      sort_backend_events(expected[[name]]()),
      sort_backend_events(actual[[name]]()),
      info = name
    )
  }

  bad_opts <- list(not_a_collator_option = TRUE)
  expect_identical(
    sort_backend_events(
      stringi::stri_order(bytes, opts_collator = bad_opts)
    ),
    sort_backend_events(
      with_test_backend(
        TRUE, charr_test_leaf("ci_order")(subject, opts_collator = bad_opts)
      )
    )
  )
  expect_sort_source_unmaterialized(subject)
})
