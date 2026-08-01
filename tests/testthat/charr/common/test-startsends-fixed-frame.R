startsends_fixed_frame_function <- function(backend, operation) {
  leaf <- switch(
    operation,
    starts = "ci_startswith_fixed",
    ends = "ci_endswith_fixed",
    stop("unknown fixed-position operation", call. = FALSE)
  )

  if (identical(backend, "stringi")) {
    return(getExportedValue("stringi", sub("^ci_", "stri_", leaf)))
  }
  if (identical(backend, "base")) {
    return(get(
      leaf,
      envir = charr:::.charr_base_leaf_environment,
      inherits = FALSE
    ))
  }
  if (identical(backend, "altrep")) {
    return(get(leaf, envir = asNamespace("charr"), inherits = FALSE))
  }

  stop("unknown fixed-position backend", call. = FALSE)
}


startsends_fixed_frame_inputs <- function(backend, subject, pattern) {
  if (identical(backend, "altrep")) {
    subject <- charport::as_charvec(subject)
    pattern <- charport::as_charvec(pattern)
  }
  list(subject = subject, pattern = pattern)
}


startsends_fixed_frame_invoke <- function(
    backend, operation, inputs, position, negate = FALSE, opts_fixed = NULL
) {
  fun <- startsends_fixed_frame_function(backend, operation)
  if (identical(operation, "starts")) {
    return(fun(
      inputs$subject, inputs$pattern, from = position,
      negate = negate, opts_fixed = opts_fixed
    ))
  }
  fun(
    inputs$subject, inputs$pattern, to = position,
    negate = negate, opts_fixed = opts_fixed
  )
}


expect_startsends_fixed_frame_unmaterialized <- function(backend, inputs) {
  if (!identical(backend, "altrep")) {
    return(invisible(NULL))
  }

  expect_true(charport::is_charvec(inputs$subject))
  expect_false(charport::charport_info(inputs$subject)$is_materialized)
  expect_true(charport::is_charvec(inputs$pattern))
  expect_false(charport::charport_info(inputs$pattern)$is_materialized)
  invisible(NULL)
}


startsends_fixed_frame_events <- function(expr, warning_handler = NULL) {
  events <- character()
  value <- tryCatch(
    withCallingHandlers(
      force(expr),
      warning = function(condition) {
        events <<- c(events, paste0("warning:", conditionMessage(condition)))
        if (!is.null(warning_handler)) {
          warning_handler(condition)
        }
        invokeRestart("muffleWarning")
      }
    ),
    error = function(condition) {
      events <<- c(events, paste0("error:", conditionMessage(condition)))
      NULL
    }
  )
  list(value = value, events = events)
}


startsends_fixed_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


startsends_fixed_frame_default_position <- function(operation) {
  if (identical(operation, "starts")) 1L else -1L
}


startsends_fixed_frame_valid_case <- function(operation) {
  if (identical(operation, "starts")) {
    return(list(
      subject = c("alpha", "beta"), pattern = c("a", "b"),
      position = 1L, expected = c(TRUE, TRUE)
    ))
  }
  list(
    subject = c("alpha", "beta"), pattern = "a",
    position = -1L, expected = c(TRUE, TRUE)
  )
}


expect_startsends_fixed_frame_valid <- function(backend, operation) {
  valid <- startsends_fixed_frame_valid_case(operation)
  inputs <- startsends_fixed_frame_inputs(
    backend, valid$subject, valid$pattern
  )
  expect_identical(
    startsends_fixed_frame_invoke(
      backend, operation, inputs, valid$position
    ),
    valid$expected,
    info = paste(backend, operation)
  )
  expect_startsends_fixed_frame_unmaterialized(backend, inputs)
}


expect_startsends_fixed_frame_parity <- function(
    operation, subject, pattern, position, expected = NULL,
    opts_fixed = NULL, negate = FALSE
) {
  oracle_inputs <- startsends_fixed_frame_inputs(
    "stringi", subject, pattern
  )
  oracle <- startsends_fixed_frame_invoke(
    "stringi", operation, oracle_inputs, position,
    negate = negate, opts_fixed = opts_fixed
  )
  if (!is.null(expected)) {
    expect_identical(oracle, expected)
  }

  for (backend in c("base", "altrep")) {
    inputs <- startsends_fixed_frame_inputs(backend, subject, pattern)
    actual <- startsends_fixed_frame_invoke(
      backend, operation, inputs, position,
      negate = negate, opts_fixed = opts_fixed
    )
    expect_identical(actual, oracle, info = paste(backend, operation))
    expect_startsends_fixed_frame_unmaterialized(backend, inputs)
  }
  invisible(oracle)
}


expect_startsends_fixed_frame_event_parity <- function(
    operation, subject, pattern, position,
    expected_value = NULL, expected_events = NULL, opts_fixed = NULL
) {
  oracle_inputs <- startsends_fixed_frame_inputs(
    "stringi", subject, pattern
  )
  oracle <- startsends_fixed_frame_events(
    startsends_fixed_frame_invoke(
      "stringi", operation, oracle_inputs, position,
      opts_fixed = opts_fixed
    )
  )
  if (!is.null(expected_value)) {
    expect_identical(oracle$value, expected_value)
  }
  if (!is.null(expected_events)) {
    expect_identical(oracle$events, expected_events)
  }

  for (backend in c("base", "altrep")) {
    inputs <- startsends_fixed_frame_inputs(backend, subject, pattern)
    actual <- startsends_fixed_frame_events(
      startsends_fixed_frame_invoke(
        backend, operation, inputs, position,
        opts_fixed = opts_fixed
      )
    )
    expect_identical(actual, oracle, info = paste(backend, operation))
    expect_startsends_fixed_frame_unmaterialized(backend, inputs)
  }
  invisible(oracle)
}


test_that("fixed starts and ends preserve Frame parity", {
  latin1 <- startsends_fixed_frame_marked(c(0x61, 0xe9, 0x61), "latin1")
  malformed <- startsends_fixed_frame_marked(c(0x61, 0xff, 0x61), "UTF-8")
  bom_a <- startsends_fixed_frame_marked(c(0xef, 0xbb, 0xbf, 0x61), "UTF-8")
  subjects <- c("aba", latin1, malformed, bom_a, NA_character_, "")
  expected <- c(TRUE, TRUE, TRUE, TRUE, NA, FALSE)

  for (operation in c("starts", "ends")) {
    position <- startsends_fixed_frame_default_position(operation)
    expect_startsends_fixed_frame_parity(
      operation, subjects, "a", position, expected
    )
  }
})


test_that("fixed starts and ends use code-point positions", {
  value <- "a\u00e9\U0001f600bc\u00e9"
  maximum <- .Machine$integer.max
  cases <- list(
    starts = list(
      pattern = c(
        "a", "\u00e9\U0001f600", "\U0001f600b", "c\u00e9", "a\u00e9", "\u00e9",
        "z", "a", "\u00e9", "a", "a", "\u00e9\u00e9"
      ),
      position = c(
        1L, 2L, 3L, -2L, -99L, -1L,
        99L, 0L, NA_integer_, maximum, -maximum, 6L
      ),
      expected = c(
        TRUE, TRUE, TRUE, TRUE, TRUE, TRUE,
        FALSE, TRUE, NA, FALSE, TRUE, FALSE
      )
    ),
    ends = list(
      pattern = c(
        "a", "a\u00e9", "\u00e9\U0001f600", "bc", "a", "c\u00e9",
        "a\u00e9\U0001f600bc\u00e9", "a", "\u00e9", "\u00e9", "a", "\u00e9\u00e9"
      ),
      position = c(
        1L, 2L, 3L, -2L, -99L, -1L,
        99L, 0L, NA_integer_, maximum, -maximum, 6L
      ),
      expected = c(
        TRUE, TRUE, TRUE, TRUE, FALSE, TRUE,
        TRUE, FALSE, NA, TRUE, FALSE, FALSE
      )
    )
  )

  for (operation in names(cases)) {
    case <- cases[[operation]]
    expect_startsends_fixed_frame_parity(
      operation, value, case$pattern, case$position, case$expected
    )
    expect_startsends_fixed_frame_parity(
      operation, "", "a", NA_integer_, FALSE
    )
    expect_startsends_fixed_frame_parity(
      operation, "", "a", NA_integer_, TRUE, negate = TRUE
    )
  }
})


test_that("fixed starts and ends preserve pattern encodings", {
  latin1_e <- startsends_fixed_frame_marked(0xe9, "latin1")
  bom_a <- startsends_fixed_frame_marked(c(0xef, 0xbb, 0xbf, 0x61), "UTF-8")
  malformed <- startsends_fixed_frame_marked(c(0x61, 0xff, 0x62), "UTF-8")
  malformed_pattern <- startsends_fixed_frame_marked(0xff, "UTF-8")
  opts <- list(case_insensitive = TRUE)

  cases <- list(
    starts = list(
      subject = c("\u00c9x", "ax"), pattern = c(latin1_e, bom_a),
      position = 1L
    ),
    ends = list(
      subject = c("x\u00c9", "xa"), pattern = c(latin1_e, bom_a),
      position = -1L
    )
  )
  if (isTRUE(l10n_info()[["UTF-8"]])) {
    native_e <- startsends_fixed_frame_marked(c(0xc3, 0xa9), "unknown")
    cases$starts$subject <- c(cases$starts$subject, "\u00e9x")
    cases$starts$pattern <- c(cases$starts$pattern, native_e)
    cases$ends$subject <- c(cases$ends$subject, "x\u00e9")
    cases$ends$pattern <- c(cases$ends$pattern, native_e)
  }

  for (operation in names(cases)) {
    case <- cases[[operation]]
    expect_startsends_fixed_frame_parity(
      operation, case$subject, case$pattern, case$position,
      rep(TRUE, length(case$subject)), opts_fixed = opts
    )
    expect_startsends_fixed_frame_parity(
      operation, malformed, malformed_pattern, 2L, TRUE
    )
  }
})


test_that("fixed starts and ends preserve warning and error order", {
  recycling <- paste0(
    "warning:longer object length is not a multiple of shorter object length"
  )
  empty <- "warning:empty search patterns are not supported"
  option <- "warning:incorrect opts_fixed setting: 'overlap'; ignoring"
  bytes_error <- "error:bytes encoding is not supported by this function"
  bytes <- startsends_fixed_frame_marked(c(0xff, 0xfe), "bytes")

  for (operation in c("starts", "ends")) {
    warning_position <- if (identical(operation, "starts")) {
      c(1L, 2L)
    } else {
      c(-1L, -2L)
    }
    expect_startsends_fixed_frame_event_parity(
      operation,
      c("a", "b", "a"), c("", "", "a"), warning_position,
      expected_value = c(NA, NA, TRUE),
      expected_events = c(option, recycling, empty, empty),
      opts_fixed = list(overlap = TRUE)
    )

    error_cases <- list(
      list(
        subject = c(bytes, "a", "a"), pattern = c("a", "b"),
        position = startsends_fixed_frame_default_position(operation),
        events = c(recycling, bytes_error)
      ),
      list(
        subject = bytes, pattern = "",
        position = startsends_fixed_frame_default_position(operation),
        events = bytes_error
      ),
      list(
        subject = "abc", pattern = bytes,
        position = if (identical(operation, "starts")) NA_integer_ else 0L,
        events = bytes_error
      )
    )

    for (case in error_cases) {
      expect_startsends_fixed_frame_event_parity(
        operation, case$subject, case$pattern, case$position,
        expected_events = case$events
      )
      for (backend in c("base", "altrep")) {
        expect_startsends_fixed_frame_valid(backend, operation)
      }
    }
  }
})


test_that("optimized fixed starts and ends validate native input and recover", {
  skip_if_not(l10n_info()[["UTF-8"]])
  bad_native <- startsends_fixed_frame_marked(0xff, "unknown")

  # Stringi accepts this byte on a UTF-8 locale. The optimized backends
  # validate native-marked input, so this case deliberately has no oracle.

  for (operation in c("starts", "ends")) {
    cases <- list(
      list(subject = bad_native, pattern = "a"),
      list(subject = "abc", pattern = bad_native)
    )
    position <- startsends_fixed_frame_default_position(operation)

    for (case in cases) {
      for (backend in c("base", "altrep")) {
        inputs <- startsends_fixed_frame_inputs(
          backend, case$subject, case$pattern
        )
        actual <- startsends_fixed_frame_events(
          startsends_fixed_frame_invoke(
            backend, operation, inputs, position
          )
        )
        expect_length(actual$events, 1L)
        expect_match(
          actual$events,
          "^error:failed to convert R native encoding to UTF-8",
          info = paste(backend, operation)
        )
        expect_startsends_fixed_frame_unmaterialized(backend, inputs)

        expect_startsends_fixed_frame_valid(backend, operation)
      }
    }
  }
})


test_that("fixed starts and ends recover when warnings are errors", {
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (operation in c("starts", "ends")) {
    position <- startsends_fixed_frame_default_position(operation)
    for (backend in c("base", "altrep")) {
      invalid <- startsends_fixed_frame_inputs(backend, "abc", "")
      expect_error(
        startsends_fixed_frame_invoke(
          backend, operation, invalid, position
        ),
        "empty search patterns are not supported",
        fixed = TRUE,
        info = paste(backend, operation)
      )
      expect_startsends_fixed_frame_unmaterialized(backend, invalid)

      expect_startsends_fixed_frame_valid(backend, operation)
    }
  }
})


test_that("fixed starts and ends skip inactive zero-length inputs", {
  bytes <- startsends_fixed_frame_marked(c(0xff, 0xfe), "bytes")

  for (operation in c("starts", "ends")) {
    default <- startsends_fixed_frame_default_position(operation)
    cases <- list(
      list(subject = character(), pattern = bytes, position = default),
      list(subject = bytes, pattern = character(), position = default),
      list(subject = bytes, pattern = "a", position = integer())
    )

    for (case in cases) {
      for (backend in c("stringi", "base", "altrep")) {
        inputs <- startsends_fixed_frame_inputs(
          backend, case$subject, case$pattern
        )
        actual <- startsends_fixed_frame_events(
          startsends_fixed_frame_invoke(
            backend, operation, inputs, case$position
          )
        )
        expect_identical(
          actual$value, logical(), info = paste(backend, operation)
        )
        expect_identical(
          actual$events, character(), info = paste(backend, operation)
        )
        expect_startsends_fixed_frame_unmaterialized(backend, inputs)
      }
    }
  }
})


test_that("fixed starts and ends permit reentry on the same charvec", {
  subject_values <- c("alpha", "beta", "alpha")
  pattern_values <- c("a", "b")

  for (operation in c("starts", "ends")) {
    position <- startsends_fixed_frame_default_position(operation)
    inputs <- startsends_fixed_frame_inputs(
      "altrep", subject_values, pattern_values
    )
    scalar_pattern <- charport::as_charvec("a")
    reentered <- NULL

    oracle_inputs <- startsends_fixed_frame_inputs(
      "stringi", subject_values, pattern_values
    )
    oracle <- startsends_fixed_frame_events(
      startsends_fixed_frame_invoke(
        "stringi", operation, oracle_inputs, position
      )
    )
    inner_inputs <- startsends_fixed_frame_inputs(
      "stringi", subject_values, "a"
    )
    inner <- startsends_fixed_frame_invoke(
      "stringi", operation, inner_inputs, position
    )

    actual <- startsends_fixed_frame_events(
      startsends_fixed_frame_invoke(
        "altrep", operation, inputs, position
      ),
      warning_handler = function(condition) {
        reentry_inputs <- list(
          subject = inputs$subject,
          pattern = scalar_pattern
        )
        reentered <<- startsends_fixed_frame_invoke(
          "altrep", operation, reentry_inputs, position
        )
      }
    )

    expect_identical(actual, oracle)
    expect_identical(reentered, inner)
    expect_startsends_fixed_frame_unmaterialized("altrep", inputs)
    expect_false(charport::charport_info(scalar_pattern)$is_materialized)
  }
})
