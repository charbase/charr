# Charr-owned tests for Reader-backed boundary counting and locating. These are
# not imported from stringr.

boundary_position_count <- function(...) {
  charr:::ci_count_boundaries(...)
}

boundary_position_first <- function(...) {
  charr:::ci_locate_first_boundaries(...)
}

boundary_position_all <- function(...) {
  charr:::ci_locate_all_boundaries(...)
}

boundary_position_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}

expect_boundary_position_unmaterialized <- function(x) {
  expect_true(charport::is_charvec(x))
  expect_false(charport::charport_info(x)$is_materialized)
}

boundary_position_events <- function(expr) {
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

test_that("boundary counts consume direct and chained unmaterialized inputs", {
  values <- c(
    "\U0001f469\u200d\U0001f469\u200d\U0001f467\u200d\U0001f466e\u0301",
    "alpha beta 123", "One. Two!", "", NA_character_
  )
  direct <- charport::as_charvec(values)
  chained <- with_altrep(TRUE, str_trim(direct))
  option_cases <- list(
    list(type = "character"),
    list(type = "word", skip_word_none = TRUE),
    list(type = "sentence")
  )

  for (subject in list(direct, chained)) {
    for (opts in option_cases) {
      expect_identical(
        with_altrep(
          TRUE,
          boundary_position_count(subject, opts_brkiter = opts)
        ),
        stringi::stri_count_boundaries(values, opts_brkiter = opts)
      )
    }
    expect_boundary_position_unmaterialized(subject)
  }
})

test_that("boundary locate preserves code-point positions and iterator rules", {
  cases <- list(
    list(
      values = c(
        "\U0001f469\u200d\U0001f469\u200d\U0001f467\u200d\U0001f466e\u0301",
        "\U0001f600x"
      ),
      opts = list(type = "character")
    ),
    list(
      values = c("alpha beta", "go 123"),
      opts = list(type = "word", skip_word_none = TRUE)
    ),
    list(
      values = c("One. Two!", "Three? Four."),
      opts = list(type = "sentence")
    )
  )

  for (case in cases) {
    subject <- charport::as_charvec(case$values)
    for (get_length in c(FALSE, TRUE)) {
      expect_identical(
        with_altrep(
          TRUE,
          boundary_position_first(
            subject, get_length = get_length,
            opts_brkiter = case$opts
          )
        ),
        stringi::stri_locate_first_boundaries(
          case$values, get_length = get_length,
          opts_brkiter = case$opts
        )
      )
      expect_identical(
        with_altrep(
          TRUE,
          boundary_position_all(
            subject, get_length = get_length,
            opts_brkiter = case$opts
          )
        ),
        stringi::stri_locate_all_boundaries(
          case$values, get_length = get_length,
          opts_brkiter = case$opts
        )
      )
    }
    expect_boundary_position_unmaterialized(subject)
  }

  emoji_direct <- charport::as_charvec(
    "\U0001f469\u200d\U0001f469\u200d\U0001f467\u200d\U0001f466e\u0301"
  )
  emoji <- with_altrep(TRUE, str_trim(emoji_direct))
  actual <- with_altrep(
    TRUE,
    boundary_position_all(
      emoji, opts_brkiter = list(type = "character")
    )
  )
  expect_identical(
    actual[[1L]],
    cbind(start = c(1L, 8L), end = c(7L, 9L))
  )
  expect_boundary_position_unmaterialized(emoji_direct)
  expect_boundary_position_unmaterialized(emoji)
})

test_that("boundary positions preserve missing and no-match shapes", {
  values <- c("", NA_character_, "abc")
  subject <- charport::as_charvec(values)
  opts <- list(
    type = "word", skip_word_none = TRUE, skip_word_letter = TRUE
  )

  expect_identical(
    with_altrep(
      TRUE,
      boundary_position_count(subject, opts_brkiter = opts)
    ),
    stringi::stri_count_boundaries(values, opts_brkiter = opts)
  )

  for (get_length in c(FALSE, TRUE)) {
    expect_identical(
      with_altrep(
        TRUE,
        boundary_position_first(
          subject, get_length = get_length, opts_brkiter = opts
        )
      ),
      stringi::stri_locate_first_boundaries(
        values, get_length = get_length, opts_brkiter = opts
      )
    )
    for (omit_no_match in c(FALSE, TRUE)) {
      expect_identical(
        with_altrep(
          TRUE,
          boundary_position_all(
            subject,
            omit_no_match = omit_no_match,
            get_length = get_length,
            opts_brkiter = opts
          )
        ),
        stringi::stri_locate_all_boundaries(
          values,
          omit_no_match = omit_no_match,
          get_length = get_length,
          opts_brkiter = opts
        )
      )
    }
  }

  omitted <- with_altrep(
    TRUE,
    boundary_position_all(
      subject, omit_no_match = TRUE, get_length = TRUE,
      opts_brkiter = opts
    )
  )
  expect_identical(dim(omitted[[1L]]), c(0L, 2L))
  expect_identical(
    omitted[[2L]],
    cbind(start = NA_integer_, length = NA_integer_)
  )
  expect_identical(dim(omitted[[3L]]), c(0L, 2L))
  expect_boundary_position_unmaterialized(subject)
})

test_that("boundary iterator opening keeps copied option and error order", {
  values <- c("", NA_character_)
  subject <- charport::as_charvec(values)
  bad_rules <- list(type = "[")

  expect_identical(
    boundary_position_events(
      stringi::stri_locate_first_boundaries(
        values, opts_brkiter = bad_rules
      )
    ),
    boundary_position_events(
      with_altrep(
        TRUE,
        boundary_position_first(subject, opts_brkiter = bad_rules)
      )
    )
  )
  expect_identical(
    boundary_position_events(
      stringi::stri_count_boundaries(values, opts_brkiter = bad_rules)
    ),
    boundary_position_events(
      with_altrep(
        TRUE,
        boundary_position_count(subject, opts_brkiter = bad_rules)
      )
    )
  )
  expect_identical(
    boundary_position_events(
      stringi::stri_locate_all_boundaries(
        values, opts_brkiter = bad_rules
      )
    ),
    boundary_position_events(
      with_altrep(
        TRUE,
        boundary_position_all(subject, opts_brkiter = bad_rules)
      )
    )
  )

  bad_opts <- 1L
  expected <- boundary_position_events(
    stringi::stri_count_boundaries(values, opts_brkiter = bad_opts)
  )
  actual <- boundary_position_events(
    with_altrep(
      TRUE,
      boundary_position_count(subject, opts_brkiter = bad_opts)
    )
  )
  expect_identical(
    sub("stri_opts_brkiter", "ci_opts_brkiter", expected, fixed = TRUE),
    actual
  )
  expected <- boundary_position_events(
    stringi::stri_locate_all_boundaries(
      values, opts_brkiter = bad_opts
    )
  )
  actual <- boundary_position_events(
    with_altrep(
      TRUE,
      boundary_position_all(subject, opts_brkiter = bad_opts)
    )
  )
  expect_identical(
    sub("stri_opts_brkiter", "ci_opts_brkiter", expected, fixed = TRUE),
    actual
  )

  fallback <- charport::as_charvec("abc")
  opts <- list(type = "word", locale = "zz_ZZ")
  expected <- boundary_position_events(
    stringi::stri_count_boundaries("abc", opts_brkiter = opts)
  )
  actual <- boundary_position_events(
    with_altrep(
      TRUE,
      boundary_position_count(fallback, opts_brkiter = opts)
    )
  )
  expect_identical(actual, expected)
  expect_length(actual, 1L)
  expect_match(tolower(actual), "resource bundle")

  expect_boundary_position_unmaterialized(subject)
  expect_boundary_position_unmaterialized(fallback)
})

test_that("boundary positions preserve encoding normalization and bytes errors", {
  latin1 <- boundary_position_marked(
    c(0x63, 0x61, 0x66, 0xe9), "latin1"
  )
  malformed <- boundary_position_marked(c(0x61, 0xff, 0x62), "UTF-8")
  values <- c(latin1, "\ufeffabc", malformed)
  subject <- charport::as_charvec(values)
  opts <- list(type = "character")

  expect_identical(
    with_altrep(
      TRUE,
      boundary_position_count(subject, opts_brkiter = opts)
    ),
    stringi::stri_count_boundaries(values, opts_brkiter = opts)
  )
  expect_identical(
    with_altrep(
      TRUE,
      boundary_position_first(subject, opts_brkiter = opts)
    ),
    stringi::stri_locate_first_boundaries(
      values, opts_brkiter = opts
    )
  )
  expect_identical(
    with_altrep(
      TRUE,
      boundary_position_all(subject, opts_brkiter = opts)
    ),
    stringi::stri_locate_all_boundaries(values, opts_brkiter = opts)
  )
  expect_boundary_position_unmaterialized(subject)

  bytes <- boundary_position_marked(c(0xff, 0xfe), "bytes")
  bytes_input <- charport::as_charvec(bytes)
  expect_error(
    with_altrep(TRUE, boundary_position_count(bytes_input)),
    "bytes encoding"
  )
  expect_error(
    with_altrep(TRUE, boundary_position_first(bytes_input)),
    "bytes encoding"
  )
  expect_error(
    with_altrep(TRUE, boundary_position_all(bytes_input)),
    "bytes encoding"
  )
  expect_boundary_position_unmaterialized(bytes_input)
})
