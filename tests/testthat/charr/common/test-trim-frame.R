trim_frame_function <- function(backend, direction) {
  name <- paste0("ci_trim_", direction)
  if (identical(backend, "stringi")) {
    return(get(
      paste0("stri_trim_", direction),
      envir = asNamespace("stringi"), inherits = FALSE
    ))
  }
  if (identical(backend, "base")) {
    return(get(
      name,
      envir = charr:::.charr_base_leaf_environment,
      inherits = FALSE
    ))
  }
  if (identical(backend, "altrep")) {
    return(get(name, envir = asNamespace("charr"), inherits = FALSE))
  }

  stop("unknown trim backend", call. = FALSE)
}


trim_frame_inputs <- function(backend, subject, pattern) {
  if (identical(backend, "altrep")) {
    subject <- charport::as_charvec(subject)
    pattern <- charport::as_charvec(pattern)
  }
  list(subject = subject, pattern = pattern)
}


trim_frame_invoke <- function(
    backend, direction, inputs, negate = FALSE
) {
  trim_frame_function(backend, direction)(
    inputs$subject, inputs$pattern, negate = negate
  )
}


trim_frame_capture <- function(expr, warning_handler = NULL) {
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


trim_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


expect_trim_frame_lazy <- function(backend, inputs, output = NULL) {
  if (!identical(backend, "altrep")) {
    return(invisible(NULL))
  }

  expect_true(charport::is_charvec(inputs$subject))
  expect_false(charport::charport_info(inputs$subject)$is_materialized)
  expect_true(charport::is_charvec(inputs$pattern))
  expect_false(charport::charport_info(inputs$pattern)$is_materialized)
  if (!is.null(output)) {
    expect_true(charport::is_charvec(output))
    expect_false(charport::charport_info(output)$is_materialized)
  }
  invisible(NULL)
}


test_that("character-class trim preserves supplementary UTF-8 bytes", {
  payload <- paste0(
    "\U0001f469\u200d\U0001f469\u200d\U0001f467\u200d\U0001f466",
    "e\u0301"
  )
  subject <- paste0(" ", payload, " ")

  for (backend in c("stringi", "base", "altrep")) {
    inputs <- trim_frame_inputs(backend, subject, "\\P{Wspace}")
    actual <- trim_frame_invoke(backend, "both", inputs)
    expect_trim_frame_lazy(backend, inputs, actual)
    expect_identical(charToRaw(as.character(actual)), charToRaw(payload))
  }
})


test_that("character-class trim preserves direction and vectorization", {
  subject <- c("  alpha  ", "--beta--", "  gamma--", NA_character_)
  pattern <- c("\\P{Wspace}", "[a-z]", "[a-z]", "\\P{Wspace}")

  for (direction in c("left", "right", "both")) {
    for (negate in c(FALSE, TRUE)) {
      oracle_inputs <- trim_frame_inputs("stringi", subject, pattern)
      oracle <- trim_frame_invoke(
        "stringi", direction, oracle_inputs, negate = negate
      )

      for (backend in c("base", "altrep")) {
        inputs <- trim_frame_inputs(backend, subject, pattern)
        actual <- trim_frame_invoke(
          backend, direction, inputs, negate = negate
        )

        expect_identical(actual, oracle, info = backend)
        expect_trim_frame_lazy(backend, inputs, actual)
      }
    }
  }
})


test_that("character-class trim preserves condition order", {
  bytes <- trim_frame_marked(c(0xff, 0xfe), "bytes")
  scenarios <- list(
    list(subject = c("a", "b", "c"), pattern = c("[a]", "[")),
    list(subject = bytes, pattern = NA_character_),
    list(subject = character(), pattern = "[")
  )

  for (direction in c("left", "right", "both")) {
    for (scenario in scenarios) {
      oracle_inputs <- trim_frame_inputs(
        "stringi", scenario$subject, scenario$pattern
      )
      oracle <- trim_frame_capture(trim_frame_invoke(
        "stringi", direction, oracle_inputs
      ))

      for (backend in c("base", "altrep")) {
        inputs <- trim_frame_inputs(
          backend, scenario$subject, scenario$pattern
        )
        actual <- trim_frame_capture(trim_frame_invoke(
          backend, direction, inputs
        ))

        expect_identical(actual$events, oracle$events, info = backend)
        expect_identical(actual$value, oracle$value, info = backend)
        expect_trim_frame_lazy(backend, inputs)
      }
    }

    for (backend in c("base", "altrep")) {
      inputs <- trim_frame_inputs(backend, bytes, "[")
      actual <- trim_frame_capture(trim_frame_invoke(
        backend, direction, inputs
      ))

      expect_length(actual$events, 1L)
      expect_match(actual$events, "UnicodeSet", info = backend)
      expect_trim_frame_lazy(backend, inputs)
    }
  }
})


test_that("character-class trim keeps zero-recycling validation order", {
  bytes <- trim_frame_marked(c(0xff, 0xfe), "bytes")

  for (direction in c("left", "right", "both")) {
    for (backend in c("stringi", "base", "altrep")) {
      empty_pattern <- trim_frame_inputs(backend, bytes, character())
      expect_identical(
        trim_frame_invoke(backend, direction, empty_pattern),
        character(),
        info = backend
      )
      expect_trim_frame_lazy(backend, empty_pattern)

      invalid_pattern <- trim_frame_inputs(backend, character(), "[")
      expect_error(
        trim_frame_invoke(backend, direction, invalid_pattern),
        "UnicodeSet|character class|pattern",
        info = backend
      )
      expect_trim_frame_lazy(backend, invalid_pattern)
    }
  }
})


test_that("character-class trim warning errors leave the Frame reusable", {
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (direction in c("left", "right", "both")) {
    for (backend in c("base", "altrep")) {
      warning_inputs <- trim_frame_inputs(
        backend, c(" a ", " b ", " c "), c("\\P{Wspace}", "[a-z]")
      )
      expect_error(
        trim_frame_invoke(backend, direction, warning_inputs),
        "longer object length",
        info = backend
      )
      expect_trim_frame_lazy(backend, warning_inputs)

      valid_inputs <- trim_frame_inputs(
        backend, c(" alpha ", " beta "), "\\P{Wspace}"
      )
      oracle <- trim_frame_invoke(
        "stringi", direction,
        trim_frame_inputs(
          "stringi", c(" alpha ", " beta "), "\\P{Wspace}"
        )
      )
      actual <- trim_frame_invoke(backend, direction, valid_inputs)
      expect_identical(actual, oracle, info = backend)
      expect_trim_frame_lazy(backend, valid_inputs, actual)
    }
  }
})


test_that("character-class trim warnings permit ALTREP reentry", {
  inputs <- trim_frame_inputs(
    "altrep", c(" a ", " b ", " c "), c("\\P{Wspace}", "[a-z]")
  )
  scalar_pattern <- charport::as_charvec("\\P{Wspace}")
  reentered <- NULL

  actual <- trim_frame_capture(
    trim_frame_invoke("altrep", "both", inputs),
    warning_handler = function(condition) {
      reentered <<- charr:::ci_trim_both(
        inputs$subject, scalar_pattern, negate = FALSE
      )
    }
  )

  oracle <- trim_frame_capture(trim_frame_invoke(
    "stringi", "both",
    trim_frame_inputs(
      "stringi", c(" a ", " b ", " c "),
      c("\\P{Wspace}", "[a-z]")
    )
  ))
  expect_identical(actual$events, oracle$events)
  expect_identical(actual$value, oracle$value)
  expect_identical(reentered, c("a", "b", "c"))
  expect_trim_frame_lazy("altrep", inputs, actual$value)
  expect_false(charport::charport_info(scalar_pattern)$is_materialized)
})
