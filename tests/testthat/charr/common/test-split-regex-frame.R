split_regex_frame_function <- function(backend) {
  if (identical(backend, "stringi")) {
    return(stringi::stri_split_regex)
  }
  if (identical(backend, "base")) {
    return(get(
      "ci_split_regex",
      envir = charr:::.charr_base_leaf_environment,
      inherits = FALSE
    ))
  }
  if (identical(backend, "altrep")) {
    return(get(
      "ci_split_regex", envir = asNamespace("charr"),
      inherits = FALSE
    ))
  }
  stop("unknown regex-split backend", call. = FALSE)
}


split_regex_frame_inputs <- function(backend, subject, pattern) {
  if (identical(backend, "altrep")) {
    subject <- charport::as_charvec(subject)
    pattern <- charport::as_charvec(pattern)
  }
  list(subject = subject, pattern = pattern)
}


split_regex_frame_invoke <- function(
    backend, inputs, n = -1L, omit_empty = FALSE,
    tokens_only = FALSE, simplify = FALSE, options = NULL
) {
  split_regex_frame_function(backend)(
    inputs$subject, inputs$pattern, n = n,
    omit_empty = omit_empty, tokens_only = tokens_only,
    simplify = simplify, opts_regex = options
  )
}


split_regex_frame_capture <- function(expr) {
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


split_regex_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


expect_split_regex_frame_shape <- function(
    backend, inputs, output, simplify
) {
  if (identical(backend, "altrep")) {
    expect_true(charport::is_charvec(inputs$subject))
    expect_true(charport::is_charvec(inputs$pattern))
    expect_false(charport::charport_info(inputs$subject)$is_materialized)
    expect_false(charport::charport_info(inputs$pattern)$is_materialized)
    if (!is.null(output)) {
      if (identical(simplify, FALSE)) {
        expect_true(all(vapply(
          output, charport::is_charvec, logical(1)
        )))
        expect_true(all(vapply(
          output,
          function(value) {
            !charport::charport_info(value)$is_materialized
          },
          logical(1)
        )))
      } else {
        expect_true(charport::is_charvec(output))
        expect_false(charport::charport_info(output)$is_materialized)
      }
    }
  } else if (!is.null(output)) {
    if (identical(simplify, FALSE)) {
      expect_true(all(vapply(
        output,
        function(value) !charport::is_charvec(value),
        logical(1)
      )))
    } else {
      expect_false(charport::is_charvec(output))
    }
  }
  invisible(NULL)
}


test_that("regex split preserves vectorized fields and output shapes", {
  malformed <- split_regex_frame_marked(
    c(0x61, 0xff, 0x2c, 0x62), "UTF-8"
  )
  subject <- c(
    "a,,b,c", malformed, "caf\u00e9 \u03b2", "", NA_character_,
    paste0("\ufeff", "a,b")
  )
  pattern <- c(",", "\\ufffd", "\\s+", ".", "x", ",")
  cases <- list(
    list(),
    list(n = 0L),
    list(n = 2L),
    list(n = 3L, omit_empty = TRUE),
    list(n = 3L, omit_empty = NA, tokens_only = TRUE),
    list(n = c(NA_integer_, 2L)),
    list(n = 4L, simplify = TRUE),
    list(n = 4L, simplify = NA),
    list(options = list(case_insensitive = TRUE))
  )

  for (case in cases) {
    args <- modifyList(list(
      n = -1L, omit_empty = FALSE, tokens_only = FALSE,
      simplify = FALSE, options = NULL
    ), case)
    expected <- split_regex_frame_capture(split_regex_frame_invoke(
      "stringi",
      split_regex_frame_inputs("stringi", subject, pattern),
      n = args$n, omit_empty = args$omit_empty,
      tokens_only = args$tokens_only, simplify = args$simplify,
      options = args$options
    ))

    for (backend in c("base", "altrep")) {
      inputs <- split_regex_frame_inputs(backend, subject, pattern)
      actual <- split_regex_frame_capture(split_regex_frame_invoke(
        backend, inputs,
        n = args$n, omit_empty = args$omit_empty,
        tokens_only = args$tokens_only, simplify = args$simplify,
        options = args$options
      ))
      expect_identical(actual$events, expected$events, info = backend)
      expect_split_regex_frame_shape(
        backend, inputs, actual$value, args$simplify
      )
      expect_identical(actual$value, expected$value, info = backend)
    }
  }
})


test_that("regex split strips a subject BOM but preserves a pattern BOM", {
  subject <- c(paste0("a", "\ufeff", "b"), paste0("\ufeff", "b"))
  pattern <- "\ufeff"
  expected <- stringi::stri_split_regex(subject, pattern)

  for (backend in c("base", "altrep")) {
    inputs <- split_regex_frame_inputs(backend, subject, pattern)
    actual <- split_regex_frame_invoke(backend, inputs)
    expect_split_regex_frame_shape(backend, inputs, actual, FALSE)
    expect_identical(actual, expected, info = backend)
  }
})


test_that("regex split preserves validation and warning order", {
  bytes <- split_regex_frame_marked(c(0xff, 0xfe), "bytes")
  scenarios <- list(
    list(
      subject = c("a", "b", "c"), pattern = c("", "["),
      options = structure(list(TRUE), names = "unknown")
    ),
    list(
      subject = bytes, pattern = "",
      options = structure(list(TRUE), names = "unknown")
    ),
    list(subject = "a", pattern = "a", n = .Machine$integer.max)
  )

  for (scenario in scenarios) {
    args <- modifyList(list(
      n = -1L, omit_empty = FALSE, tokens_only = FALSE,
      simplify = FALSE, options = NULL
    ), scenario)
    expected <- split_regex_frame_capture(split_regex_frame_invoke(
      "stringi",
      split_regex_frame_inputs(
        "stringi", args$subject, args$pattern
      ),
      n = args$n, omit_empty = args$omit_empty,
      tokens_only = args$tokens_only, simplify = args$simplify,
      options = args$options
    ))

    for (backend in c("base", "altrep")) {
      inputs <- split_regex_frame_inputs(
        backend, args$subject, args$pattern
      )
      actual <- split_regex_frame_capture(split_regex_frame_invoke(
        backend, inputs,
        n = args$n, omit_empty = args$omit_empty,
        tokens_only = args$tokens_only, simplify = args$simplify,
        options = args$options
      ))
      expect_identical(actual$events, expected$events, info = backend)
      expect_split_regex_frame_shape(
        backend, inputs, actual$value, args$simplify
      )
      expect_identical(actual$value, expected$value, info = backend)
    }
  }
})


test_that("regex split compiles patterns only for lanes that need them", {
  scenarios <- list(
    list(subject = NA_character_, n = -1L),
    list(subject = "", n = -1L),
    list(subject = "abc", n = 0L)
  )

  for (scenario in scenarios) {
    expected <- split_regex_frame_invoke(
      "stringi",
      split_regex_frame_inputs("stringi", scenario$subject, "["),
      n = scenario$n
    )
    for (backend in c("base", "altrep")) {
      inputs <- split_regex_frame_inputs(
        backend, scenario$subject, "["
      )
      actual <- split_regex_frame_invoke(
        backend, inputs, n = scenario$n
      )
      expect_split_regex_frame_shape(backend, inputs, actual, FALSE)
      expect_identical(actual, expected, info = backend)
    }
  }
})


test_that("regex split suppresses input work after zero recycling", {
  bytes <- split_regex_frame_marked(c(0xff, 0xfe), "bytes")
  options <- structure(list(TRUE), names = "unknown")
  for (simplify in list(FALSE, TRUE, NA)) {
    expected <- split_regex_frame_capture(split_regex_frame_invoke(
      "stringi",
      split_regex_frame_inputs("stringi", bytes, character()),
      n = 3L, simplify = simplify, options = options
    ))

    for (backend in c("base", "altrep")) {
      inputs <- split_regex_frame_inputs(
        backend, bytes, character()
      )
      actual <- split_regex_frame_capture(split_regex_frame_invoke(
        backend, inputs, n = 3L, simplify = simplify,
        options = options
      ))
      expect_identical(actual$events, expected$events, info = backend)
      expect_split_regex_frame_shape(
        backend, inputs, actual$value, simplify
      )
      expect_identical(actual$value, expected$value, info = backend)
      if (!identical(simplify, FALSE)) {
        expect_identical(
          dim(actual$value), c(0L, 3L), info = backend
        )
      }
    }
  }
})


test_that("regex split warning errors leave Frames reusable", {
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (backend in c("base", "altrep")) {
    empty <- split_regex_frame_inputs(backend, "a", "")
    expect_error(
      split_regex_frame_invoke(backend, empty),
      "empty search patterns",
      info = backend
    )
    expect_split_regex_frame_shape(backend, empty, NULL, FALSE)

    valid <- split_regex_frame_inputs(
      backend, c("a,a", "b,b"), ","
    )
    actual <- split_regex_frame_invoke(backend, valid, n = 2L)
    expected <- stringi::stri_split_regex(
      c("a,a", "b,b"), ",", n = 2L
    )
    expect_split_regex_frame_shape(backend, valid, actual, FALSE)
    expect_identical(actual, expected, info = backend)
  }
})
