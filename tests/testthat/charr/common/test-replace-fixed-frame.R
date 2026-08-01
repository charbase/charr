replace_fixed_frame_function <- function(backend, operation) {
  name <- switch(
    operation,
    first = "ci_replace_first_fixed",
    all = "ci_replace_all_fixed",
    stop("unknown fixed replacement operation", call. = FALSE)
  )

  if (identical(backend, "stringi")) {
    name <- sub("^ci_", "stri_", name)
    return(get(name, envir = asNamespace("stringi"), inherits = FALSE))
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

  stop("unknown fixed replacement backend", call. = FALSE)
}


replace_fixed_frame_inputs <- function(
    backend, subject, pattern, replacement
) {
  if (identical(backend, "altrep")) {
    subject <- charport::as_charvec(subject)
    pattern <- charport::as_charvec(pattern)
    replacement <- charport::as_charvec(replacement)
  }
  list(
    subject = subject,
    pattern = pattern,
    replacement = replacement
  )
}


replace_fixed_frame_invoke <- function(
    backend, operation, inputs, vectorize_all = TRUE,
    opts_fixed = NULL
) {
  fun <- replace_fixed_frame_function(backend, operation)
  args <- list(
    inputs$subject,
    inputs$pattern,
    inputs$replacement,
    opts_fixed = opts_fixed
  )
  if (identical(operation, "all")) {
    args$vectorize_all <- vectorize_all
  }
  do.call(fun, args)
}


replace_fixed_frame_capture <- function(expr, warning_handler = NULL) {
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


replace_fixed_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


expect_replace_fixed_frame_shape <- function(backend, inputs, output = NULL) {
  if (identical(backend, "base") && !is.null(output)) {
    expect_false(charport::is_charvec(output))
  }
  if (!identical(backend, "altrep")) {
    return(invisible(NULL))
  }

  for (input in inputs) {
    expect_true(charport::is_charvec(input))
    expect_false(charport::charport_info(input)$is_materialized)
  }
  if (!is.null(output)) {
    expect_true(charport::is_charvec(output))
    expect_false(charport::charport_info(output)$is_materialized)
  }
  invisible(NULL)
}


test_that("fixed replacement keeps direct and general Frame results", {
  malformed <- replace_fixed_frame_marked(c(0x61, 0xff, 0x61), "UTF-8")
  latin1 <- replace_fixed_frame_marked(0xe9, "latin1")
  subject <- c("a-a", paste0("\ufeff", "a-a"), malformed, "", NA_character_)

  cases <- list(
    list(operation = "first", pattern = "a", replacement = "X"),
    list(operation = "all", pattern = "a", replacement = ""),
    list(operation = "all", pattern = "a", replacement = latin1),
    list(
      operation = "all", pattern = "A", replacement = "x",
      opts_fixed = list(case_insensitive = TRUE)
    )
  )

  for (case in cases) {
    oracle_inputs <- replace_fixed_frame_inputs(
      "stringi", subject, case$pattern, case$replacement
    )
    oracle <- replace_fixed_frame_invoke(
      "stringi", case$operation, oracle_inputs,
      opts_fixed = case$opts_fixed
    )

    for (backend in c("base", "altrep")) {
      inputs <- replace_fixed_frame_inputs(
        backend, subject, case$pattern, case$replacement
      )
      actual <- replace_fixed_frame_invoke(
        backend, case$operation, inputs,
        opts_fixed = case$opts_fixed
      )
      expect_replace_fixed_frame_shape(backend, inputs, actual)
      expect_identical(actual, oracle, info = backend)
      expect_identical(Encoding(actual), Encoding(oracle), info = backend)
    }
  }
})


test_that("sequential fixed replacement keeps condition order", {
  invalid_options <- structure(list(TRUE), names = "unknown")
  bytes <- replace_fixed_frame_marked(c(0xff, 0xfe), "bytes")

  empty_oracle <- replace_fixed_frame_capture(
    replace_fixed_frame_invoke(
      "stringi", "all",
      replace_fixed_frame_inputs(
        "stringi", character(), bytes, bytes
      ),
      vectorize_all = FALSE,
      opts_fixed = invalid_options
    )
  )
  expect_identical(empty_oracle, list(value = character(), events = character()))

  for (backend in c("base", "altrep")) {
    inputs <- replace_fixed_frame_inputs(
      backend, character(), bytes, bytes
    )
    actual <- replace_fixed_frame_capture(
      replace_fixed_frame_invoke(
        backend, "all", inputs,
        vectorize_all = FALSE,
        opts_fixed = invalid_options
      )
    )
    expect_identical(actual, empty_oracle, info = backend)
    expect_replace_fixed_frame_shape(backend, inputs, actual$value)
  }

  scenarios <- list(
    list("a", "a", c("x", "y"), invalid_options),
    list("a", c("a", "x", "z"), c("A", "X"), invalid_options)
  )
  for (scenario in scenarios) {
    oracle <- replace_fixed_frame_capture(
      replace_fixed_frame_invoke(
        "stringi", "all",
        replace_fixed_frame_inputs(
          "stringi", scenario[[1L]], scenario[[2L]], scenario[[3L]]
        ),
        vectorize_all = FALSE,
        opts_fixed = scenario[[4L]]
      )
    )

    for (backend in c("base", "altrep")) {
      inputs <- replace_fixed_frame_inputs(
        backend, scenario[[1L]], scenario[[2L]], scenario[[3L]]
      )
      actual <- replace_fixed_frame_capture(
        replace_fixed_frame_invoke(
          backend, "all", inputs,
          vectorize_all = FALSE,
          opts_fixed = scenario[[4L]]
        )
      )
      expect_identical(actual, oracle, info = backend)
      expect_replace_fixed_frame_shape(backend, inputs, actual$value)
    }
  }
})


test_that("fixed replacement warnings keep their phase order", {
  invalid_options <- structure(list(TRUE), names = "unknown")
  subject <- c("a", "b", "c")
  pattern <- c("", "a")
  replacement <- "x"

  oracle <- replace_fixed_frame_capture(
    replace_fixed_frame_invoke(
      "stringi", "all",
      replace_fixed_frame_inputs(
        "stringi", subject, pattern, replacement
      ),
      opts_fixed = invalid_options
    )
  )
  expect_length(oracle$events, 3L)
  expect_match(oracle$events[[1L]], "incorrect opts_fixed setting")
  expect_match(oracle$events[[2L]], "longer object length")
  expect_match(oracle$events[[3L]], "empty search patterns")

  for (backend in c("base", "altrep")) {
    inputs <- replace_fixed_frame_inputs(
      backend, subject, pattern, replacement
    )
    actual <- replace_fixed_frame_capture(
      replace_fixed_frame_invoke(
        backend, "all", inputs,
        opts_fixed = invalid_options
      )
    )
    expect_identical(actual, oracle, info = backend)
    expect_replace_fixed_frame_shape(backend, inputs, actual$value)
  }

  sequential_oracle <- replace_fixed_frame_capture(
    replace_fixed_frame_invoke(
      "stringi", "all",
      replace_fixed_frame_inputs(
        "stringi", "a", c("", "x"), "z"
      ),
      vectorize_all = FALSE
    )
  )
  expect_identical(
    sequential_oracle$events,
    rep("warning:empty search patterns are not supported", 2L)
  )
  for (backend in c("base", "altrep")) {
    inputs <- replace_fixed_frame_inputs(
      backend, "a", c("", "x"), "z"
    )
    actual <- replace_fixed_frame_capture(
      replace_fixed_frame_invoke(
        backend, "all", inputs, vectorize_all = FALSE
      )
    )
    expect_identical(actual, sequential_oracle, info = backend)
    expect_replace_fixed_frame_shape(backend, inputs, actual$value)
  }
})


test_that("fixed replacement warnings leave the Frame reusable", {
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (backend in c("base", "altrep")) {
    warning_inputs <- replace_fixed_frame_inputs(
      backend, c("a", "b", "c"), c("a", "b"), "x"
    )
    expect_error(
      replace_fixed_frame_invoke(backend, "all", warning_inputs),
      "longer object length",
      info = backend
    )
    expect_replace_fixed_frame_shape(backend, warning_inputs)

    valid_inputs <- replace_fixed_frame_inputs(
      backend, c("a", "ba"), "a", "x"
    )
    oracle <- replace_fixed_frame_invoke(
      "stringi", "all",
      replace_fixed_frame_inputs(
        "stringi", c("a", "ba"), "a", "x"
      )
    )
    actual <- replace_fixed_frame_invoke(
      backend, "all", valid_inputs
    )
    expect_identical(actual, oracle, info = backend)
    expect_replace_fixed_frame_shape(backend, valid_inputs, actual)
  }
})


test_that("fixed replacement warnings permit ALTREP reentry", {
  inputs <- replace_fixed_frame_inputs(
    "altrep", c("a", "b", "c"), c("a", "b"), "x"
  )
  scalar_pattern <- charport::as_charvec("a")
  scalar_replacement <- charport::as_charvec("y")
  reentered <- NULL

  actual <- replace_fixed_frame_capture(
    replace_fixed_frame_invoke("altrep", "all", inputs),
    warning_handler = function(condition) {
      reentered <<- charr:::ci_replace_all_fixed(
        inputs$subject, scalar_pattern, scalar_replacement
      )
    }
  )
  oracle <- replace_fixed_frame_capture(
    replace_fixed_frame_invoke(
      "stringi", "all",
      replace_fixed_frame_inputs(
        "stringi", c("a", "b", "c"), c("a", "b"), "x"
      )
    )
  )

  expect_identical(actual, oracle)
  expect_identical(reentered, c("y", "b", "c"))
  expect_replace_fixed_frame_shape("altrep", inputs, actual$value)
  expect_false(charport::charport_info(scalar_pattern)$is_materialized)
  expect_false(charport::charport_info(scalar_replacement)$is_materialized)
})
