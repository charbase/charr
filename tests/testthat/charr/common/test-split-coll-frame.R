split_coll_frame_function <- function(backend) {
  if (identical(backend, "stringi")) {
    return(stringi::stri_split_coll)
  }
  if (identical(backend, "base")) {
    return(get(
      "ci_split_coll",
      envir = charr:::.charr_base_leaf_environment,
      inherits = FALSE
    ))
  }
  if (identical(backend, "altrep")) {
    return(get(
      "ci_split_coll", envir = asNamespace("charr"),
      inherits = FALSE
    ))
  }
  stop("unknown collation-split backend", call. = FALSE)
}


split_coll_frame_inputs <- function(backend, subject, pattern) {
  if (identical(backend, "altrep")) {
    subject <- charport::as_charvec(subject)
    pattern <- charport::as_charvec(pattern)
  }
  list(subject = subject, pattern = pattern)
}


split_coll_frame_invoke <- function(
    backend, inputs, n = -1L, omit_empty = FALSE,
    tokens_only = FALSE, simplify = FALSE, options = NULL
) {
  split_coll_frame_function(backend)(
    inputs$subject, inputs$pattern, n = n,
    omit_empty = omit_empty, tokens_only = tokens_only,
    simplify = simplify, opts_collator = options
  )
}


split_coll_frame_capture <- function(expr) {
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


split_coll_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


expect_split_coll_frame_shape <- function(
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


test_that("collation split preserves fields, limits, and output shapes", {
  subject <- c(
    "😀äAäB", "üÜx",
    "åaaÅ", "none", "", NA_character_, "-a--b-"
  )
  pattern <- c("a", "ü", "å", "a", "x", "a", "-")
  options <- list(locale = "de", strength = 1L)
  cases <- list(
    list(),
    list(n = 0L),
    list(n = 2L),
    list(n = 2L, tokens_only = TRUE),
    list(n = 3L, omit_empty = TRUE, tokens_only = TRUE),
    list(n = c(3L, 1L), omit_empty = TRUE),
    list(n = 3L, omit_empty = NA),
    list(n = c(NA_integer_, 2L)),
    list(n = 2L, simplify = TRUE),
    list(n = 2L, simplify = NA)
  )

  for (case in cases) {
    args <- modifyList(list(
      n = -1L, omit_empty = FALSE, tokens_only = FALSE,
      simplify = FALSE
    ), case)
    oracle_inputs <- split_coll_frame_inputs(
      "stringi", subject, pattern
    )
    expected <- split_coll_frame_capture(split_coll_frame_invoke(
      "stringi", oracle_inputs,
      n = args$n, omit_empty = args$omit_empty,
      tokens_only = args$tokens_only, simplify = args$simplify,
      options = options
    ))

    for (backend in c("base", "altrep")) {
      inputs <- split_coll_frame_inputs(backend, subject, pattern)
      actual <- split_coll_frame_capture(split_coll_frame_invoke(
        backend, inputs,
        n = args$n, omit_empty = args$omit_empty,
        tokens_only = args$tokens_only, simplify = args$simplify,
        options = options
      ))

      expect_identical(actual$events, expected$events, info = backend)
      expect_split_coll_frame_shape(
        backend, inputs, actual$value, args$simplify
      )
      expect_identical(actual$value, expected$value, info = backend)
    }
  }
})


test_that("collation split preserves encoding and special-value precedence", {
  latin1 <- split_coll_frame_marked(
    c(0x63, 0x61, 0x66, 0xe9), "latin1"
  )
  malformed <- split_coll_frame_marked(
    c(0x61, 0xff, 0x62), "UTF-8"
  )
  bytes <- split_coll_frame_marked(c(0xff, 0xfe), "bytes")
  cases <- list(
    list(
      subject = c(latin1, "﻿apple", malformed, NA_character_),
      pattern = c("é", "a", "�", "a"),
      n = 2L
    ),
    list(subject = c("", NA_character_), pattern = "", n = 0L),
    list(
      subject = c("", NA_character_), pattern = c("x", NA_character_),
      n = .Machine$integer.max, omit_empty = NA
    ),
    list(subject = bytes, pattern = "x", n = NA_integer_),
    list(subject = "x", pattern = bytes, n = 0L)
  )

  for (case in cases) {
    args <- modifyList(list(
      n = -1L, omit_empty = FALSE, tokens_only = FALSE,
      simplify = FALSE, options = NULL
    ), case)
    expected <- split_coll_frame_capture(split_coll_frame_invoke(
      "stringi",
      split_coll_frame_inputs(
        "stringi", args$subject, args$pattern
      ),
      n = args$n, omit_empty = args$omit_empty,
      tokens_only = args$tokens_only, simplify = args$simplify,
      options = args$options
    ))

    for (backend in c("base", "altrep")) {
      inputs <- split_coll_frame_inputs(
        backend, args$subject, args$pattern
      )
      actual <- split_coll_frame_capture(split_coll_frame_invoke(
        backend, inputs,
        n = args$n, omit_empty = args$omit_empty,
        tokens_only = args$tokens_only, simplify = args$simplify,
        options = args$options
      ))
      expect_identical(actual$events, expected$events, info = backend)
      expect_split_coll_frame_shape(
        backend, inputs, actual$value, args$simplify
      )
      expect_identical(actual$value, expected$value, info = backend)
      if (!is.null(actual$value) && identical(args$simplify, FALSE)) {
        expect_identical(
          lapply(actual$value, Encoding),
          lapply(expected$value, Encoding),
          info = backend
        )
      }
    }
  }
})


test_that("collation split keeps matcher text stable while patterns change", {
  subject <- c(
    paste0(strrep("a-", 4096L), "z"),
    "b-c",
    paste0(strrep("x-", 16384L), "z"),
    "d-e"
  )
  pattern <- c("-", "b", "-", "d")
  expected <- split_coll_frame_invoke(
    "stringi",
    split_coll_frame_inputs("stringi", subject, pattern),
    n = 2L
  )

  for (backend in c("base", "altrep")) {
    inputs <- split_coll_frame_inputs(backend, subject, pattern)
    actual <- split_coll_frame_invoke(backend, inputs, n = 2L)
    expect_split_coll_frame_shape(backend, inputs, actual, FALSE)
    expect_identical(actual, expected, info = backend)
  }
})


test_that("collation split preserves warning and error order", {
  scenarios <- list(
    list(
      subject = c("a", "b", "c"),
      pattern = c("", "a"),
      n = c(1L, .Machine$integer.max),
      options = structure(list(TRUE), names = "bogus")
    ),
    list(
      subject = c("a", "b", "c"),
      pattern = c("", ""),
      n = 2L
    ),
    list(
      subject = "a-a",
      pattern = "-",
      n = as.integer(.Machine$integer.max - 1)
    )
  )

  for (scenario in scenarios) {
    expected <- split_coll_frame_capture(split_coll_frame_invoke(
      "stringi",
      split_coll_frame_inputs(
        "stringi", scenario$subject, scenario$pattern
      ),
      n = scenario$n, options = scenario$options
    ))
    for (backend in c("base", "altrep")) {
      inputs <- split_coll_frame_inputs(
        backend, scenario$subject, scenario$pattern
      )
      actual <- split_coll_frame_capture(split_coll_frame_invoke(
        backend, inputs, n = scenario$n,
        options = scenario$options
      ))
      expect_identical(actual$events, expected$events, info = backend)
      expect_split_coll_frame_shape(
        backend, inputs, actual$value, FALSE
      )
      expect_identical(actual$value, expected$value, info = backend)
    }
  }

  empty_expected <- split_coll_frame_capture(split_coll_frame_invoke(
    "stringi",
    split_coll_frame_inputs("stringi", character(), ""),
    n = 3L, simplify = TRUE,
    options = structure(list(TRUE), names = "bogus")
  ))
  for (backend in c("base", "altrep")) {
    inputs <- split_coll_frame_inputs(backend, character(), "")
    actual <- split_coll_frame_capture(split_coll_frame_invoke(
      backend, inputs, n = 3L, simplify = TRUE,
      options = structure(list(TRUE), names = "bogus")
    ))
    expect_identical(actual$events, empty_expected$events, info = backend)
    expect_split_coll_frame_shape(
      backend, inputs, actual$value, TRUE
    )
    expect_identical(actual$value, empty_expected$value, info = backend)
    expect_identical(dim(actual$value), c(0L, 3L), info = backend)
  }
})


test_that("collation split warning errors leave Frames reusable", {
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (backend in c("base", "altrep")) {
    recycled <- split_coll_frame_inputs(
      backend, c("a", "b", "c"), c("a", "b")
    )
    expect_error(
      split_coll_frame_invoke(backend, recycled),
      "longer object length",
      info = backend
    )
    expect_split_coll_frame_shape(backend, recycled, NULL, FALSE)

    empty <- split_coll_frame_inputs(backend, "a", "")
    expect_error(
      split_coll_frame_invoke(backend, empty),
      "empty search patterns",
      info = backend
    )
    expect_split_coll_frame_shape(backend, empty, NULL, FALSE)

    valid <- split_coll_frame_inputs(
      backend, c("a-a", "b-b"), "-"
    )
    actual <- split_coll_frame_invoke(backend, valid, n = 2L)
    expected <- stringi::stri_split_coll(
      c("a-a", "b-b"), "-", n = 2L
    )
    expect_split_coll_frame_shape(backend, valid, actual, FALSE)
    expect_identical(actual, expected, info = backend)
  }
})
