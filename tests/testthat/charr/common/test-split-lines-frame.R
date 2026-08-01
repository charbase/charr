line_split_native_symbol <- function(name) {
  routines <- getDLLRegisteredRoutines(
    getLoadedDLLs()[["charr"]]
  )$.Call
  routines[[name]]
}


line_split_function <- function(backend, scalar = FALSE) {
  if (identical(backend, "stringi")) {
    name <- if (scalar) "stri_split_lines1" else "stri_split_lines"
    return(get(name, envir = asNamespace("stringi"), inherits = FALSE))
  }

  name <- if (identical(backend, "base")) {
    if (scalar) {
      "C_charr_base_ci_split_lines1"
    } else {
      "C_charr_base_ci_split_lines"
    }
  } else if (identical(backend, "altrep")) {
    if (scalar) "C_ci_split_lines1" else "C_ci_split_lines"
  } else {
    stop("unknown line-split backend", call. = FALSE)
  }
  symbol <- line_split_native_symbol(name)

  if (scalar) {
    return(function(str) .Call(symbol, str))
  }
  function(str, omit_empty) .Call(symbol, str, omit_empty)
}


line_split_input <- function(backend, value) {
  if (identical(backend, "altrep")) {
    return(charport::as_charvec(value))
  }
  value
}


line_split_capture <- function(expr, warning_handler = NULL) {
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


line_split_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


expect_line_split_shape <- function(backend, input, output, scalar = FALSE) {
  if (identical(backend, "base")) {
    if (scalar) {
      expect_false(charport::is_charvec(output))
    } else {
      expect_false(any(vapply(output, charport::is_charvec, logical(1))))
    }
    return(invisible(NULL))
  }
  if (!identical(backend, "altrep")) {
    return(invisible(NULL))
  }

  expect_true(charport::is_charvec(input))
  expect_false(charport::charport_info(input)$is_materialized)
  if (scalar) {
    expect_true(charport::is_charvec(output))
    expect_false(charport::charport_info(output)$is_materialized)
  } else {
    expect_true(all(vapply(output, charport::is_charvec, logical(1))))
    expect_true(all(vapply(
      output,
      function(value) !charport::charport_info(value)$is_materialized,
      logical(1)
    )))
  }
  invisible(NULL)
}


test_that("line splitting preserves empty fields and every separator", {
  separators <- c("\r\n", "\r", "\n", "\u0085", "\v", "\f", "\u2028", "\u2029")
  values <- c("", "\n", "a\n", "\n\n", "a\n\nb", NA_character_)

  for (omit_empty in list(FALSE, TRUE, NA)) {
    expected <- line_split_function("stringi")(values, omit_empty)
    for (backend in c("base", "altrep")) {
      input <- line_split_input(backend, values)
      actual <- line_split_function(backend)(input, omit_empty)
      expect_line_split_shape(backend, input, actual)
      expect_identical(actual, expected, info = backend)
    }
  }

  scalar_cases <- c(
    "", "\n", "\n\n", "a\n",
    paste0("a", paste(separators, collapse = "b"), "z")
  )
  for (value in scalar_cases) {
    expected <- line_split_function("stringi", scalar = TRUE)(value)
    for (backend in c("base", "altrep")) {
      input <- line_split_input(backend, value)
      actual <- line_split_function(backend, scalar = TRUE)(input)
      expect_line_split_shape(backend, input, actual, scalar = TRUE)
      expect_identical(actual, expected, info = backend)
    }
  }
})


test_that("scalar line splitting preserves preparation order and missing identity", {
  bytes <- line_split_marked(0xff, "bytes")
  cases <- list(
    c("a\n", bytes),
    c(bytes, "a\n")
  )

  for (value in cases) {
    expected <- line_split_capture(
      line_split_function("stringi", scalar = TRUE)(value)
    )
    for (backend in c("base", "altrep")) {
      input <- line_split_input(backend, value)
      actual <- line_split_capture(
        line_split_function(backend, scalar = TRUE)(input)
      )
      expect_identical(actual, expected, info = backend)
    }
  }

  expected_empty <- line_split_capture(
    line_split_function("stringi", scalar = TRUE)(character())
  )
  for (backend in c("base", "altrep")) {
    actual <- line_split_capture(
      line_split_function(backend, scalar = TRUE)(
        line_split_input(backend, character())
      )
    )
    expect_identical(actual, expected_empty, info = backend)
  }

  missing <- structure(c(name = NA_character_), note = "retained")
  for (backend in c("base", "altrep")) {
    input <- line_split_input(backend, missing)
    actual <- line_split_function(backend, scalar = TRUE)(input)
    expect_identical(actual, input, info = backend)
    if (identical(backend, "altrep")) {
      expect_line_split_shape(backend, input, actual, scalar = TRUE)
    }
  }
})


test_that("line splitting preserves encoding normalization and output shape", {
  latin1 <- line_split_marked(
    c(0x63, 0x61, 0x66, 0xe9, 0x0a, 0x66, 0x69, 0x6e),
    "latin1"
  )
  malformed <- line_split_marked(c(0x61, 0xff, 0x62), "UTF-8")
  values <- structure(
    c(paste0("\ufeffa\n\ufeffb"), latin1, malformed),
    names = c("bom", "latin1", "malformed"),
    note = "dropped"
  )
  expected <- line_split_function("stringi")(values, FALSE)

  for (backend in c("base", "altrep")) {
    input <- line_split_input(backend, values)
    actual <- line_split_function(backend)(input, FALSE)
    expect_null(attributes(actual), info = backend)
    expect_line_split_shape(backend, input, actual)
    expect_identical(actual, expected, info = backend)
    expect_identical(lapply(actual, Encoding), lapply(expected, Encoding))
  }
})


test_that("line splitting keeps empty-vector and warning-error order", {
  bytes <- line_split_marked(c(0xff, 0xfe), "bytes")

  for (backend in c("stringi", "base", "altrep")) {
    input <- line_split_input(backend, bytes)
    actual <- line_split_function(backend)(input, logical())
    expect_identical(actual, list(), info = backend)
    if (identical(backend, "altrep")) {
      expect_false(charport::charport_info(input)$is_materialized)
    }
  }

  values <- c(bytes, "x")
  omit_empty <- c(FALSE, FALSE, FALSE)
  expected <- line_split_capture(
    line_split_function("stringi")(values, omit_empty)
  )
  expect_length(expected$events, 2L)
  expect_match(expected$events[[1L]], "^warning:longer object length")
  expect_match(expected$events[[2L]], "^error:.*bytes encoding")

  for (backend in c("base", "altrep")) {
    input <- line_split_input(backend, values)
    actual <- line_split_capture(
      line_split_function(backend)(input, omit_empty)
    )
    expect_identical(actual, expected, info = backend)

    stopped <- line_split_capture(
      line_split_function(backend)(input, omit_empty),
      warning_handler = function(condition) {
        stop("warning handler stopped", call. = FALSE)
      }
    )
    expect_length(stopped$events, 2L)
    expect_match(stopped$events[[1L]], "^warning:longer object length")
    expect_identical(stopped$events[[2L]], "error:warning handler stopped")
  }
})
