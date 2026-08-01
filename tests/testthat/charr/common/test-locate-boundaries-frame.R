locate_boundaries_frame_function <- function(backend, operation) {
  name <- paste0("ci_locate_", operation, "_boundaries")
  if (identical(backend, "stringi")) {
    return(get(
      paste0("stri_locate_", operation, "_boundaries"),
      envir = asNamespace("stringi"), inherits = FALSE
    ))
  }
  if (identical(backend, "base")) {
    return(get(
      name, envir = charr:::.charr_base_leaf_environment,
      inherits = FALSE
    ))
  }
  if (identical(backend, "altrep")) {
    return(get(name, envir = asNamespace("charr"), inherits = FALSE))
  }
  stop("unknown boundary-locate backend", call. = FALSE)
}


locate_boundaries_frame_input <- function(backend, value) {
  if (identical(backend, "altrep")) {
    return(charport::as_charvec(value))
  }
  value
}


locate_boundaries_frame_options <- function(backend, options) {
  if (
    identical(backend, "altrep") &&
      is.list(options) &&
      !is.null(options$type)
  ) {
    options$type <- charport::as_charvec(options$type)
  }
  options
}


locate_boundaries_frame_invoke <- function(
  backend, operation, input, options = NULL,
  get_length = FALSE, omit_no_match = FALSE
) {
  fun <- locate_boundaries_frame_function(backend, operation)
  if (identical(operation, "first")) {
    return(fun(
      input, opts_brkiter = options, get_length = get_length
    ))
  }
  fun(
    input, omit_no_match = omit_no_match,
    opts_brkiter = options, get_length = get_length
  )
}


locate_boundaries_frame_capture <- function(expr) {
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


locate_boundaries_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


expect_locate_boundaries_frame_unmaterialized <- function(backend, value) {
  if (!identical(backend, "altrep")) {
    return(invisible(NULL))
  }
  expect_true(charport::is_charvec(value))
  expect_false(charport::charport_info(value)$is_materialized)
  invisible(NULL)
}


test_that("boundary locate preserves iterator types and skip ranges", {
  values <- c(
    "alpha 123 \u65e5\u672c\u8a9e. Next!",
    "One. Two! Three?",
    "\U0001f469\u200d\U0001f469\u200d\U0001f467\u200d\U0001f466e\u0301",
    "", NA_character_
  )
  cases <- list(
    character = list(type = "char"),
    empty_type = list(type = ""),
    line = list(type = "line"),
    sentence = list(type = "sent"),
    word_none = list(type = "wo", skip_word_none = TRUE),
    word_number = list(type = "word", skip_word_number = TRUE),
    word_letter = list(type = "word", skip_word_letter = TRUE),
    word_kana = list(type = "word", skip_word_kana = TRUE),
    word_ideo = list(type = "word", skip_word_ideo = TRUE),
    line_soft = list(type = "line_break", skip_line_soft = TRUE),
    line_hard = list(type = "line_break", skip_line_hard = TRUE),
    sentence_term = list(type = "sentence", skip_sentence_term = TRUE),
    sentence_sep = list(type = "sentence", skip_sentence_sep = TRUE)
  )

  for (case_name in names(cases)) {
    options <- cases[[case_name]]
    for (operation in c("first", "all")) {
      for (get_length in c(FALSE, TRUE)) {
        expected <- locate_boundaries_frame_invoke(
          "stringi", operation, values, options,
          get_length = get_length
        )
        for (backend in c("base", "altrep")) {
          input <- locate_boundaries_frame_input(backend, values)
          backend_options <- locate_boundaries_frame_options(
            backend, options
          )
          actual <- locate_boundaries_frame_invoke(
            backend, operation, input, backend_options,
            get_length = get_length
          )

          expect_identical(
            actual, expected,
            info = paste(backend, operation, case_name, get_length)
          )
          expect_locate_boundaries_frame_unmaterialized(backend, input)
          expect_locate_boundaries_frame_unmaterialized(
            backend, backend_options$type
          )
        }
      }
    }
  }
})


test_that("boundary locate preserves empty and no-match shapes", {
  values <- c("", NA_character_, "abc")
  options <- list(
    type = "word", skip_word_none = TRUE,
    skip_word_letter = TRUE
  )

  for (get_length in c(FALSE, TRUE)) {
    expected_first <- locate_boundaries_frame_invoke(
      "stringi", "first", values, options,
      get_length = get_length
    )
    for (backend in c("base", "altrep")) {
      input <- locate_boundaries_frame_input(backend, values)
      actual <- locate_boundaries_frame_invoke(
        backend, "first", input,
        locate_boundaries_frame_options(backend, options),
        get_length = get_length
      )
      expect_identical(actual, expected_first, info = backend)
      expect_identical(dim(actual), c(3L, 2L))
      expect_identical(
        colnames(actual), c("start", if (get_length) "length" else "end")
      )
      expect_true(all(is.na(actual[2L, ])))
      if (get_length) {
        expect_identical(unname(actual[c(1L, 3L), ]), matrix(
          -1L, nrow = 2L, ncol = 2L
        ))
      } else {
        expect_true(all(is.na(actual[c(1L, 3L), ])))
      }
      expect_locate_boundaries_frame_unmaterialized(backend, input)
    }

    for (omit_no_match in c(FALSE, TRUE)) {
      expected_all <- locate_boundaries_frame_invoke(
        "stringi", "all", values, options,
        get_length = get_length, omit_no_match = omit_no_match
      )
      for (backend in c("base", "altrep")) {
        input <- locate_boundaries_frame_input(backend, values)
        actual <- locate_boundaries_frame_invoke(
          backend, "all", input,
          locate_boundaries_frame_options(backend, options),
          get_length = get_length, omit_no_match = omit_no_match
        )
        expect_identical(actual, expected_all, info = paste(
          backend, get_length, omit_no_match
        ))
        expect_identical(dim(actual[[2L]]), c(1L, 2L))
        expect_true(all(is.na(actual[[2L]])))
        expected_rows <- if (omit_no_match) 0L else 1L
        expect_identical(dim(actual[[1L]]), c(expected_rows, 2L))
        expect_identical(dim(actual[[3L]]), c(expected_rows, 2L))
        expect_locate_boundaries_frame_unmaterialized(backend, input)
      }
    }
  }

  for (operation in c("first", "all")) {
    for (backend in c("base", "altrep")) {
      actual <- locate_boundaries_frame_invoke(
        backend, operation,
        locate_boundaries_frame_input(backend, character())
      )
      expected <- locate_boundaries_frame_invoke(
        "stringi", operation, character()
      )
      expect_identical(actual, expected, info = paste(backend, operation))
    }
  }
})


test_that("first and all boundary locate keep distinct lazy-open rules", {
  bad_rules <- list(type = "[")
  cases <- list(
    first = list(character(), rep(NA_character_, 2L), "", "a"),
    all = list(character(), rep(NA_character_, 2L), "", c(NA, ""))
  )

  for (operation in names(cases)) {
    for (values in cases[[operation]]) {
      expected <- locate_boundaries_frame_capture(
        locate_boundaries_frame_invoke(
          "stringi", operation, values, bad_rules
        )
      )
      for (backend in c("base", "altrep")) {
        input <- locate_boundaries_frame_input(backend, values)
        actual <- locate_boundaries_frame_capture(
          locate_boundaries_frame_invoke(
            backend, operation, input,
            locate_boundaries_frame_options(backend, bad_rules)
          )
        )
        expect_identical(actual, expected, info = paste(backend, operation))
        expect_locate_boundaries_frame_unmaterialized(backend, input)
      }
    }
  }

  expect_identical(
    locate_boundaries_frame_capture(
      locate_boundaries_frame_invoke(
        "stringi", "first", "", bad_rules
      )
    )$events,
    character()
  )
  expect_match(
    locate_boundaries_frame_capture(
      locate_boundaries_frame_invoke(
        "stringi", "all", "", bad_rules
      )
    )$events,
    "^error:"
  )
})


test_that("boundary locate normalizes all input before opening ICU", {
  bytes <- locate_boundaries_frame_marked(c(0xff, 0xfe), "bytes")
  option_cases <- list(
    list(type = "["),
    list(type = "word", locale = "zz_ZZ")
  )

  for (operation in c("first", "all")) {
    for (options in option_cases) {
      expected <- locate_boundaries_frame_capture(
        locate_boundaries_frame_invoke(
          "stringi", operation, c("alpha", bytes), options
        )
      )
      expect_length(expected$events, 1L)
      expect_match(expected$events, "bytes encoding")

      for (backend in c("base", "altrep")) {
        input <- locate_boundaries_frame_input(
          backend, c("alpha", bytes)
        )
        actual <- locate_boundaries_frame_capture(
          locate_boundaries_frame_invoke(
            backend, operation, input,
            locate_boundaries_frame_options(backend, options)
          )
        )
        expect_identical(actual$events, expected$events, info = backend)
        expect_null(actual$value)
        expect_locate_boundaries_frame_unmaterialized(backend, input)

        valid <- locate_boundaries_frame_input(backend, "alpha")
        expect_identical(
          locate_boundaries_frame_invoke(
            backend, operation, valid, list(type = "character")
          ),
          locate_boundaries_frame_invoke(
            "stringi", operation, "alpha", list(type = "character")
          ),
          info = paste(backend, operation)
        )
      }
    }
  }
})


test_that("boundary locate preserves argument condition order", {
  bytes <- locate_boundaries_frame_marked(c(0x77, 0xff), "bytes")
  cases <- list(
    list(
      operation = "first", get_length = c(FALSE, TRUE),
      omit_no_match = FALSE, options = list(type = "word")
    ),
    list(
      operation = "all", get_length = c(FALSE, TRUE),
      omit_no_match = c(FALSE, TRUE), options = list(type = "word")
    ),
    list(
      operation = "first", get_length = FALSE,
      omit_no_match = FALSE, options = list(type = c("word", NA))
    ),
    list(
      operation = "all", get_length = FALSE,
      omit_no_match = FALSE, options = list(type = c(NA, "word"))
    ),
    list(
      operation = "first", get_length = FALSE,
      omit_no_match = FALSE, options = list(type = c("word", bytes))
    )
  )

  for (case in cases) {
    expected <- locate_boundaries_frame_capture(
      locate_boundaries_frame_invoke(
        "stringi", case$operation, "alpha beta", case$options,
        get_length = case$get_length,
        omit_no_match = case$omit_no_match
      )
    )
    for (backend in c("base", "altrep")) {
      input <- locate_boundaries_frame_input(backend, "alpha beta")
      actual <- locate_boundaries_frame_capture(
        locate_boundaries_frame_invoke(
          backend, case$operation, input,
          locate_boundaries_frame_options(backend, case$options),
          get_length = case$get_length,
          omit_no_match = case$omit_no_match
        )
      )
      expect_identical(actual$events, expected$events, info = backend)
      expect_identical(actual$value, expected$value, info = backend)
      expect_locate_boundaries_frame_unmaterialized(backend, input)
    }
  }
})


test_that("boundary locate keeps code-point positions and encoding rules", {
  latin1 <- locate_boundaries_frame_marked(
    c(0x63, 0x61, 0x66, 0xe9), "latin1"
  )
  malformed <- locate_boundaries_frame_marked(
    c(0x61, 0xc3, 0x28, 0x62), "UTF-8"
  )
  values <- c(
    "\U0001f469\u200d\U0001f469\u200d\U0001f467\u200d\U0001f466e\u0301",
    "\u65e5\u672c\u8a9e test", latin1, "\ufeffabc", malformed,
    NA_character_
  )
  options <- list(type = "character")

  for (operation in c("first", "all")) {
    for (get_length in c(FALSE, TRUE)) {
      expected <- locate_boundaries_frame_invoke(
        "stringi", operation, values, options,
        get_length = get_length
      )
      for (backend in c("base", "altrep")) {
        input <- locate_boundaries_frame_input(backend, values)
        actual <- locate_boundaries_frame_invoke(
          backend, operation, input,
          locate_boundaries_frame_options(backend, options),
          get_length = get_length
        )
        expect_identical(
          actual, expected,
          info = paste(backend, operation, get_length)
        )
        expect_locate_boundaries_frame_unmaterialized(backend, input)
      }
    }
  }
})


test_that("boundary locate warning errors clean up before recovery", {
  fallback <- list(type = "word", locale = "zz_ZZ")
  oracle <- locate_boundaries_frame_capture(
    locate_boundaries_frame_invoke(
      "stringi", "first", "alpha beta", fallback
    )
  )
  skip_if_not(
    length(oracle$events) == 1L &&
      startsWith(oracle$events, "warning:"),
    "this ICU installation does not warn for the fallback locale"
  )

  expect_identical(
    locate_boundaries_frame_capture(
      locate_boundaries_frame_invoke(
        "stringi", "first", "", fallback
      )
    )$events,
    character()
  )
  expect_match(
    locate_boundaries_frame_capture(
      locate_boundaries_frame_invoke(
        "stringi", "all", "", fallback
      )
    )$events,
    "^warning:"
  )

  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (operation in c("first", "all")) {
    for (backend in c("base", "altrep")) {
      input <- locate_boundaries_frame_input(backend, "alpha beta")
      expect_error(
        locate_boundaries_frame_invoke(
          backend, operation, input,
          locate_boundaries_frame_options(backend, fallback)
        ),
        info = paste(backend, operation)
      )
      expect_locate_boundaries_frame_unmaterialized(backend, input)

      expect_identical(
        locate_boundaries_frame_invoke(
          backend, operation, input, list(type = "word")
        ),
        locate_boundaries_frame_invoke(
          "stringi", operation, "alpha beta", list(type = "word")
        ),
        info = paste(backend, operation)
      )
    }
  }
})
