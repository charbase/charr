split_fixed_frame_function <- function(backend) {
  if (identical(backend, "stringi")) {
    return(stringi::stri_split_fixed)
  }
  if (identical(backend, "base")) {
    return(get(
      "ci_split_fixed",
      envir = charr:::.charr_base_leaf_environment,
      inherits = FALSE
    ))
  }
  if (identical(backend, "altrep")) {
    return(get(
      "ci_split_fixed", envir = asNamespace("charr"),
      inherits = FALSE
    ))
  }
  stop("unknown fixed-split backend", call. = FALSE)
}


split_fixed_frame_inputs <- function(backend, subject, pattern) {
  if (identical(backend, "altrep")) {
    subject <- charport::as_charvec(subject)
    pattern <- charport::as_charvec(pattern)
  }
  list(subject = subject, pattern = pattern)
}


split_fixed_frame_invoke <- function(
    backend, inputs, n = -1L, omit_empty = FALSE,
    tokens_only = FALSE, simplify = FALSE, options = NULL
) {
  split_fixed_frame_function(backend)(
    inputs$subject, inputs$pattern, n = n,
    omit_empty = omit_empty, tokens_only = tokens_only,
    simplify = simplify, opts_fixed = options
  )
}


split_fixed_frame_capture <- function(expr) {
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


split_fixed_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


expect_split_fixed_frame_shape <- function(
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


test_that("fixed split preserves edge-case precedence and output shapes", {
  malformed <- split_fixed_frame_marked(
    c(0x61, 0xff, 0x2d, 0x62), "UTF-8"
  )
  malformed_pattern <- split_fixed_frame_marked(
    c(0xff, 0x2d), "UTF-8"
  )
  subject <- c("a--b-", malformed, "café-β", "", NA_character_)
  pattern <- c("-", "-", "é", "x", malformed_pattern)
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
    expected <- split_fixed_frame_capture(split_fixed_frame_invoke(
      "stringi",
      split_fixed_frame_inputs("stringi", subject, pattern),
      n = args$n, omit_empty = args$omit_empty,
      tokens_only = args$tokens_only, simplify = args$simplify,
      options = args$options
    ))

    for (backend in c("base", "altrep")) {
      inputs <- split_fixed_frame_inputs(backend, subject, pattern)
      actual <- split_fixed_frame_capture(split_fixed_frame_invoke(
        backend, inputs,
        n = args$n, omit_empty = args$omit_empty,
        tokens_only = args$tokens_only, simplify = args$simplify,
        options = args$options
      ))
      expect_identical(actual$events, expected$events, info = backend)
      expect_split_fixed_frame_shape(
        backend, inputs, actual$value, args$simplify
      )
      expect_identical(actual$value, expected$value, info = backend)
    }
  }
})


test_that("fixed split preserves eager validation and warning order", {
  bytes <- split_fixed_frame_marked(c(0xff, 0xfe), "bytes")
  scenarios <- list(
    list(subject = bytes, pattern = "x", n = NA_integer_),
    list(
      subject = c("a-a", "b-b", "c-c"),
      pattern = c("", "-"),
      n = c(1L, .Machine$integer.max),
      options = structure(list(TRUE), names = "bogus")
    ),
    list(
      subject = "a-a", pattern = "-",
      n = as.integer(.Machine$integer.max - 1)
    )
  )

  for (scenario in scenarios) {
    args <- modifyList(list(
      n = -1L, omit_empty = FALSE, tokens_only = FALSE,
      simplify = FALSE, options = NULL
    ), scenario)
    expected <- split_fixed_frame_capture(split_fixed_frame_invoke(
      "stringi",
      split_fixed_frame_inputs(
        "stringi", args$subject, args$pattern
      ),
      n = args$n, omit_empty = args$omit_empty,
      tokens_only = args$tokens_only, simplify = args$simplify,
      options = args$options
    ))

    for (backend in c("base", "altrep")) {
      inputs <- split_fixed_frame_inputs(
        backend, args$subject, args$pattern
      )
      actual <- split_fixed_frame_capture(split_fixed_frame_invoke(
        backend, inputs,
        n = args$n, omit_empty = args$omit_empty,
        tokens_only = args$tokens_only, simplify = args$simplify,
        options = args$options
      ))
      expect_identical(actual$events, expected$events, info = backend)
      expect_split_fixed_frame_shape(
        backend, inputs, actual$value, args$simplify
      )
      expect_identical(actual$value, expected$value, info = backend)
    }
  }
})


test_that("fixed split suppresses input work after zero recycling", {
  bytes <- split_fixed_frame_marked(c(0xff, 0xfe), "bytes")
  for (simplify in list(FALSE, TRUE, NA)) {
    expected <- split_fixed_frame_capture(split_fixed_frame_invoke(
      "stringi",
      split_fixed_frame_inputs("stringi", bytes, character()),
      n = 3L, simplify = simplify
    ))

    for (backend in c("base", "altrep")) {
      inputs <- split_fixed_frame_inputs(
        backend, bytes, character()
      )
      actual <- split_fixed_frame_capture(split_fixed_frame_invoke(
        backend, inputs, n = 3L, simplify = simplify
      ))
      expect_identical(actual$events, expected$events, info = backend)
      expect_split_fixed_frame_shape(
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


test_that("fixed split warning errors leave Frames reusable", {
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (backend in c("base", "altrep")) {
    recycled <- split_fixed_frame_inputs(
      backend, c("a", "b", "c"), c("a", "b")
    )
    expect_error(
      split_fixed_frame_invoke(backend, recycled),
      "longer object length",
      info = backend
    )
    expect_split_fixed_frame_shape(backend, recycled, NULL, FALSE)

    empty <- split_fixed_frame_inputs(backend, "a", "")
    expect_error(
      split_fixed_frame_invoke(backend, empty),
      "empty search patterns",
      info = backend
    )
    expect_split_fixed_frame_shape(backend, empty, NULL, FALSE)

    valid <- split_fixed_frame_inputs(
      backend, c("a-a", "b-b"), "-"
    )
    actual <- split_fixed_frame_invoke(backend, valid, n = 2L)
    expected <- stringi::stri_split_fixed(
      c("a-a", "b-b"), "-", n = 2L
    )
    expect_split_fixed_frame_shape(backend, valid, actual, FALSE)
    expect_identical(actual, expected, info = backend)
  }
})
