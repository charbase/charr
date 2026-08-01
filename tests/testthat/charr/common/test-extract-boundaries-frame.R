extract_boundaries_frame_function <- function(backend, operation) {
  name <- paste0("ci_extract_", operation, "_boundaries")
  if (identical(backend, "stringi")) {
    return(get(
      paste0("stri_extract_", operation, "_boundaries"),
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
  stop("unknown boundary-extract backend", call. = FALSE)
}


extract_boundaries_frame_input <- function(backend, value) {
  if (identical(backend, "altrep")) {
    return(charport::as_charvec(value))
  }
  value
}


extract_boundaries_frame_options <- function(backend, options) {
  if (
    identical(backend, "altrep") &&
      is.list(options) &&
      !is.null(options$type)
  ) {
    options$type <- charport::as_charvec(options$type)
  }
  options
}


extract_boundaries_frame_invoke <- function(
  backend, operation, input, options = NULL,
  simplify = FALSE, omit_no_match = FALSE
) {
  fun <- extract_boundaries_frame_function(backend, operation)
  if (identical(operation, "first")) {
    return(fun(input, opts_brkiter = options))
  }
  fun(
    input, simplify = simplify, omit_no_match = omit_no_match,
    opts_brkiter = options
  )
}


expect_extract_boundaries_frame_unmaterialized <- function(backend, value) {
  if (!identical(backend, "altrep")) {
    return(invisible(NULL))
  }
  expect_true(charport::is_charvec(value))
  expect_false(charport::charport_info(value)$is_materialized)
  invisible(NULL)
}


expect_extract_boundaries_frame_output <- function(
  backend, value, list_output = FALSE
) {
  if (list_output) {
    for (element in value) {
      if (identical(backend, "altrep")) {
        expect_extract_boundaries_frame_unmaterialized(backend, element)
      } else {
        expect_false(charport::is_charvec(element))
      }
    }
    return(invisible(NULL))
  }

  if (identical(backend, "altrep")) {
    expect_extract_boundaries_frame_unmaterialized(backend, value)
  } else {
    expect_false(charport::is_charvec(value))
  }
  invisible(NULL)
}


extract_boundaries_frame_capture <- function(expr) {
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


extract_boundaries_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


test_that("boundary extraction preserves iterator types and skip ranges", {
  values <- c(
    "alpha 123 \u65e5\u672c\u8a9e. Next!",
    "One. Two! Three?",
    "\U0001f469\u200d\U0001f469\u200d\U0001f467\u200d\U0001f466e\u0301",
    "", NA_character_
  )
  cases <- list(
    list(type = "char"),
    list(type = "line"),
    list(type = "sent"),
    list(type = "wo", skip_word_none = TRUE),
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
    for (operation in c("first", "all")) {
      expected <- extract_boundaries_frame_invoke(
        "stringi", operation, values, options
      )
      for (backend in c("base", "altrep")) {
        input <- extract_boundaries_frame_input(backend, values)
        backend_options <- extract_boundaries_frame_options(
          backend, options
        )
        actual <- extract_boundaries_frame_invoke(
          backend, operation, input, backend_options
        )

        expect_identical(
          actual, expected,
          info = paste(backend, operation, names(options)[[1L]])
        )
        expect_extract_boundaries_frame_unmaterialized(backend, input)
        expect_extract_boundaries_frame_unmaterialized(
          backend, backend_options$type
        )
        expect_extract_boundaries_frame_output(
          backend, actual, list_output = identical(operation, "all")
        )
      }
    }
  }
})


test_that("boundary extraction preserves custom rules and marked input", {
  latin1 <- extract_boundaries_frame_marked(
    c(0x63, 0x61, 0x66, 0xe9), "latin1"
  )
  malformed <- extract_boundaries_frame_marked(
    c(0x61, 0xc3, 0x28, 0x62), "UTF-8"
  )
  rules <- paste0(
    "$letters = [[:L:]]; $numbers = [[:N:]]; ",
    "$letters+; $numbers+; .;"
  )
  values <- c(latin1, "\ufeffabc 123", malformed, "", NA_character_)
  cases <- list(list(type = ""), list(type = rules))

  for (options in cases) {
    for (operation in c("first", "all")) {
      expected <- extract_boundaries_frame_invoke(
        "stringi", operation, values, options
      )
      for (backend in c("base", "altrep")) {
        input <- extract_boundaries_frame_input(backend, values)
        backend_options <- extract_boundaries_frame_options(
          backend, options
        )
        actual <- extract_boundaries_frame_invoke(
          backend, operation, input, backend_options
        )

        expect_identical(actual, expected, info = paste(backend, operation))
        expect_extract_boundaries_frame_unmaterialized(backend, input)
        expect_extract_boundaries_frame_output(
          backend, actual, list_output = identical(operation, "all")
        )
      }
    }
  }
})


test_that("boundary extraction keeps first and all lazy-open rules distinct", {
  bad_rules <- list(type = "[")
  first_cases <- list(character(), rep(NA_character_, 2L), "", "a")
  all_cases <- list(character(), rep(NA_character_, 2L), "", c(NA, ""))

  for (operation in c("first", "all")) {
    cases <- if (identical(operation, "first")) first_cases else all_cases
    for (values in cases) {
      expected <- extract_boundaries_frame_capture(
        extract_boundaries_frame_invoke(
          "stringi", operation, values, bad_rules
        )
      )
      for (backend in c("base", "altrep")) {
        input <- extract_boundaries_frame_input(backend, values)
        options <- extract_boundaries_frame_options(backend, bad_rules)
        actual <- extract_boundaries_frame_capture(
          extract_boundaries_frame_invoke(
            backend, operation, input, options
          )
        )

        expect_identical(actual$events, expected$events, info = backend)
        expect_identical(actual$value, expected$value, info = backend)
        expect_extract_boundaries_frame_unmaterialized(backend, input)
      }
    }
  }

  expect_identical(
    extract_boundaries_frame_capture(
      extract_boundaries_frame_invoke("stringi", "first", "", bad_rules)
    )$events,
    character()
  )
  expect_match(
    extract_boundaries_frame_capture(
      extract_boundaries_frame_invoke("stringi", "all", "", bad_rules)
    )$events,
    "^error:"
  )
})


test_that("boundary extraction normalizes all input before opening ICU", {
  bytes <- extract_boundaries_frame_marked(c(0xff, 0xfe), "bytes")
  bad_rules <- list(type = "[")

  for (operation in c("first", "all")) {
    expected <- extract_boundaries_frame_capture(
      extract_boundaries_frame_invoke(
        "stringi", operation, c("alpha", bytes), bad_rules
      )
    )
    expect_match(expected$events, "bytes encoding")

    for (backend in c("base", "altrep")) {
      input <- extract_boundaries_frame_input(
        backend, c("alpha", bytes)
      )
      options <- extract_boundaries_frame_options(backend, bad_rules)
      actual <- extract_boundaries_frame_capture(
        extract_boundaries_frame_invoke(
          backend, operation, input, options
        )
      )

      expect_identical(actual$events, expected$events, info = backend)
      expect_null(actual$value)
      expect_extract_boundaries_frame_unmaterialized(backend, input)

      valid <- extract_boundaries_frame_input(backend, c("alpha", ""))
      expect_identical(
        extract_boundaries_frame_invoke(
          backend, operation, valid, list(type = "character")
        ),
        extract_boundaries_frame_invoke(
          "stringi", operation, c("alpha", ""),
          list(type = "character")
        ),
        info = backend
      )
    }
  }
})


test_that("all-boundary extraction preserves list and matrix shapes", {
  cases <- list(
    list(values = character(), omit = FALSE),
    list(values = c("abc", "123"), omit = TRUE),
    list(values = c(NA_character_, "abc", ""), omit = TRUE)
  )
  options <- list(
    type = "word", skip_word_none = TRUE,
    skip_word_letter = TRUE, skip_word_number = TRUE
  )

  for (case in cases) {
    for (simplify in list(FALSE, TRUE, NA)) {
      expected <- extract_boundaries_frame_invoke(
        "stringi", "all", case$values, options,
        simplify = simplify, omit_no_match = case$omit
      )

      for (backend in c("base", "altrep")) {
        input <- extract_boundaries_frame_input(backend, case$values)
        actual <- extract_boundaries_frame_invoke(
          backend, "all", input,
          extract_boundaries_frame_options(backend, options),
          simplify = simplify, omit_no_match = case$omit
        )

        expect_identical(actual, expected, info = paste(backend, simplify))
        expect_extract_boundaries_frame_output(
          backend, actual, list_output = identical(simplify, FALSE)
        )
        expect_extract_boundaries_frame_unmaterialized(backend, input)
      }
    }
  }
})


test_that("boundary extraction warning errors clean up before recovery", {
  fallback <- list(type = "word", locale = "zz_ZZ")
  oracle <- extract_boundaries_frame_capture(
    extract_boundaries_frame_invoke(
      "stringi", "first", "alpha beta", fallback
    )
  )
  skip_if_not(
    length(oracle$events) == 1L &&
      startsWith(oracle$events, "warning:"),
    "this ICU installation does not warn for the fallback locale"
  )

  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (operation in c("first", "all")) {
    for (backend in c("base", "altrep")) {
      input <- extract_boundaries_frame_input(backend, "alpha beta")
      expect_error(
        extract_boundaries_frame_invoke(
          backend, operation, input,
          extract_boundaries_frame_options(backend, fallback)
        ),
        info = paste(backend, operation)
      )
      expect_extract_boundaries_frame_unmaterialized(backend, input)

      expect_identical(
        extract_boundaries_frame_invoke(
          backend, operation, input, list(type = "word")
        ),
        extract_boundaries_frame_invoke(
          "stringi", operation, "alpha beta", list(type = "word")
        ),
        info = paste(backend, operation)
      )
    }
  }
})
