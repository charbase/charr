dup_frame_function <- function(backend) {
  if (identical(backend, "stringi")) {
    return(get("stri_dup", envir = asNamespace("stringi"), inherits = FALSE))
  }
  if (identical(backend, "base")) {
    return(get(
      "ci_dup",
      envir = charr:::.charr_base_leaf_environment,
      inherits = FALSE
    ))
  }
  if (identical(backend, "altrep")) {
    return(get("ci_dup", envir = asNamespace("charr"), inherits = FALSE))
  }

  stop("unknown duplicate backend", call. = FALSE)
}


dup_frame_input <- function(backend, value) {
  if (identical(backend, "altrep")) {
    return(charport::as_charvec(value))
  }
  value
}


dup_frame_call <- function(backend, value, times) {
  dup_frame_function(backend)(dup_frame_input(backend, value), times)
}


dup_frame_capture <- function(expr, warning_handler = NULL) {
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


dup_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


expect_dup_frame_shape <- function(backend, input, output = NULL) {
  if (identical(backend, "base") && !is.null(output)) {
    expect_false(charport::is_charvec(output))
  }
  if (!identical(backend, "altrep")) {
    return(invisible(NULL))
  }

  expect_true(charport::is_charvec(input))
  expect_false(charport::charport_info(input)$is_materialized)
  if (!is.null(output)) {
    expect_true(charport::is_charvec(output))
    expect_false(charport::charport_info(output)$is_materialized)
  }
  invisible(NULL)
}


test_that("duplicate Frame preserves values, encodings, and output shape", {
  latin1 <- dup_frame_marked(c(0x63, 0x61, 0x66, 0xe9), "latin1")
  malformed <- dup_frame_marked(c(0x61, 0xff, 0x62), "UTF-8")
  values <- c(
    bom = paste0("\ufeff", "ab"),
    latin1 = latin1,
    malformed = malformed,
    empty = "",
    missing = NA_character_
  )
  times <- c(2L, 1L, 2L, 0L, NA_integer_)
  oracle <- dup_frame_call("stringi", values, times)

  for (backend in c("base", "altrep")) {
    input <- dup_frame_input(backend, unname(values))
    actual <- dup_frame_function(backend)(input, times)

    expect_identical(actual, unname(oracle), info = backend)
    expect_identical(Encoding(as.character(actual)), Encoding(unname(oracle)))
    expect_dup_frame_shape(backend, input, actual)
  }
})


test_that("duplicate validates all strings before applying times", {
  bytes <- dup_frame_marked(c(0xff, 0xfe), "bytes")

  for (times in list(0L, -1L, NA_integer_)) {
    oracle <- dup_frame_capture(dup_frame_call("stringi", bytes, times))
    expect_match(oracle$events[[1L]], "bytes encoding")

    for (backend in c("base", "altrep")) {
      input <- dup_frame_input(backend, bytes)
      actual <- dup_frame_capture(
        dup_frame_function(backend)(input, times)
      )
      expect_identical(actual, oracle, info = backend)
      expect_dup_frame_shape(backend, input)
    }
  }
})


test_that("duplicate keeps empty-vector and warning-error order", {
  bytes <- dup_frame_marked(c(0xff, 0xfe), "bytes")

  for (backend in c("base", "altrep")) {
    input <- dup_frame_input(backend, bytes)
    actual <- dup_frame_function(backend)(input, integer())
    expect_identical(actual, character(), info = backend)
    expect_dup_frame_shape(backend, input, actual)
  }

  values <- c(bytes, "x")
  times <- c(0L, 0L, 0L)
  oracle <- dup_frame_capture(dup_frame_call("stringi", values, times))
  expect_length(oracle$events, 2L)
  expect_match(oracle$events[[1L]], "^warning:longer object length")
  expect_match(oracle$events[[2L]], "^error:.*bytes encoding")

  for (backend in c("base", "altrep")) {
    input <- dup_frame_input(backend, values)
    actual <- dup_frame_capture(
      dup_frame_function(backend)(input, times)
    )
    expect_identical(actual, oracle, info = backend)
    expect_dup_frame_shape(backend, input)
  }
})


test_that("duplicate recovers from warning and overflow errors", {
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (backend in c("base", "altrep")) {
    warning_input <- dup_frame_input(backend, c("a", "b", "c"))
    expect_error(
      dup_frame_function(backend)(warning_input, c(1L, 2L)),
      "longer object length",
      info = backend
    )
    expect_dup_frame_shape(backend, warning_input)

    overflow_input <- dup_frame_input(backend, "ab")
    expect_error(
      dup_frame_function(backend)(overflow_input, .Machine$integer.max),
      "limited to 2\\^31-1 bytes",
      info = backend
    )
    expect_dup_frame_shape(backend, overflow_input)

    valid_input <- dup_frame_input(backend, c("a", "bc"))
    actual <- dup_frame_function(backend)(valid_input, c(2L, 1L))
    expect_identical(actual, c("aa", "bc"), info = backend)
    expect_dup_frame_shape(backend, valid_input, actual)
  }
})


test_that("duplicate recycling warnings permit ALTREP reentry", {
  input <- dup_frame_input("altrep", c("a", "b", "c"))
  reentry_input <- dup_frame_input("altrep", c("x", "yz"))
  reentered <- NULL

  actual <- dup_frame_capture(
    dup_frame_function("altrep")(input, c(1L, 2L)),
    warning_handler = function(condition) {
      reentered <<- dup_frame_function("altrep")(
        reentry_input, c(2L, 1L)
      )
    }
  )
  oracle <- dup_frame_capture(
    dup_frame_call("stringi", c("a", "b", "c"), c(1L, 2L))
  )

  expect_identical(actual, oracle)
  expect_identical(reentered, c("xx", "yz"))
  expect_dup_frame_shape("altrep", input, actual$value)
  expect_dup_frame_shape("altrep", reentry_input, reentered)
})
