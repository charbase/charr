replace_charclass_frame_function <- function(backend) {
  if (identical(backend, "stringi")) {
    return(get(
      "stri_replace_all_charclass",
      envir = asNamespace("stringi"), inherits = FALSE
    ))
  }
  if (identical(backend, "base")) {
    return(get(
      "ci_replace_all_charclass",
      envir = charr:::.charr_base_leaf_environment,
      inherits = FALSE
    ))
  }
  if (identical(backend, "altrep")) {
    return(get(
      "ci_replace_all_charclass",
      envir = asNamespace("charr"), inherits = FALSE
    ))
  }

  stop("unknown character-class replacement backend", call. = FALSE)
}


replace_charclass_frame_inputs <- function(
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


replace_charclass_frame_invoke <- function(
    backend, inputs, merge = FALSE, vectorize_all = TRUE
) {
  replace_charclass_frame_function(backend)(
    inputs$subject,
    inputs$pattern,
    inputs$replacement,
    merge = merge,
    vectorize_all = vectorize_all
  )
}


replace_charclass_frame_capture <- function(expr, warning_handler = NULL) {
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


replace_charclass_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


expect_replace_charclass_frame_shape <- function(
    backend, inputs, output = NULL
) {
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


test_that("character-class replacement preserves both vectorization modes", {
  vectorized_cases <- list(
    list(
      subject = c("aa  ββ", "ab12", "none", NA_character_),
      pattern = c("[aβ]", "[0-9]"),
      replacement = c("-", "#"),
      merge = FALSE
    ),
    list(
      subject = c("aa  ββ", "ab12", "none", NA_character_),
      pattern = c("[aβ]", "[0-9]"),
      replacement = c("-", "#"),
      merge = TRUE
    )
  )

  for (case in vectorized_cases) {
    oracle_inputs <- replace_charclass_frame_inputs(
      "stringi", case$subject, case$pattern, case$replacement
    )
    oracle <- replace_charclass_frame_invoke(
      "stringi", oracle_inputs, merge = case$merge
    )

    for (backend in c("base", "altrep")) {
      inputs <- replace_charclass_frame_inputs(
        backend, case$subject, case$pattern, case$replacement
      )
      actual <- replace_charclass_frame_invoke(
        backend, inputs, merge = case$merge
      )
      expect_replace_charclass_frame_shape(backend, inputs, actual)
      expect_identical(actual, oracle, info = backend)
    }
  }

  subject <- c("ab12", "cd34", NA_character_)
  pattern <- c("[a-z]", "[A-Z]", "[0-9]")
  replacement <- c("A", "x", "N")
  oracle_inputs <- replace_charclass_frame_inputs(
    "stringi", subject, pattern, replacement
  )
  oracle <- replace_charclass_frame_invoke(
    "stringi", oracle_inputs, vectorize_all = FALSE
  )

  for (backend in c("base", "altrep")) {
    inputs <- replace_charclass_frame_inputs(
      backend, subject, pattern, replacement
    )
    actual <- replace_charclass_frame_invoke(
      backend, inputs, vectorize_all = FALSE
    )
    expect_replace_charclass_frame_shape(backend, inputs, actual)
    expect_identical(actual, oracle, info = backend)
  }

  scalar_subject <- c("aaa", "ba", NA_character_)
  scalar_pattern <- "[a]"
  scalar_replacement <- "x"
  scalar_oracle <- replace_charclass_frame_invoke(
    "stringi",
    replace_charclass_frame_inputs(
      "stringi", scalar_subject, scalar_pattern, scalar_replacement
    ),
    vectorize_all = FALSE
  )
  for (backend in c("base", "altrep")) {
    inputs <- replace_charclass_frame_inputs(
      backend, scalar_subject, scalar_pattern, scalar_replacement
    )
    actual <- replace_charclass_frame_invoke(
      backend, inputs, vectorize_all = FALSE
    )
    expect_replace_charclass_frame_shape(backend, inputs, actual)
    expect_identical(actual, scalar_oracle, info = backend)
  }
})


test_that("fully vectorized character-class replacement keeps condition order", {
  bytes <- replace_charclass_frame_marked(c(0xff, 0xfe), "bytes")
  malformed <- replace_charclass_frame_marked(c(0xc3, 0x28), "UTF-8")
  scenarios <- list(
    list("a", "[", "x", NA),
    list(bytes, "[", "x", FALSE),
    list("a", "[", bytes, FALSE),
    list(malformed, "[", "x", FALSE),
    list(character(), "[", "x", FALSE),
    list("a", "[", character(), FALSE)
  )

  for (scenario in scenarios) {
    oracle_inputs <- replace_charclass_frame_inputs(
      "stringi", scenario[[1L]], scenario[[2L]], scenario[[3L]]
    )
    oracle <- replace_charclass_frame_capture(
      replace_charclass_frame_invoke(
        "stringi", oracle_inputs, merge = scenario[[4L]]
      )
    )

    for (backend in c("base", "altrep")) {
      inputs <- replace_charclass_frame_inputs(
        backend, scenario[[1L]], scenario[[2L]], scenario[[3L]]
      )
      actual <- replace_charclass_frame_capture(
        replace_charclass_frame_invoke(
          backend, inputs, merge = scenario[[4L]]
        )
      )
      expect_identical(actual$events, oracle$events, info = backend)
      expect_identical(actual$value, oracle$value, info = backend)
      expect_replace_charclass_frame_shape(backend, inputs)
    }
  }

  eager_oracle <- replace_charclass_frame_capture(
    replace_charclass_frame_invoke(
      "stringi",
      replace_charclass_frame_inputs(
        "stringi", "a", c(NA_character_, "["), c("x", "y")
      ),
      vectorize_all = FALSE
    )
  )
  expect_match(eager_oracle$events, "UnicodeSet pattern is invalid")
  for (backend in c("base", "altrep")) {
    inputs <- replace_charclass_frame_inputs(
      backend, "a", c(NA_character_, "["), c("x", "y")
    )
    actual <- replace_charclass_frame_capture(
      replace_charclass_frame_invoke(
        backend, inputs, vectorize_all = FALSE
      )
    )
    expect_identical(actual, eager_oracle, info = backend)
    expect_replace_charclass_frame_shape(backend, inputs)
  }
})


test_that("sequential character-class replacement keeps condition order", {
  empty_oracle <- replace_charclass_frame_invoke(
    "stringi",
    replace_charclass_frame_inputs(
      "stringi", character(), "[", character()
    ),
    merge = NA,
    vectorize_all = FALSE
  )
  expect_identical(empty_oracle, character())

  for (backend in c("base", "altrep")) {
    inputs <- replace_charclass_frame_inputs(
      backend, character(), "[", character()
    )
    actual <- replace_charclass_frame_invoke(
      backend, inputs, merge = NA, vectorize_all = FALSE
    )
    expect_replace_charclass_frame_shape(backend, inputs, actual)
    expect_identical(actual, empty_oracle, info = backend)
  }

  bytes <- replace_charclass_frame_marked(c(0xff, 0xfe), "bytes")
  malformed <- replace_charclass_frame_marked(c(0xc3, 0x28), "UTF-8")
  scenarios <- list(
    list("a", "[a]", c("x", "y"), NA),
    list("a", c("[a]", "[b]", "[c]"), c("x", "y"), NA),
    list(bytes, "[", "x", FALSE),
    list(malformed, "[", "x", FALSE)
  )

  for (scenario in scenarios) {
    oracle_inputs <- replace_charclass_frame_inputs(
      "stringi", scenario[[1L]], scenario[[2L]], scenario[[3L]]
    )
    oracle <- replace_charclass_frame_capture(
      replace_charclass_frame_invoke(
        "stringi", oracle_inputs,
        merge = scenario[[4L]], vectorize_all = FALSE
      )
    )

    for (backend in c("base", "altrep")) {
      inputs <- replace_charclass_frame_inputs(
        backend, scenario[[1L]], scenario[[2L]], scenario[[3L]]
      )
      actual <- replace_charclass_frame_capture(
        replace_charclass_frame_invoke(
          backend, inputs,
          merge = scenario[[4L]], vectorize_all = FALSE
        )
      )
      expect_identical(actual$events, oracle$events, info = backend)
      expect_identical(actual$value, oracle$value, info = backend)
      expect_replace_charclass_frame_shape(backend, inputs)
    }
  }
})


test_that("character-class replacement preserves missing-value semantics", {
  cases <- list(
    list(
      subject = c("abc", "123", NA_character_),
      pattern = "[a-z]",
      replacement = NA_character_,
      vectorize_all = TRUE
    ),
    list(
      subject = c("abc", "123", NA_character_),
      pattern = c(NA_character_, "[0-9]"),
      replacement = c("x", "y"),
      vectorize_all = FALSE
    ),
    list(
      subject = c("abc", "123", NA_character_),
      pattern = c("[a-z]", "[0-9]"),
      replacement = c(NA_character_, "y"),
      vectorize_all = FALSE
    )
  )

  for (case in cases) {
    oracle_inputs <- replace_charclass_frame_inputs(
      "stringi", case$subject, case$pattern, case$replacement
    )
    oracle <- replace_charclass_frame_invoke(
      "stringi", oracle_inputs, vectorize_all = case$vectorize_all
    )

    for (backend in c("base", "altrep")) {
      inputs <- replace_charclass_frame_inputs(
        backend, case$subject, case$pattern, case$replacement
      )
      actual <- replace_charclass_frame_invoke(
        backend, inputs, vectorize_all = case$vectorize_all
      )
      expect_replace_charclass_frame_shape(backend, inputs, actual)
      expect_identical(actual, oracle, info = backend)
    }
  }
})


test_that("character-class replacement preserves encoded and malformed input", {
  latin1_subject <- iconv(" café1 ", from = "UTF-8", to = "latin1")
  latin1_pattern <- iconv("[é]", from = "UTF-8", to = "latin1")
  latin1_replacement <- iconv("à", from = "UTF-8", to = "latin1")
  Encoding(latin1_subject) <- "latin1"
  Encoding(latin1_pattern) <- "latin1"
  Encoding(latin1_replacement) <- "latin1"

  bom <- enc2utf8("\ufeff")
  cases <- list(
    list(latin1_subject, latin1_pattern, latin1_replacement),
    list(
      paste0(bom, "a1"),
      paste0(bom, "[a]"),
      paste0(bom, "x")
    )
  )

  for (case in cases) {
    oracle_inputs <- replace_charclass_frame_inputs(
      "stringi", case[[1L]], case[[2L]], case[[3L]]
    )
    oracle <- replace_charclass_frame_invoke("stringi", oracle_inputs)

    for (backend in c("base", "altrep")) {
      inputs <- replace_charclass_frame_inputs(
        backend, case[[1L]], case[[2L]], case[[3L]]
      )
      actual <- replace_charclass_frame_invoke(backend, inputs)
      expect_replace_charclass_frame_shape(backend, inputs, actual)
      expect_identical(actual, oracle, info = backend)
      expect_identical(Encoding(actual), Encoding(oracle), info = backend)
    }
  }

  malformed <- replace_charclass_frame_marked(c(0xc3, 0x28), "UTF-8")
  for (backend in c("stringi", "base", "altrep")) {
    inputs <- replace_charclass_frame_inputs(
      backend, malformed, "[a-z]", NA_character_
    )
    expect_error(
      replace_charclass_frame_invoke(backend, inputs),
      "invalid UTF-8 byte sequence",
      info = backend
    )
    expect_replace_charclass_frame_shape(backend, inputs)
  }

  for (subject in c("a", "z")) {
    oracle <- replace_charclass_frame_invoke(
      "stringi",
      replace_charclass_frame_inputs(
        "stringi", subject, "[a]", malformed
      )
    )
    for (backend in c("base", "altrep")) {
      inputs <- replace_charclass_frame_inputs(
        backend, subject, "[a]", malformed
      )
      actual <- replace_charclass_frame_invoke(backend, inputs)
      expect_identical(actual, oracle, info = backend)
      expect_identical(charToRaw(actual), charToRaw(oracle), info = backend)
      expect_replace_charclass_frame_shape(backend, inputs, actual)
    }
  }

  for (merge in c(FALSE, TRUE)) {
    oracle <- replace_charclass_frame_invoke(
      "stringi",
      replace_charclass_frame_inputs("stringi", "aaa", "[a]", ""),
      merge = merge
    )
    for (backend in c("base", "altrep")) {
      inputs <- replace_charclass_frame_inputs(
        backend, "aaa", "[a]", ""
      )
      actual <- replace_charclass_frame_invoke(
        backend, inputs, merge = merge
      )
      expect_identical(actual, oracle, info = backend)
      expect_identical(Encoding(actual), Encoding(oracle), info = backend)
      expect_replace_charclass_frame_shape(backend, inputs, actual)
    }
  }
})


test_that("vectorized recycling warnings precede native errors", {
  oracle <- replace_charclass_frame_capture(
    replace_charclass_frame_invoke(
      "stringi",
      replace_charclass_frame_inputs(
        "stringi", c("a", "b", "c"), c("[", "[a]"), "x"
      )
    )
  )
  expect_length(oracle$events, 2L)
  expect_match(oracle$events[[1L]], "^warning:longer object length")
  expect_match(oracle$events[[2L]], "^error:.*UnicodeSet pattern is invalid")

  for (backend in c("base", "altrep")) {
    inputs <- replace_charclass_frame_inputs(
      backend, c("a", "b", "c"), c("[", "[a]"), "x"
    )
    actual <- replace_charclass_frame_capture(
      replace_charclass_frame_invoke(backend, inputs)
    )
    expect_identical(actual, oracle, info = backend)
    expect_replace_charclass_frame_shape(backend, inputs)
  }
})


test_that("character-class recycling warnings leave the Frame reusable", {
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (backend in c("base", "altrep")) {
    warning_inputs <- replace_charclass_frame_inputs(
      backend,
      c("a1", "b2", "c3"),
      c("[a-z]", "[0-9]"),
      "x"
    )
    expect_error(
      replace_charclass_frame_invoke(backend, warning_inputs),
      "longer object length",
      info = backend
    )
    expect_replace_charclass_frame_shape(backend, warning_inputs)

    valid_inputs <- replace_charclass_frame_inputs(
      backend, c("a1", "b2"), "[a-z]", "x"
    )
    oracle <- replace_charclass_frame_invoke(
      "stringi",
      replace_charclass_frame_inputs(
        "stringi", c("a1", "b2"), "[a-z]", "x"
      )
    )
    actual <- replace_charclass_frame_invoke(backend, valid_inputs)
    expect_replace_charclass_frame_shape(backend, valid_inputs, actual)
    expect_identical(actual, oracle, info = backend)
  }
})


test_that("character-class recycling warnings permit ALTREP reentry", {
  inputs <- replace_charclass_frame_inputs(
    "altrep",
    c("a1", "b2", "c3"),
    c("[a-z]", "[0-9]"),
    "x"
  )
  scalar_pattern <- charport::as_charvec("[a-z]")
  scalar_replacement <- charport::as_charvec("y")
  reentered <- NULL

  actual <- replace_charclass_frame_capture(
    replace_charclass_frame_invoke("altrep", inputs),
    warning_handler = function(condition) {
      reentered <<- charr:::ci_replace_all_charclass(
        inputs$subject, scalar_pattern, scalar_replacement
      )
    }
  )
  oracle <- replace_charclass_frame_capture(
    replace_charclass_frame_invoke(
      "stringi",
      replace_charclass_frame_inputs(
        "stringi",
        c("a1", "b2", "c3"),
        c("[a-z]", "[0-9]"),
        "x"
      )
    )
  )

  expect_identical(actual$events, oracle$events)
  expect_identical(actual$value, oracle$value)
  expect_identical(reentered, c("y1", "y2", "y3"))
  expect_replace_charclass_frame_shape("altrep", inputs, actual$value)
  expect_false(charport::charport_info(scalar_pattern)$is_materialized)
  expect_false(charport::charport_info(scalar_replacement)$is_materialized)
})
