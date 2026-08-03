detect_fixed_frame_function <- function(backend) {
  if (identical(backend, "stringi")) {
    return(stringi::stri_detect_fixed)
  }
  if (identical(backend, "base")) {
    return(get(
      "ci_detect_fixed",
      envir = charr:::.charr_base_leaf_environment,
      inherits = FALSE
    ))
  }
  if (identical(backend, "altrep")) {
    return(charr:::ci_detect_fixed)
  }

  stop("unknown fixed-detect backend", call. = FALSE)
}


detect_fixed_frame_inputs <- function(backend, subject, pattern) {
  if (identical(backend, "altrep")) {
    subject <- charport::as_charvec(subject)
    pattern <- charport::as_charvec(pattern)
  }
  list(subject = subject, pattern = pattern)
}


detect_fixed_frame_invoke <- function(
    backend, inputs, negate = FALSE, max_count = -1L, opts_fixed = NULL
) {
  detect_fixed_frame_function(backend)(
    inputs$subject, inputs$pattern,
    negate = negate, max_count = max_count, opts_fixed = opts_fixed
  )
}


expect_detect_fixed_frame_unmaterialized <- function(backend, inputs) {
  if (!identical(backend, "altrep")) {
    return(invisible(NULL))
  }

  expect_true(charport::is_charvec(inputs$subject))
  expect_false(charport::charport_info(inputs$subject)$is_materialized)
  expect_true(charport::is_charvec(inputs$pattern))
  expect_false(charport::charport_info(inputs$pattern)$is_materialized)
  invisible(NULL)
}


detect_fixed_frame_events <- function(expr, warning_handler = NULL) {
  events <- character()
  value <- tryCatch(
    withCallingHandlers(
      force(expr),
      warning = function(condition) {
        events <<- c(events, "warning")
        if (!is.null(warning_handler)) {
          warning_handler(condition)
        }
        invokeRestart("muffleWarning")
      }
    ),
    error = function(condition) {
      events <<- c(events, "error")
      NULL
    }
  )
  list(value = value, events = events)
}


detect_fixed_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


test_that("fixed-detect Frame paths agree across matcher transitions", {
  latin1 <- detect_fixed_frame_marked(c(0x61, 0xe9, 0x61), "latin1")
  malformed <- detect_fixed_frame_marked(c(0x61, 0xff, 0x61), "UTF-8")
  long_pattern <- "0123456789abcdef"
  cases <- list(
    list(
      subject = c("aaaa", malformed, latin1, NA_character_, ""),
      pattern = "a",
      opts = NULL,
      expected = c(TRUE, TRUE, TRUE, NA, FALSE)
    ),
    list(
      subject = c(
        "a", "ttthe", paste0("prefix", long_pattern, "suffix")
      ),
      pattern = c("a", "the", long_pattern),
      opts = NULL,
      expected = rep(TRUE, 3L)
    ),
    list(
      subject = c("Alpha", "BETA", "\u00e9"),
      pattern = "a",
      opts = list(case_insensitive = TRUE),
      expected = c(TRUE, TRUE, FALSE)
    )
  )

  for (case in cases) {
    oracle_inputs <- detect_fixed_frame_inputs(
      "stringi", case$subject, case$pattern
    )
    oracle <- detect_fixed_frame_invoke(
      "stringi", oracle_inputs, opts_fixed = case$opts
    )
    expect_identical(oracle, case$expected)

    for (backend in c("base", "altrep")) {
      inputs <- detect_fixed_frame_inputs(
        backend, case$subject, case$pattern
      )
      actual <- detect_fixed_frame_invoke(
        backend, inputs, opts_fixed = case$opts
      )

      expect_detect_fixed_frame_unmaterialized(backend, inputs)
      expect_identical(actual, oracle, info = backend)
    }
  }
})


test_that("fixed-detect preserves grouped max-count traversal", {
  cases <- list(
    list(
      subject = c("hit", "miss", "hit", "miss", "hit", "miss"),
      pattern = c("hit", "x"),
      negate = FALSE,
      max_count = 1L,
      expected = c(TRUE, NA, NA, NA, NA, NA)
    ),
    list(
      subject = c("hit", "", "miss", "hit"),
      pattern = c("hit", "x"),
      negate = TRUE,
      max_count = 2L,
      expected = c(FALSE, TRUE, TRUE, NA)
    )
  )
  if (isTRUE(l10n_info()[["UTF-8"]])) {
    bad_native <- detect_fixed_frame_marked(
      c(0x61, 0xff, 0x62), "unknown"
    )
    bad_pattern <- detect_fixed_frame_marked(0xff, "unknown")
    cases <- c(
      cases,
      list(
        list(
          subject = c("hit", bad_native), pattern = "hit",
          negate = FALSE, max_count = 1L,
          expected = c(TRUE, NA)
        ),
        list(
          subject = bad_native, pattern = bad_pattern,
          negate = FALSE, max_count = -1L,
          expected = TRUE
        )
      )
    )
  }

  for (case in cases) {
    oracle_inputs <- detect_fixed_frame_inputs(
      "stringi", case$subject, case$pattern
    )
    oracle <- detect_fixed_frame_invoke(
      "stringi", oracle_inputs,
      negate = case$negate, max_count = case$max_count
    )
    expect_identical(oracle, case$expected)

    for (backend in c("base", "altrep")) {
      inputs <- detect_fixed_frame_inputs(
        backend, case$subject, case$pattern
      )
      actual <- detect_fixed_frame_invoke(
        backend, inputs,
        negate = case$negate, max_count = case$max_count
      )

      expect_detect_fixed_frame_unmaterialized(backend, inputs)
      expect_identical(actual, oracle, info = backend)
    }
  }
})


test_that("fixed-detect input errors are not hidden and permit recovery", {
  bytes <- detect_fixed_frame_marked(c(0xff, 0xfe), "bytes")
  scenarios <- list(
    list(
      subject = c(bytes, "a", "a"), pattern = c("a", "b"),
      max_count = -1L, events = c("warning", "error")
    ),
    list(subject = bytes, pattern = "a", max_count = 0L, events = "error"),
    list(
      subject = c("hit", bytes), pattern = "hit",
      max_count = 1L, events = "error"
    ),
    list(subject = bytes, pattern = "", max_count = -1L, events = "error"),
    list(subject = "abc", pattern = bytes, max_count = -1L, events = "error")
  )

  for (scenario in scenarios) {
    oracle_inputs <- detect_fixed_frame_inputs(
      "stringi", scenario$subject, scenario$pattern
    )
    oracle <- detect_fixed_frame_events(detect_fixed_frame_invoke(
      "stringi", oracle_inputs, max_count = scenario$max_count
    ))
    expected_events <- oracle$events
    expect_identical(expected_events, scenario$events)

    for (backend in c("base", "altrep")) {
      inputs <- detect_fixed_frame_inputs(
        backend, scenario$subject, scenario$pattern
      )
      actual <- detect_fixed_frame_events(detect_fixed_frame_invoke(
        backend, inputs, max_count = scenario$max_count
      ))

      expect_identical(actual$events, expected_events, info = backend)
      expect_detect_fixed_frame_unmaterialized(backend, inputs)

      valid <- detect_fixed_frame_inputs(backend, c("alpha", "beta"), "a")
      expect_identical(
        detect_fixed_frame_invoke(backend, valid),
        c(TRUE, TRUE),
        info = backend
      )
      expect_detect_fixed_frame_unmaterialized(backend, valid)
    }
  }
})


test_that("fixed-detect warning errors leave the Frame reusable", {
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (backend in c("base", "altrep")) {
    invalid <- detect_fixed_frame_inputs(backend, "abc", "")
    expect_error(
      detect_fixed_frame_invoke(backend, invalid),
      "empty search patterns are not supported",
      fixed = TRUE,
      info = backend
    )
    expect_detect_fixed_frame_unmaterialized(backend, invalid)

    valid <- detect_fixed_frame_inputs(backend, c("alpha", "beta"), "a")
    expect_identical(
      detect_fixed_frame_invoke(backend, valid),
      c(TRUE, TRUE),
      info = backend
    )
    expect_detect_fixed_frame_unmaterialized(backend, valid)
  }
})


test_that("fixed-detect warnings permit reentry on the same charvec", {
  inputs <- detect_fixed_frame_inputs(
    "altrep", c("aaaa", "bbbb", "cccc"), c("a", "b")
  )
  scalar_pattern <- charport::as_charvec("a")
  reentered <- NULL

  actual <- detect_fixed_frame_events(
    detect_fixed_frame_invoke("altrep", inputs),
    warning_handler = function(condition) {
      reentered <<- charr:::ci_detect_fixed(inputs$subject, scalar_pattern)
    }
  )

  expect_identical(actual$events, "warning")
  expect_identical(actual$value, c(TRUE, TRUE, FALSE))
  expect_identical(reentered, c(TRUE, FALSE, FALSE))
  expect_detect_fixed_frame_unmaterialized("altrep", inputs)
  expect_false(charport::charport_info(scalar_pattern)$is_materialized)
})


test_that("fixed-detect zero recycling does not inspect either input", {
  bytes <- detect_fixed_frame_marked(c(0xff, 0xfe), "bytes")
  cases <- list(
    list(subject = character(), pattern = bytes),
    list(subject = bytes, pattern = character())
  )

  for (case in cases) {
    for (backend in c("stringi", "base", "altrep")) {
      inputs <- detect_fixed_frame_inputs(
        backend, case$subject, case$pattern
      )
      expect_identical(
        detect_fixed_frame_invoke(backend, inputs),
        logical(),
        info = backend
      )
      expect_detect_fixed_frame_unmaterialized(backend, inputs)
    }
  }
})


test_that("fixed-detect preserves empty-pattern warning multiplicity", {
  subject <- c("a", "b", "a")
  pattern <- c("", "", "a")

  for (backend in c("stringi", "base", "altrep")) {
    inputs <- detect_fixed_frame_inputs(backend, subject, pattern)
    actual <- detect_fixed_frame_events(
      detect_fixed_frame_invoke(backend, inputs)
    )

    expect_identical(actual$events, c("warning", "warning"), info = backend)
    expect_identical(actual$value, c(NA, NA, TRUE))
    expect_detect_fixed_frame_unmaterialized(backend, inputs)
  }
})
