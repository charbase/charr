extract_first_fixed_frame_symbol <- function(backend) {
  namespace <- asNamespace("charr")
  name <- if (identical(backend, "base")) {
    "C_charr_base_ci_extract_first_fixed"
  } else if (identical(backend, "altrep")) {
    "C_ci_extract_first_fixed"
  } else {
    stop("unknown fixed-extract backend", call. = FALSE)
  }

  get(name, envir = namespace, inherits = FALSE)
}


extract_first_fixed_frame_call <- function(
    backend, subject, pattern, opts_fixed = NULL
) {
  if (identical(backend, "stringi")) {
    return(stringi::stri_extract_first_fixed(
      subject, pattern, opts_fixed = opts_fixed
    ))
  }

  symbol <- extract_first_fixed_frame_symbol(backend)
  if (identical(backend, "altrep")) {
    return(.Call(
      symbol, subject, pattern, opts_fixed
    ))
  }
  .Call(symbol, subject, pattern, opts_fixed)
}


extract_first_fixed_frame_inputs <- function(backend, subject, pattern) {
  if (identical(backend, "altrep")) {
    subject <- charport::as_charvec(subject)
    pattern <- charport::as_charvec(pattern)
  }
  list(subject = subject, pattern = pattern)
}


extract_first_fixed_frame_invoke <- function(
    backend, inputs, opts_fixed = NULL
) {
  extract_first_fixed_frame_call(
    backend,
    inputs$subject,
    inputs$pattern,
    opts_fixed = opts_fixed
  )
}


extract_first_fixed_frame_capture <- function(expr) {
  events <- character()
  value <- tryCatch(
    withCallingHandlers(
      force(expr),
      warning = function(condition) {
        events <<- c(
          events,
          paste0("warning:", conditionMessage(condition))
        )
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


extract_first_fixed_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


expect_extract_first_fixed_frame_inputs <- function(backend, inputs) {
  if (!identical(backend, "altrep")) {
    return(invisible(NULL))
  }

  expect_true(charport::is_charvec(inputs$subject))
  expect_false(charport::charport_info(inputs$subject)$is_materialized)
  expect_true(charport::is_charvec(inputs$pattern))
  expect_false(charport::charport_info(inputs$pattern)$is_materialized)
  invisible(NULL)
}


expect_extract_first_fixed_frame_output <- function(backend, output) {
  expect_null(attributes(output))
  if (identical(backend, "base")) {
    expect_false(charport::is_charvec(output))
  } else if (identical(backend, "altrep")) {
    expect_true(charport::is_charvec(output))
    expect_false(charport::charport_info(output)$is_materialized)
  }
  invisible(NULL)
}


test_that("fixed first extraction keeps direct and fallback slices", {
  skip_if_stringi_cannot_compare_native()

  direct_subject <- c(
    "zaq", "caf\u00e9", "no-match", "", NA_character_, "value"
  )
  direct_pattern <- c("a", "\u00e9", "x", "x", "x", NA_character_)
  direct_oracle <- extract_first_fixed_frame_call(
    "stringi", direct_subject, direct_pattern
  )
  expect_identical(
    direct_oracle,
    c("a", "\u00e9", NA_character_, NA_character_, NA_character_, NA_character_)
  )

  for (backend in c("base", "altrep")) {
    inputs <- extract_first_fixed_frame_inputs(
      backend, direct_subject, direct_pattern
    )
    actual <- extract_first_fixed_frame_invoke(backend, inputs)

    expect_extract_first_fixed_frame_inputs(backend, inputs)
    expect_extract_first_fixed_frame_output(backend, actual)
    expect_identical(actual, direct_oracle, info = backend)
    expect_identical(Encoding(actual), Encoding(direct_oracle), info = backend)
  }

  folded_subject <- "\u017fince"
  folded_pattern <- "s"
  folded_options <- list(case_insensitive = TRUE)
  folded_oracle <- extract_first_fixed_frame_call(
    "stringi", folded_subject, folded_pattern, folded_options
  )
  expect_identical(folded_oracle, "\u017f")

  for (backend in c("base", "altrep")) {
    inputs <- extract_first_fixed_frame_inputs(
      backend, folded_subject, folded_pattern
    )
    actual <- extract_first_fixed_frame_invoke(
      backend, inputs, folded_options
    )

    expect_extract_first_fixed_frame_inputs(backend, inputs)
    expect_extract_first_fixed_frame_output(backend, actual)
    expect_identical(actual, folded_oracle, info = backend)
    expect_identical(Encoding(actual), Encoding(folded_oracle), info = backend)
  }

  latin1 <- extract_first_fixed_frame_marked(
    c(0x63, 0x61, 0x66, 0xe9), "latin1"
  )
  native <- enc2native("na\u00efve")
  Encoding(native) <- "unknown"
  bom <- extract_first_fixed_frame_marked(
    c(0xef, 0xbb, 0xbf, 0x61), "UTF-8"
  )
  malformed <- extract_first_fixed_frame_marked(
    c(0x61, 0xff, 0x62), "UTF-8"
  )
  malformed_pattern <- extract_first_fixed_frame_marked(0xff, "UTF-8")
  fallback_subject <- c(latin1, native, bom, malformed)
  fallback_pattern <- c("\u00e9", "\u00ef", "a", malformed_pattern)
  fallback_oracle <- extract_first_fixed_frame_call(
    "stringi", fallback_subject, fallback_pattern
  )
  expect_identical(fallback_oracle[[3L]], "a")
  expect_identical(charToRaw(fallback_oracle[[4L]]), as.raw(0xff))
  expect_identical(Encoding(fallback_oracle[[4L]]), "UTF-8")

  for (backend in c("base", "altrep")) {
    inputs <- extract_first_fixed_frame_inputs(
      backend, fallback_subject, fallback_pattern
    )
    actual <- extract_first_fixed_frame_invoke(backend, inputs)

    expect_extract_first_fixed_frame_inputs(backend, inputs)
    expect_extract_first_fixed_frame_output(backend, actual)
    expect_identical(actual, fallback_oracle, info = backend)
    expect_identical(
      Encoding(actual), Encoding(fallback_oracle), info = backend
    )
    expect_identical(charToRaw(actual[[4L]]), as.raw(0xff), info = backend)
  }
})


test_that("fixed first extraction skips zero-recycled inputs", {
  bytes <- extract_first_fixed_frame_marked(c(0xff, 0xfe), "bytes")
  cases <- list(
    list(subject = character(), pattern = bytes),
    list(subject = bytes, pattern = character())
  )

  for (case in cases) {
    oracle <- extract_first_fixed_frame_capture(
      extract_first_fixed_frame_call(
        "stringi", case$subject, case$pattern
      )
    )
    expect_identical(oracle, list(value = character(), events = character()))

    for (backend in c("base", "altrep")) {
      inputs <- extract_first_fixed_frame_inputs(
        backend, case$subject, case$pattern
      )
      actual <- extract_first_fixed_frame_capture(
        extract_first_fixed_frame_invoke(backend, inputs)
      )

      expect_extract_first_fixed_frame_inputs(backend, inputs)
      expect_extract_first_fixed_frame_output(backend, actual$value)
      expect_identical(actual, oracle, info = backend)
    }
  }
})


test_that("fixed first extraction preserves condition order and recovers", {
  bytes <- extract_first_fixed_frame_marked(c(0xff, 0xfe), "bytes")
  warning_subject <- c("a", "b", "c")
  warning_pattern <- c("", "a")
  warning_options <- list(unknown = TRUE)
  warning_oracle <- extract_first_fixed_frame_capture(
    extract_first_fixed_frame_call(
      "stringi", warning_subject, warning_pattern, warning_options
    )
  )
  expect_identical(
    warning_oracle$events,
    c(
      "warning:incorrect opts_fixed setting: 'unknown'; ignoring",
      "warning:longer object length is not a multiple of shorter object length",
      "warning:empty search patterns are not supported"
    )
  )

  for (backend in c("base", "altrep")) {
    inputs <- extract_first_fixed_frame_inputs(
      backend, warning_subject, warning_pattern
    )
    actual <- extract_first_fixed_frame_capture(
      extract_first_fixed_frame_invoke(
        backend, inputs, opts_fixed = warning_options
      )
    )

    expect_extract_first_fixed_frame_inputs(backend, inputs)
    expect_extract_first_fixed_frame_output(backend, actual$value)
    expect_identical(actual, warning_oracle, info = backend)
  }

  error_cases <- list(
    list(subject = bytes, pattern = ""),
    list(subject = "abc", pattern = bytes)
  )
  for (case in error_cases) {
    oracle <- extract_first_fixed_frame_capture(
      extract_first_fixed_frame_call(
        "stringi", case$subject, case$pattern
      )
    )
    expect_identical(
      oracle$events,
      "error:bytes encoding is not supported by this function"
    )
    expect_null(oracle$value)

    for (backend in c("base", "altrep")) {
      inputs <- extract_first_fixed_frame_inputs(
        backend, case$subject, case$pattern
      )
      actual <- extract_first_fixed_frame_capture(
        extract_first_fixed_frame_invoke(backend, inputs)
      )

      expect_identical(actual, oracle, info = backend)
      expect_extract_first_fixed_frame_inputs(backend, inputs)

      valid <- extract_first_fixed_frame_inputs(
        backend, c("alpha", "none"), "a"
      )
      recovered <- extract_first_fixed_frame_invoke(backend, valid)
      expect_extract_first_fixed_frame_inputs(backend, valid)
      expect_extract_first_fixed_frame_output(backend, recovered)
      expect_identical(recovered, c("a", NA_character_), info = backend)
    }
  }
})


test_that("fixed first extraction coerces arguments and drops attributes", {
  decorated <- structure(
    c("a", "ba", NA_character_),
    names = c("first", "second", "third"),
    class = c("fixed_extract_input", "character"),
    source_tag = "input only"
  )
  cases <- list(
    list(
      subject = factor(c("1", "21", NA_character_)),
      pattern = c(1L, 2L, 3L)
    ),
    list(subject = 11:13, pattern = factor(c("1", "2", "3"))),
    list(
      subject = decorated,
      pattern = structure(c("a", "a", "x"), names = c("x", "y", "z"))
    )
  )

  for (case in cases) {
    oracle <- extract_first_fixed_frame_call(
      "stringi", case$subject, case$pattern
    )
    expect_null(attributes(oracle))

    for (backend in c("base", "altrep")) {
      actual <- extract_first_fixed_frame_call(
        backend, case$subject, case$pattern
      )

      expect_extract_first_fixed_frame_output(backend, actual)
      expect_identical(actual, oracle, info = backend)
      expect_null(attributes(actual), info = backend)
    }
  }
})


test_that("fixed first extraction recovers when warnings become errors", {
  oracle <- extract_first_fixed_frame_call(
    "stringi", c("alpha", "none"), "a"
  )
  old_options <- options(warn = 2)
  on.exit(options(old_options), add = TRUE)

  for (backend in c("base", "altrep")) {
    invalid <- extract_first_fixed_frame_inputs(backend, "abc", "")
    expect_error(
      extract_first_fixed_frame_invoke(backend, invalid),
      "empty search patterns are not supported",
      fixed = TRUE,
      info = backend
    )
    expect_extract_first_fixed_frame_inputs(backend, invalid)

    valid <- extract_first_fixed_frame_inputs(
      backend, c("alpha", "none"), "a"
    )
    recovered <- extract_first_fixed_frame_invoke(backend, valid)
    expect_extract_first_fixed_frame_inputs(backend, valid)
    expect_extract_first_fixed_frame_output(backend, recovered)
    expect_identical(recovered, oracle, info = backend)
  }
})
