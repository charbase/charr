split_boundaries_frame_function <- function(backend) {
  if (identical(backend, "stringi")) {
    return(stringi::stri_split_boundaries)
  }
  if (identical(backend, "base")) {
    return(get(
      "ci_split_boundaries",
      envir = charr:::.charr_base_leaf_environment,
      inherits = FALSE
    ))
  }
  if (identical(backend, "altrep")) {
    return(get(
      "ci_split_boundaries", envir = asNamespace("charr"),
      inherits = FALSE
    ))
  }
  stop("unknown boundary-split backend", call. = FALSE)
}


split_boundaries_frame_input <- function(backend, value) {
  if (identical(backend, "altrep")) {
    return(charport::as_charvec(value))
  }
  value
}


split_boundaries_frame_options <- function(backend, options) {
  if (
    identical(backend, "altrep") &&
      is.list(options) &&
      !is.null(options$type)
  ) {
    options$type <- charport::as_charvec(options$type)
  }
  options
}


split_boundaries_frame_invoke <- function(
  backend, input, n = -1L, tokens_only = FALSE,
  simplify = FALSE, options = NULL
) {
  split_boundaries_frame_function(backend)(
    input, n = n, tokens_only = tokens_only,
    simplify = simplify, opts_brkiter = options
  )
}


split_boundaries_frame_capture <- function(expr) {
  events <- character()
  value <- tryCatch(
    withCallingHandlers(
      force(expr),
      warning = function(condition) {
        events <<- c(
          events, paste0("warning:", conditionMessage(condition))
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


split_boundaries_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


expect_split_boundaries_frame_unmaterialized <- function(backend, value) {
  if (!identical(backend, "altrep")) {
    return(invisible(NULL))
  }
  expect_true(charport::is_charvec(value))
  expect_false(charport::charport_info(value)$is_materialized)
  invisible(NULL)
}


expect_split_boundaries_frame_output <- function(
  backend, value, simplify
) {
  if (identical(backend, "altrep")) {
    if (identical(simplify, FALSE)) {
      for (element in value) {
        expect_split_boundaries_frame_unmaterialized(backend, element)
      }
    } else {
      expect_split_boundaries_frame_unmaterialized(backend, value)
    }
  } else if (identical(simplify, FALSE)) {
    for (element in value) {
      expect_false(charport::is_charvec(element))
    }
  } else {
    expect_false(charport::is_charvec(value))
  }
  invisible(NULL)
}


test_that("boundary split preserves iterator types and skip ranges", {
  values <- c(
    "alpha 123 \u65e5\u672c\u8a9e. Next!",
    "One. Two! Three?",
    "a b\nnext line", "", NA_character_
  )
  rules <- paste0(
    "$letters = [[:L:]]; $numbers = [[:N:]]; ",
    "$letters+; $numbers+; .;"
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
    list(type = "sentence", skip_sentence_sep = TRUE),
    list(type = ""),
    list(type = rules)
  )

  for (options in cases) {
    expected <- split_boundaries_frame_invoke(
      "stringi", values, options = options
    )
    for (backend in c("base", "altrep")) {
      input <- split_boundaries_frame_input(backend, values)
      backend_options <- split_boundaries_frame_options(backend, options)
      actual <- split_boundaries_frame_invoke(
        backend, input, options = backend_options
      )

      expect_identical(actual, expected, info = backend)
      expect_split_boundaries_frame_unmaterialized(backend, input)
      expect_split_boundaries_frame_unmaterialized(
        backend, backend_options$type
      )
      expect_split_boundaries_frame_output(backend, actual, FALSE)
    }
  }
})


test_that("boundary split preserves n, token caps, and matrix shapes", {
  values <- c("abc def", "", NA_character_, "123 456")
  options <- list(type = "word", skip_word_none = TRUE)
  cases <- list(
    list(n = -1L, tokens_only = FALSE, simplify = FALSE),
    list(n = 0L, tokens_only = FALSE, simplify = FALSE),
    list(n = 1L, tokens_only = FALSE, simplify = FALSE),
    list(n = 1L, tokens_only = TRUE, simplify = FALSE),
    list(n = 2L, tokens_only = FALSE, simplify = FALSE),
    list(n = 2L, tokens_only = TRUE, simplify = FALSE),
    list(
      n = c(1L, 2L, NA_integer_), tokens_only = FALSE,
      simplify = FALSE
    ),
    list(n = 3L, tokens_only = FALSE, simplify = TRUE),
    list(n = 3L, tokens_only = FALSE, simplify = NA)
  )

  for (case in cases) {
    expected <- split_boundaries_frame_capture(
      split_boundaries_frame_invoke(
        "stringi", values, n = case$n,
        tokens_only = case$tokens_only, simplify = case$simplify,
        options = options
      )
    )
    for (backend in c("base", "altrep")) {
      input <- split_boundaries_frame_input(backend, values)
      actual <- split_boundaries_frame_capture(
        split_boundaries_frame_invoke(
          backend, input, n = case$n,
          tokens_only = case$tokens_only, simplify = case$simplify,
          options = split_boundaries_frame_options(backend, options)
        )
      )

      expect_identical(actual$events, expected$events, info = backend)
      expect_identical(actual$value, expected$value, info = backend)
      expect_split_boundaries_frame_output(
        backend, actual$value, case$simplify
      )
      expect_split_boundaries_frame_unmaterialized(backend, input)
    }
  }

  shape_cases <- list(
    list(values = character(), n = 3L, simplify = TRUE),
    list(values = c("a", "b"), n = integer(), simplify = TRUE),
    list(values = NA_character_, n = 0L, simplify = TRUE),
    list(values = NA_character_, n = 0L, simplify = NA),
    list(values = "", n = 0L, simplify = TRUE),
    list(
      values = c("a b", "c d", "e f"),
      n = c(-1L, 0L, NA_integer_), simplify = TRUE
    )
  )
  for (case in shape_cases) {
    expected <- split_boundaries_frame_invoke(
      "stringi", case$values, n = case$n,
      simplify = case$simplify, options = options
    )
    for (backend in c("base", "altrep")) {
      input <- split_boundaries_frame_input(backend, case$values)
      actual <- split_boundaries_frame_invoke(
        backend, input, n = case$n, simplify = case$simplify,
        options = split_boundaries_frame_options(backend, options)
      )
      expect_identical(actual, expected, info = backend)
      expect_split_boundaries_frame_output(
        backend, actual, case$simplify
      )
      expect_split_boundaries_frame_unmaterialized(backend, input)
    }
  }
})


test_that("boundary split normalizes active input before lane processing", {
  bytes <- split_boundaries_frame_marked(c(0xff, 0xfe), "bytes")
  bad_rules <- list(type = "[")
  valid_options <- list(type = "character")
  cases <- list(
    list(values = bytes, n = NA_integer_, options = valid_options),
    list(values = bytes, n = 0L, options = valid_options),
    list(values = bytes, n = .Machine$integer.max, options = valid_options),
    list(
      values = c("alpha", bytes), n = c(1L, .Machine$integer.max),
      options = bad_rules
    )
  )

  for (case in cases) {
    expected <- split_boundaries_frame_capture(
      split_boundaries_frame_invoke(
        "stringi", case$values, n = case$n, options = case$options
      )
    )
    expect_match(expected$events, "bytes encoding")

    for (backend in c("base", "altrep")) {
      input <- split_boundaries_frame_input(backend, case$values)
      actual <- split_boundaries_frame_capture(
        split_boundaries_frame_invoke(
          backend, input, n = case$n,
          options = split_boundaries_frame_options(
            backend, case$options
          )
        )
      )
      expect_identical(actual$events, expected$events, info = backend)
      expect_null(actual$value)
      expect_split_boundaries_frame_unmaterialized(backend, input)
    }
  }
})


test_that("boundary split keeps zero recycling and iterator opening lazy", {
  bad_rules <- list(type = "[")
  cases <- list(
    list(values = character(), n = 1L),
    list(values = "alpha", n = integer()),
    list(values = "alpha", n = 0L),
    list(values = NA_character_, n = 1L),
    list(values = NA_character_, n = NA_integer_),
    list(values = "", n = 1L),
    list(values = "alpha", n = .Machine$integer.max),
    list(
      values = c("alpha", "beta"),
      n = c(1L, .Machine$integer.max)
    )
  )

  for (case in cases) {
    expected <- split_boundaries_frame_capture(
      split_boundaries_frame_invoke(
        "stringi", case$values, n = case$n, options = bad_rules
      )
    )
    for (backend in c("base", "altrep")) {
      input <- split_boundaries_frame_input(backend, case$values)
      actual <- split_boundaries_frame_capture(
        split_boundaries_frame_invoke(
          backend, input, n = case$n,
          options = split_boundaries_frame_options(backend, bad_rules)
        )
      )
      expect_identical(actual$events, expected$events, info = backend)
      expect_identical(actual$value, expected$value, info = backend)
      expect_split_boundaries_frame_unmaterialized(backend, input)
    }
  }
})


test_that("boundary split preserves marked input and output encodings", {
  latin1 <- split_boundaries_frame_marked(
    c(0x63, 0x61, 0x66, 0xe9, 0x20, 0x78), "latin1"
  )
  malformed <- split_boundaries_frame_marked(
    c(0x61, 0xc3, 0x28, 0x62), "UTF-8"
  )
  values <- c(latin1, "\ufeffabc x", malformed, "", NA_character_)
  options <- list(type = "character")
  expected <- split_boundaries_frame_invoke(
    "stringi", values, options = options
  )

  for (backend in c("base", "altrep")) {
    input <- split_boundaries_frame_input(backend, values)
    actual <- split_boundaries_frame_invoke(
      backend, input,
      options = split_boundaries_frame_options(backend, options)
    )
    expect_identical(actual, expected, info = backend)
    expect_identical(lapply(actual, Encoding), lapply(expected, Encoding))
    expect_split_boundaries_frame_output(backend, actual, FALSE)
    expect_split_boundaries_frame_unmaterialized(backend, input)
  }
})


test_that("boundary split preserves preparation warning order", {
  values <- c("alpha", "beta", "gamma")
  n <- 1:2
  options <- list(
    type = c("word", "character"),
    locale = c("en", "fr"),
    skip_word_none = c(TRUE, FALSE)
  )
  expected <- split_boundaries_frame_capture(
    split_boundaries_frame_invoke(
      "stringi", values, n = n, options = options
    )
  )
  expect_gte(length(expected$events), 4L)

  for (backend in c("base", "altrep")) {
    input <- split_boundaries_frame_input(backend, values)
    actual <- split_boundaries_frame_capture(
      split_boundaries_frame_invoke(
        backend, input, n = n,
        options = split_boundaries_frame_options(backend, options)
      )
    )
    expect_identical(actual$events, expected$events, info = backend)
    expect_identical(actual$value, expected$value, info = backend)
    expect_split_boundaries_frame_unmaterialized(backend, input)
  }
})


test_that("boundary split warning errors clean up before recovery", {
  bad_rules <- list(type = "[")
  fallback <- list(type = "word", locale = "zz_ZZ")
  fallback_oracle <- split_boundaries_frame_capture(
    split_boundaries_frame_invoke(
      "stringi", "alpha beta", options = fallback
    )
  )

  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (backend in c("base", "altrep")) {
    recycling_input <- split_boundaries_frame_input(
      backend, c("alpha", "beta")
    )
    expect_error(
      split_boundaries_frame_invoke(
        backend, recycling_input, n = c(1L, 1L, 1L),
        options = split_boundaries_frame_options(backend, bad_rules)
      ),
      "converted from warning",
      info = paste(backend, "recycling")
    )
    expect_identical(
      split_boundaries_frame_invoke(
        backend,
        split_boundaries_frame_input(backend, "alpha beta"),
        options = split_boundaries_frame_options(
          backend, list(type = "word", skip_word_none = TRUE)
        )
      ),
      list(c("alpha", "beta")),
      info = paste(backend, "recycling recovery")
    )

    if (
      length(fallback_oracle$events) == 1L &&
        startsWith(fallback_oracle$events, "warning:")
    ) {
      fallback_input <- split_boundaries_frame_input(
        backend, c("alpha", "beta")
      )
      expect_error(
        split_boundaries_frame_invoke(
          backend, fallback_input,
          n = c(1L, .Machine$integer.max),
          options = split_boundaries_frame_options(backend, fallback)
        ),
        "converted from warning",
        info = paste(backend, "fallback")
      )
      expect_split_boundaries_frame_unmaterialized(
        backend, fallback_input
      )
    }

    expect_identical(
      split_boundaries_frame_invoke(
        backend,
        split_boundaries_frame_input(backend, "abc"),
        options = split_boundaries_frame_options(
          backend, list(type = "character")
        )
      ),
      list(c("a", "b", "c")),
      info = paste(backend, "final recovery")
    )
    expect_split_boundaries_frame_unmaterialized(
      backend, recycling_input
    )
  }
})
