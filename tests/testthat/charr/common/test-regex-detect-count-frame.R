regex_frame_symbol <- function(backend, operation) {
  namespace <- asNamespace("charr")
  prefix <- if (identical(backend, "base")) {
    "C_charr_base_ci_"
  } else if (identical(backend, "altrep")) {
    "C_ci_"
  } else {
    stop("unknown regex backend", call. = FALSE)
  }
  name <- paste0(prefix, operation, "_regex")

  get(name, envir = namespace, inherits = FALSE)
}


regex_frame_call <- function(
    backend, operation, subject, pattern,
    negate = FALSE, max_count = -1L, opts_regex = NULL
) {
  if (identical(backend, "stringi")) {
    if (identical(operation, "count")) {
      return(stringi::stri_count_regex(
        subject, pattern, opts_regex = opts_regex
      ))
    }
    if (identical(operation, "detect")) {
      return(stringi::stri_detect_regex(
        subject,
        pattern,
        negate = negate,
        max_count = max_count,
        opts_regex = opts_regex
      ))
    }
    stop("unknown regex operation", call. = FALSE)
  }

  symbol <- regex_frame_symbol(backend, operation)
  if (identical(operation, "count")) {
    return(.Call(symbol, subject, pattern, opts_regex))
  }
  if (identical(operation, "detect")) {
    return(.Call(
      symbol, subject, pattern, negate, max_count, opts_regex
    ))
  }
  stop("unknown regex operation", call. = FALSE)
}


regex_frame_inputs <- function(backend, subject, pattern) {
  if (identical(backend, "altrep")) {
    subject <- charport::as_charvec(subject)
    pattern <- charport::as_charvec(pattern)
  }
  list(subject = subject, pattern = pattern)
}


regex_frame_invoke <- function(
    backend, operation, inputs,
    negate = FALSE, max_count = -1L, opts_regex = NULL
) {
  regex_frame_call(
    backend,
    operation,
    inputs$subject,
    inputs$pattern,
    negate = negate,
    max_count = max_count,
    opts_regex = opts_regex
  )
}


regex_frame_capture <- function(expr) {
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
  list(value = value, events = events)
}


regex_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


expect_regex_frame_inputs <- function(backend, inputs) {
  if (!identical(backend, "altrep")) {
    return(invisible(NULL))
  }

  expect_true(charport::is_charvec(inputs$subject))
  expect_false(charport::charport_info(inputs$subject)$is_materialized)
  expect_true(charport::is_charvec(inputs$pattern))
  expect_false(charport::charport_info(inputs$pattern)$is_materialized)
  invisible(NULL)
}


expect_regex_frame_output <- function(operation, output, size) {
  expected_type <- if (identical(operation, "count")) {
    "integer"
  } else {
    "logical"
  }
  expect_identical(typeof(output), expected_type)
  expect_length(output, size)
  expect_null(attributes(output))
  expect_false(charport::is_charvec(output))
  invisible(NULL)
}


test_that("regex count and detect preserve vectorized results", {
  cases <- list(
    list(
      subject = c("aba", "bbb", "", NA_character_),
      pattern = "a",
      negate = FALSE,
      max_count = -1L
    ),
    list(
      subject = c("aba", "bbb", "ba", "a", "cab", NA_character_),
      pattern = c("^a", "b", "(?=a)"),
      negate = FALSE,
      max_count = -1L
    )
  )

  for (operation in c("count", "detect")) {
    for (case in cases) {
      oracle <- regex_frame_call(
        "stringi",
        operation,
        case$subject,
        case$pattern,
        negate = case$negate,
        max_count = case$max_count
      )

      for (backend in c("base", "altrep")) {
        inputs <- regex_frame_inputs(
          backend, case$subject, case$pattern
        )
        actual <- regex_frame_invoke(
          backend,
          operation,
          inputs,
          negate = case$negate,
          max_count = case$max_count
        )

        expect_identical(actual, oracle, info = paste(backend, operation))
        expect_regex_frame_output(operation, actual, length(oracle))
        expect_regex_frame_inputs(backend, inputs)
      }
    }
  }

  subject <- c("a", "b", "a", "c", "a")
  pattern <- "a"
  detect_cases <- list(
    list(negate = FALSE, max_count = 1L),
    list(negate = TRUE, max_count = 2L)
  )
  for (case in detect_cases) {
    oracle <- regex_frame_call(
      "stringi",
      "detect",
      subject,
      pattern,
      negate = case$negate,
      max_count = case$max_count
    )
    for (backend in c("base", "altrep")) {
      inputs <- regex_frame_inputs(backend, subject, pattern)
      actual <- regex_frame_invoke(
        backend,
        "detect",
        inputs,
        negate = case$negate,
        max_count = case$max_count
      )

      expect_identical(actual, oracle, info = backend)
      expect_regex_frame_output("detect", actual, length(subject))
      expect_regex_frame_inputs(backend, inputs)
    }
  }
})


test_that("regex count and detect short-circuit zero recycling", {
  bytes <- regex_frame_marked(c(0xff, 0xfe), "bytes")
  cases <- list(
    list(subject = character(), pattern = bytes),
    list(subject = bytes, pattern = character())
  )
  options <- list(unknown = TRUE)

  for (operation in c("count", "detect")) {
    for (case in cases) {
      oracle <- regex_frame_capture(regex_frame_call(
        "stringi",
        operation,
        case$subject,
        case$pattern,
        opts_regex = options
      ))
      expect_identical(
        oracle$events,
        "warning:incorrect opts_regex setting: 'unknown'; ignoring"
      )
      expect_regex_frame_output(operation, oracle$value, 0L)

      for (backend in c("base", "altrep")) {
        inputs <- regex_frame_inputs(
          backend, case$subject, case$pattern
        )
        actual <- regex_frame_capture(regex_frame_invoke(
          backend,
          operation,
          inputs,
          opts_regex = options
        ))

        expect_identical(actual, oracle, info = paste(backend, operation))
        expect_regex_frame_output(operation, actual$value, 0L)
        expect_regex_frame_inputs(backend, inputs)
      }
    }
  }
})


test_that("regex count preserves pattern-lane error traversal", {
  subject <- c(NA_character_, "x", "x", "x")
  pattern <- c("[", "a(")
  oracle <- regex_frame_capture(regex_frame_call(
    "stringi", "count", subject, pattern
  ))
  expect_null(oracle$value)
  expect_length(oracle$events, 1L)
  expect_match(oracle$events, "U_REGEX_MISSING_CLOSE_BRACKET", fixed = TRUE)
  expect_match(oracle$events, "context=`[`", fixed = TRUE)

  for (backend in c("base", "altrep")) {
    inputs <- regex_frame_inputs(backend, subject, pattern)
    actual <- regex_frame_capture(regex_frame_invoke(
      backend, "count", inputs
    ))

    expect_identical(actual, oracle, info = backend)
    expect_regex_frame_inputs(backend, inputs)
  }
})


test_that("regex warnings and errors retain their staged order", {
  warning_subject <- c("a", "b", "c")
  warning_pattern <- c("", "")
  options <- list(unknown = TRUE)
  expected_warnings <- c(
    "warning:longer object length is not a multiple of shorter object length",
    "warning:incorrect opts_regex setting: 'unknown'; ignoring",
    "warning:empty search patterns are not supported",
    "warning:empty search patterns are not supported"
  )

  for (operation in c("count", "detect")) {
    oracle <- regex_frame_capture(regex_frame_call(
      "stringi",
      operation,
      warning_subject,
      warning_pattern,
      opts_regex = options
    ))
    expect_identical(oracle$events, expected_warnings)

    for (backend in c("base", "altrep")) {
      inputs <- regex_frame_inputs(
        backend, warning_subject, warning_pattern
      )
      actual <- regex_frame_capture(regex_frame_invoke(
        backend,
        operation,
        inputs,
        opts_regex = options
      ))

      expect_identical(actual, oracle, info = paste(backend, operation))
      expect_regex_frame_inputs(backend, inputs)
    }
  }

  bytes <- regex_frame_marked(c(0xff, 0xfe), "bytes")
  error_cases <- list(
    list(subject = "x", pattern = "["),
    list(subject = bytes, pattern = "["),
    list(subject = "x", pattern = bytes),
    list(subject = bytes, pattern = "")
  )
  for (operation in c("count", "detect")) {
    for (case in error_cases) {
      oracle <- regex_frame_capture(regex_frame_call(
        "stringi",
        operation,
        case$subject,
        case$pattern,
        opts_regex = options
      ))
      expect_null(oracle$value)
      expect_identical(
        oracle$events[[1L]],
        "warning:incorrect opts_regex setting: 'unknown'; ignoring"
      )
      expect_match(oracle$events[[2L]], "^error:")

      for (backend in c("base", "altrep")) {
        inputs <- regex_frame_inputs(
          backend, case$subject, case$pattern
        )
        actual <- regex_frame_capture(regex_frame_invoke(
          backend,
          operation,
          inputs,
          opts_regex = options
        ))

        expect_identical(actual, oracle, info = paste(backend, operation))
        expect_regex_frame_inputs(backend, inputs)
      }
    }
  }
})


test_that("regex count and detect preserve string encoding semantics", {
  latin1 <- regex_frame_marked(
    c(0x63, 0x61, 0x66, 0xe9), "latin1"
  )
  native <- enc2native("na\u00efve")
  Encoding(native) <- "unknown"
  bom <- regex_frame_marked(
    c(0xef, 0xbb, 0xbf, 0x61), "UTF-8"
  )
  malformed <- regex_frame_marked(c(0x61, 0xff, 0x62), "UTF-8")
  subject <- c(latin1, native, bom, malformed)
  pattern <- c("\u00e9", "\u00ef", "^a", "\\ufffd")

  for (operation in c("count", "detect")) {
    oracle <- regex_frame_call(
      "stringi", operation, subject, pattern
    )
    for (backend in c("base", "altrep")) {
      inputs <- regex_frame_inputs(backend, subject, pattern)
      actual <- regex_frame_invoke(backend, operation, inputs)

      expect_identical(actual, oracle, info = paste(backend, operation))
      expect_regex_frame_output(operation, actual, length(subject))
      expect_regex_frame_inputs(backend, inputs)
    }
  }

  bytes <- regex_frame_marked(c(0xff, 0xfe), "bytes")
  bytes_cases <- list(
    list(subject = bytes, pattern = "x", max_count = -1L),
    list(subject = "x", pattern = bytes, max_count = -1L),
    list(subject = bytes, pattern = "x", max_count = 0L)
  )
  for (operation in c("count", "detect")) {
    for (case in bytes_cases) {
      if (identical(operation, "count") && case$max_count == 0L) {
        next
      }
      oracle <- regex_frame_capture(regex_frame_call(
        "stringi",
        operation,
        case$subject,
        case$pattern,
        max_count = case$max_count
      ))
      expect_null(oracle$value)
      expect_match(oracle$events, "bytes encoding")

      for (backend in c("base", "altrep")) {
        inputs <- regex_frame_inputs(
          backend, case$subject, case$pattern
        )
        actual <- regex_frame_capture(regex_frame_invoke(
          backend,
          operation,
          inputs,
          max_count = case$max_count
        ))

        expect_identical(actual, oracle, info = paste(backend, operation))
        expect_regex_frame_inputs(backend, inputs)
      }
    }
  }
})


test_that("regex compilation remains lazy where results are predetermined", {
  cases <- list(
    list(
      operation = "count",
      subject = NA_character_,
      pattern = "[",
      max_count = -1L
    ),
    list(
      operation = "detect",
      subject = NA_character_,
      pattern = "[",
      max_count = -1L
    ),
    list(
      operation = "detect",
      subject = "x",
      pattern = "[",
      max_count = 0L
    ),
    list(
      operation = "detect",
      subject = c("x", "x"),
      pattern = c("x", "["),
      max_count = 1L
    )
  )

  for (case in cases) {
    oracle <- regex_frame_call(
      "stringi",
      case$operation,
      case$subject,
      case$pattern,
      max_count = case$max_count
    )
    for (backend in c("base", "altrep")) {
      inputs <- regex_frame_inputs(
        backend, case$subject, case$pattern
      )
      actual <- regex_frame_invoke(
        backend,
        case$operation,
        inputs,
        max_count = case$max_count
      )

      expect_identical(actual, oracle, info = paste(
        backend, case$operation, case$max_count
      ))
      expect_regex_frame_output(
        case$operation, actual, length(oracle)
      )
      expect_regex_frame_inputs(backend, inputs)
    }
  }
})


test_that("regex counting preserves ICU zero-width advancement", {
  subject <- c("", "a", "aa", "\U0001f600a", NA_character_)
  patterns <- c(".*?", "^", "(?=a)")

  for (pattern in patterns) {
    oracle <- regex_frame_call(
      "stringi", "count", subject, pattern
    )
    for (backend in c("base", "altrep")) {
      inputs <- regex_frame_inputs(backend, subject, pattern)
      actual <- regex_frame_invoke(backend, "count", inputs)

      expect_identical(actual, oracle, info = paste(backend, pattern))
      expect_regex_frame_output("count", actual, length(subject))
      expect_regex_frame_inputs(backend, inputs)
    }
  }
})


test_that("regex count and detect coerce inputs and drop attributes", {
  decorated <- structure(
    c("aba", "bbb", NA_character_),
    names = c("first", "second", "third"),
    class = c("regex_frame_input", "character"),
    source_tag = "input only"
  )
  cases <- list(
    list(subject = factor(c("aba", "bbb", NA)), pattern = factor("a")),
    list(subject = 11:13, pattern = c(1L, 2L, 3L)),
    list(
      subject = decorated,
      pattern = structure(c("a", "b", "x"), names = c("x", "y", "z"))
    )
  )

  for (operation in c("count", "detect")) {
    for (case in cases) {
      oracle <- regex_frame_call(
        "stringi", operation, case$subject, case$pattern
      )
      expect_null(attributes(oracle))

      for (backend in c("base", "altrep")) {
        actual <- regex_frame_call(
          backend, operation, case$subject, case$pattern
        )

        expect_identical(actual, oracle, info = paste(backend, operation))
        expect_regex_frame_output(operation, actual, length(oracle))
      }
    }
  }
})


test_that("regex count and detect recover after warn equals two", {
  old_options <- options(warn = 2)
  on.exit(options(old_options), add = TRUE)

  for (operation in c("count", "detect")) {
    expected <- if (identical(operation, "count")) 1L else TRUE

    for (backend in c("base", "altrep")) {
      invalid <- regex_frame_inputs(backend, "x", "")
      expect_error(
        regex_frame_invoke(backend, operation, invalid),
        "empty search patterns are not supported",
        info = paste(backend, operation)
      )
      expect_regex_frame_inputs(backend, invalid)

      valid <- regex_frame_inputs(backend, "x", "x")
      recovered <- regex_frame_invoke(backend, operation, valid)
      expect_identical(recovered, expected, info = paste(backend, operation))
      expect_regex_frame_output(operation, recovered, 1L)
      expect_regex_frame_inputs(backend, valid)
    }
  }
})
