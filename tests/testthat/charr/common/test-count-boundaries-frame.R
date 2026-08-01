count_boundaries_frame_function <- function(backend) {
  if (identical(backend, "stringi")) {
    return(stringi::stri_count_boundaries)
  }

  if (identical(backend, "base")) {
    return(get(
      "ci_count_boundaries",
      envir = charr:::.charr_base_leaf_environment,
      inherits = FALSE
    ))
  }

  if (identical(backend, "altrep")) {
    return(get(
      "ci_count_boundaries",
      envir = asNamespace("charr"),
      inherits = FALSE
    ))
  }

  stop("unknown boundary-count backend", call. = FALSE)
}


count_boundaries_frame_input <- function(backend, value) {
  if (identical(backend, "altrep")) {
    return(charport::as_charvec(value))
  }
  value
}


count_boundaries_frame_options <- function(backend, options) {
  if (
    identical(backend, "altrep") &&
      is.list(options) &&
      !is.null(options$type)
  ) {
    options$type <- charport::as_charvec(options$type)
  }
  options
}


count_boundaries_frame_invoke <- function(backend, input, options = NULL) {
  count_boundaries_frame_function(backend)(
    input, opts_brkiter = options
  )
}


expect_count_boundaries_frame_unmaterialized <- function(backend, value) {
  if (!identical(backend, "altrep")) {
    return(invisible(NULL))
  }

  expect_true(charport::is_charvec(value))
  expect_false(charport::charport_info(value)$is_materialized)
  invisible(NULL)
}


expect_count_boundaries_frame_result <- function(value) {
  expect_type(value, "integer")
  expect_null(attributes(value))
  invisible(NULL)
}


count_boundaries_frame_capture <- function(expr) {
  events <- character()
  value <- tryCatch(
    withCallingHandlers(
      force(expr),
      warning = function(condition) {
        events <<- c(events, "warning")
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


count_boundaries_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


test_that("boundary count preserves built-in types and prefix matching", {
  values <- c(
    "alpha 123 \u65e5\u672c\u8a9e. Next!",
    "One. Two! Three?",
    "\U0001f469\u200d\U0001f469\u200d\U0001f467\u200d\U0001f466e\u0301",
    "",
    NA_character_
  )
  types <- list(
    character = c("character", "char"),
    line_break = c("line_break", "line"),
    sentence = c("sentence", "sent"),
    word = c("word", "wo")
  )

  for (type_group in types) {
    expected <- count_boundaries_frame_function("stringi")(
      values, opts_brkiter = list(type = type_group[[1L]])
    )

    for (type in type_group) {
      expect_identical(
        count_boundaries_frame_function("stringi")(
          values, opts_brkiter = list(type = type)
        ),
        expected
      )

      for (backend in c("base", "altrep")) {
        input <- count_boundaries_frame_input(backend, values)
        options <- count_boundaries_frame_options(
          backend, list(type = type)
        )
        actual <- count_boundaries_frame_invoke(
          backend, input, options
        )

        expect_identical(actual, expected, info = paste(backend, type))
        expect_count_boundaries_frame_result(actual)
        expect_count_boundaries_frame_unmaterialized(backend, input)
        expect_count_boundaries_frame_unmaterialized(
          backend, options$type
        )
      }
    }
  }
})


test_that("boundary count preserves every iterator skip flag", {
  values <- c(
    "alpha 123 \u65e5\u672c\u8a9e. Next!",
    "a b\nnext line",
    "",
    NA_character_
  )
  cases <- list(
    list(type = "word", skip_word_none = TRUE),
    list(type = "word", skip_word_number = TRUE),
    list(type = "word", skip_word_letter = TRUE),
    list(type = "word", skip_word_kana = TRUE),
    list(type = "word", skip_word_ideo = TRUE),
    list(type = "line_break", skip_line_soft = TRUE),
    list(type = "line_break", skip_line_hard = TRUE),
    list(type = "sentence", skip_sentence_term = TRUE),
    list(type = "sentence", skip_sentence_sep = TRUE)
  )

  for (options in cases) {
    expected <- count_boundaries_frame_function("stringi")(
      values, opts_brkiter = options
    )

    for (backend in c("base", "altrep")) {
      input <- count_boundaries_frame_input(backend, values)
      backend_options <- count_boundaries_frame_options(backend, options)
      actual <- count_boundaries_frame_invoke(
        backend, input, backend_options
      )

      expect_identical(
        actual, expected,
        info = paste(backend, names(options)[[2L]])
      )
      expect_count_boundaries_frame_result(actual)
      expect_count_boundaries_frame_unmaterialized(backend, input)
      expect_count_boundaries_frame_unmaterialized(
        backend, backend_options$type
      )
    }
  }
})


test_that("boundary count preserves default, empty, and custom rules", {
  values <- c("abc 123", "\u00e942", "", NA_character_)
  rules <- paste0(
    "$letters = [[:L:]]; $numbers = [[:N:]]; ",
    "$letters+; $numbers+; .;"
  )
  cases <- list(
    NULL,
    list(type = ""),
    list(type = rules)
  )

  for (options in cases) {
    expected <- count_boundaries_frame_function("stringi")(
      values, opts_brkiter = options
    )

    for (backend in c("base", "altrep")) {
      input <- count_boundaries_frame_input(backend, values)
      backend_options <- count_boundaries_frame_options(backend, options)
      actual <- count_boundaries_frame_invoke(
        backend, input, backend_options
      )

      expect_identical(actual, expected, info = backend)
      expect_count_boundaries_frame_result(actual)
      expect_count_boundaries_frame_unmaterialized(backend, input)
      if (!is.null(backend_options)) {
        expect_count_boundaries_frame_unmaterialized(
          backend, backend_options$type
        )
      }
    }
  }
})


test_that("boundary type validation preserves full-vector condition order", {
  bytes <- count_boundaries_frame_marked(c(0x77, 0xff), "bytes")
  cases <- list(
    c("word", "character"),
    c("word", NA_character_),
    c(NA_character_, "word"),
    c("word", bytes),
    character(),
    NA_character_
  )

  for (type in cases) {
    expected <- count_boundaries_frame_capture(
      count_boundaries_frame_function("stringi")(
        "alpha beta", opts_brkiter = list(type = type)
      )
    )

    for (backend in c("base", "altrep")) {
      input <- count_boundaries_frame_input(backend, "alpha beta")
      options <- count_boundaries_frame_options(
        backend, list(type = type)
      )
      actual <- count_boundaries_frame_capture(
        count_boundaries_frame_invoke(backend, input, options)
      )

      expect_identical(actual$events, expected$events, info = backend)
      expect_identical(actual$value, expected$value, info = backend)
      if (!is.null(actual$value)) {
        expect_count_boundaries_frame_result(actual$value)
      }
      expect_count_boundaries_frame_unmaterialized(backend, input)
      expect_count_boundaries_frame_unmaterialized(
        backend, options$type
      )
    }
  }

  expect_identical(
    count_boundaries_frame_capture(
      count_boundaries_frame_function("stringi")(
        "alpha beta", opts_brkiter = list(type = c("word", bytes))
      )
    )$events,
    "error"
  )
})


test_that("invalid custom rules are opened only for nonmissing input", {
  cases <- list(
    character(),
    rep(NA_character_, 2L),
    "",
    c(NA_character_, "")
  )
  bad_rules <- list(type = "[")

  for (values in cases) {
    expected <- count_boundaries_frame_capture(
      count_boundaries_frame_function("stringi")(
        values, opts_brkiter = bad_rules
      )
    )

    for (backend in c("base", "altrep")) {
      input <- count_boundaries_frame_input(backend, values)
      options <- count_boundaries_frame_options(backend, bad_rules)
      actual <- count_boundaries_frame_capture(
        count_boundaries_frame_invoke(backend, input, options)
      )

      expect_identical(actual$events, expected$events, info = backend)
      expect_identical(actual$value, expected$value, info = backend)
      if (!is.null(actual$value)) {
        expect_count_boundaries_frame_result(actual$value)
      }
      expect_count_boundaries_frame_unmaterialized(backend, input)
      expect_count_boundaries_frame_unmaterialized(
        backend, options$type
      )
    }
  }

  expect_identical(
    count_boundaries_frame_capture(
      count_boundaries_frame_function("stringi")(
        character(), opts_brkiter = bad_rules
      )
    )$events,
    character()
  )
  expect_identical(
    count_boundaries_frame_capture(
      count_boundaries_frame_function("stringi")(
        rep(NA_character_, 2L), opts_brkiter = bad_rules
      )
    )$events,
    character()
  )
  expect_identical(
    count_boundaries_frame_capture(
      count_boundaries_frame_function("stringi")(
        "", opts_brkiter = bad_rules
      )
    )$events,
    "error"
  )
})


test_that("boundary count recovers after bytes and warning errors", {
  bytes <- count_boundaries_frame_marked(c(0xff, 0xfe), "bytes")

  for (backend in c("base", "altrep")) {
    invalid <- count_boundaries_frame_input(backend, c(bytes, "abc"))
    expect_error(
      count_boundaries_frame_invoke(backend, invalid),
      "bytes encoding"
    )
    expect_count_boundaries_frame_unmaterialized(backend, invalid)

    valid <- count_boundaries_frame_input(backend, c("abc", ""))
    actual <- count_boundaries_frame_invoke(
      backend, valid, list(type = "character")
    )
    expect_identical(actual, c(3L, 0L), info = backend)
    expect_count_boundaries_frame_result(actual)
    expect_count_boundaries_frame_unmaterialized(backend, valid)
  }

  fallback <- list(type = "word", locale = "zz_ZZ")
  oracle <- count_boundaries_frame_capture(
    count_boundaries_frame_function("stringi")(
      "abc", opts_brkiter = fallback
    )
  )
  skip_if_not(
    identical(oracle$events, "warning"),
    "this ICU installation does not warn for the fallback locale"
  )

  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (backend in c("base", "altrep")) {
    invalid <- count_boundaries_frame_input(backend, "abc")
    options <- count_boundaries_frame_options(backend, fallback)
    expect_error(
      count_boundaries_frame_invoke(backend, invalid, options),
      info = backend
    )
    expect_count_boundaries_frame_unmaterialized(backend, invalid)
    expect_count_boundaries_frame_unmaterialized(
      backend, options$type
    )

    valid <- count_boundaries_frame_input(backend, "alpha beta")
    actual <- count_boundaries_frame_invoke(
      backend, valid, list(type = "word", locale = "en")
    )
    expect_identical(actual, 3L, info = backend)
    expect_count_boundaries_frame_result(actual)
    expect_count_boundaries_frame_unmaterialized(backend, valid)
  }
})
