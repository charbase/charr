startsends_coll_frame_function <- function(backend, operation) {
  leaf <- switch(
    operation,
    starts = "ci_startswith_coll",
    ends = "ci_endswith_coll",
    stop("unknown collation-position operation", call. = FALSE)
  )

  if (identical(backend, "stringi")) {
    return(getExportedValue("stringi", sub("^ci_", "stri_", leaf)))
  }
  if (identical(backend, "base")) {
    return(get(
      leaf,
      envir = charr:::.charr_base_leaf_environment,
      inherits = FALSE
    ))
  }
  if (identical(backend, "altrep")) {
    return(get(leaf, envir = asNamespace("charr"), inherits = FALSE))
  }

  stop("unknown collation-position backend", call. = FALSE)
}


startsends_coll_frame_inputs <- function(backend, subject, pattern) {
  if (identical(backend, "altrep")) {
    subject <- charport::as_charvec(subject)
    pattern <- charport::as_charvec(pattern)
  }
  list(subject = subject, pattern = pattern)
}


startsends_coll_frame_invoke <- function(
    backend, operation, inputs, position,
    negate = FALSE, opts_collator = NULL
) {
  fun <- startsends_coll_frame_function(backend, operation)
  if (identical(operation, "starts")) {
    return(fun(
      inputs$subject, inputs$pattern, from = position,
      negate = negate, opts_collator = opts_collator
    ))
  }
  fun(
    inputs$subject, inputs$pattern, to = position,
    negate = negate, opts_collator = opts_collator
  )
}


startsends_coll_frame_capture <- function(expr, warning_handler = NULL) {
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


startsends_coll_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


startsends_coll_frame_default_position <- function(operation) {
  if (identical(operation, "starts")) 1L else -1L
}


expect_startsends_coll_frame_unmaterialized <- function(backend, inputs) {
  if (!identical(backend, "altrep")) {
    return(invisible(NULL))
  }

  expect_true(charport::is_charvec(inputs$subject))
  expect_false(charport::charport_info(inputs$subject)$is_materialized)
  expect_true(charport::is_charvec(inputs$pattern))
  expect_false(charport::charport_info(inputs$pattern)$is_materialized)
  invisible(NULL)
}


expect_startsends_coll_frame_parity <- function(
    operation, subject, pattern, position,
    opts_collator = NULL, negate = FALSE
) {
  oracle_inputs <- startsends_coll_frame_inputs(
    "stringi", subject, pattern
  )
  oracle <- startsends_coll_frame_invoke(
    "stringi", operation, oracle_inputs, position,
    negate = negate, opts_collator = opts_collator
  )

  for (backend in c("base", "altrep")) {
    inputs <- startsends_coll_frame_inputs(backend, subject, pattern)
    actual <- startsends_coll_frame_invoke(
      backend, operation, inputs, position,
      negate = negate, opts_collator = opts_collator
    )
    expect_identical(actual, oracle, info = paste(backend, operation))
    expect_startsends_coll_frame_unmaterialized(backend, inputs)
  }
  invisible(oracle)
}


expect_startsends_coll_frame_event_parity <- function(
    operation, subject, pattern, position, opts_collator = NULL
) {
  oracle_inputs <- startsends_coll_frame_inputs(
    "stringi", subject, pattern
  )
  oracle <- startsends_coll_frame_capture(
    startsends_coll_frame_invoke(
      "stringi", operation, oracle_inputs, position,
      opts_collator = opts_collator
    )
  )

  for (backend in c("base", "altrep")) {
    inputs <- startsends_coll_frame_inputs(backend, subject, pattern)
    actual <- startsends_coll_frame_capture(
      startsends_coll_frame_invoke(
        backend, operation, inputs, position,
        opts_collator = opts_collator
      )
    )
    expect_identical(actual$events, oracle$events, info = paste(backend, operation))
    expect_identical(actual$value, oracle$value, info = paste(backend, operation))
    expect_startsends_coll_frame_unmaterialized(backend, inputs)

    valid <- startsends_coll_frame_inputs(
      backend, c("alpha", "beta"), c("a", "b")
    )
    expect_identical(
      startsends_coll_frame_invoke(
        backend, "starts", valid, 1L
      ),
      c(TRUE, TRUE),
      info = backend
    )
    expect_startsends_coll_frame_unmaterialized(backend, valid)
  }
  invisible(oracle)
}


test_that("collation starts and ends preserve matching and encodings", {
  latin1 <- startsends_coll_frame_marked(
    c(0x61, 0xe9, 0x61), "latin1"
  )
  malformed <- startsends_coll_frame_marked(
    c(0x61, 0xff, 0x61), "UTF-8"
  )
  bom_a <- startsends_coll_frame_marked(
    c(0xef, 0xbb, 0xbf, 0x61), "UTF-8"
  )
  subjects <- c("aba", latin1, malformed, bom_a, "", NA_character_)
  options <- list(locale = "de", strength = 1L)

  for (operation in c("starts", "ends")) {
    position <- startsends_coll_frame_default_position(operation)
    for (negate in c(FALSE, TRUE)) {
      expect_startsends_coll_frame_parity(
        operation, subjects, "a", position,
        opts_collator = options, negate = negate
      )
    }

    expect_startsends_coll_frame_parity(
      operation,
      "a\u00e9\U0001f600bc\u00e4",
      c("a", "\u00e9", "\U0001f600", "bc", "\u00e4"),
      position,
      opts_collator = options
    )
  }
})


test_that("collation starts and ends use code-point positions", {
  subject <- rep("a\u00e9\U0001f600bc\u00e4", 12L)
  pattern <- c(
    "a", "\u00e9", "\U0001f600", "b", "c", "a",
    "\u00e4", "x", "a", "a", "\u00e9\U0001f600", "bc\u00e4"
  )
  positions <- list(
    starts = c(
      1L, 2L, 3L, -3L, 5L, 9L,
      -1L, NA_integer_, 0L, -9L,
      .Machine$integer.max, -.Machine$integer.max
    ),
    ends = c(
      1L, 2L, 3L, -3L, 5L, 0L,
      -1L, NA_integer_, 99L, -99L,
      .Machine$integer.max, -.Machine$integer.max
    )
  )
  options <- list(locale = "de", strength = 1L)

  for (operation in names(positions)) {
    for (negate in c(FALSE, TRUE)) {
      expect_startsends_coll_frame_parity(
        operation, subject, pattern, positions[[operation]],
        opts_collator = options, negate = negate
      )
    }
  }
})


test_that("collation starts and ends preserve condition order", {
  bytes <- startsends_coll_frame_marked(c(0xff, 0xfe), "bytes")

  for (operation in c("starts", "ends")) {
    default <- startsends_coll_frame_default_position(operation)
    warning_positions <- if (identical(operation, "starts")) {
      c(1L, 2L)
    } else {
      c(-1L, -2L)
    }

    expect_startsends_coll_frame_event_parity(
      operation,
      c("a", "b", "a"), c("", "", "a"), warning_positions,
      opts_collator = list(bogus = TRUE)
    )
    expect_startsends_coll_frame_event_parity(
      operation, c(bytes, "a", "a"), c("a", "b"), default
    )
    expect_startsends_coll_frame_event_parity(
      operation, "abc", bytes, NA_integer_
    )
    expect_startsends_coll_frame_event_parity(
      operation, bytes, "a",
      if (identical(operation, "starts")) 99L else 0L
    )
    expect_startsends_coll_frame_event_parity(
      operation, c("a", "x02"), c("", "x2"), default,
      opts_collator = list(numeric = TRUE)
    )
  }
})


test_that("collation starts and ends do not inspect zero-recycled inputs", {
  bytes <- startsends_coll_frame_marked(c(0xff, 0xfe), "bytes")
  cases <- list(
    list(subject = character(), pattern = bytes, position = 1L),
    list(subject = bytes, pattern = character(), position = 1L),
    list(subject = bytes, pattern = "a", position = integer())
  )

  for (operation in c("starts", "ends")) {
    for (case in cases) {
      for (backend in c("stringi", "base", "altrep")) {
        inputs <- startsends_coll_frame_inputs(
          backend, case$subject, case$pattern
        )
        actual <- startsends_coll_frame_capture(
          startsends_coll_frame_invoke(
            backend, operation, inputs, case$position
          )
        )

        expect_identical(actual$value, logical(), info = paste(backend, operation))
        expect_identical(actual$events, character(), info = paste(backend, operation))
        expect_startsends_coll_frame_unmaterialized(backend, inputs)
      }
    }
  }
})


test_that("collation starts and ends warning errors leave the Frame reusable", {
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (operation in c("starts", "ends")) {
    position <- startsends_coll_frame_default_position(operation)
    for (backend in c("base", "altrep")) {
      invalid <- startsends_coll_frame_inputs(backend, "abc", "")
      expect_error(
        startsends_coll_frame_invoke(
          backend, operation, invalid, position
        ),
        "empty search patterns are not supported",
        fixed = TRUE,
        info = paste(backend, operation)
      )
      expect_startsends_coll_frame_unmaterialized(backend, invalid)

      valid <- startsends_coll_frame_inputs(
        backend, c("alpha", "beta"), c("a", "b")
      )
      expect_identical(
        startsends_coll_frame_invoke(
          backend, "starts", valid, 1L
        ),
        c(TRUE, TRUE),
        info = backend
      )
      expect_startsends_coll_frame_unmaterialized(backend, valid)
    }
  }
})


test_that("collation starts and ends warnings permit ALTREP reentry", {
  for (operation in c("starts", "ends")) {
    inputs <- startsends_coll_frame_inputs(
      "altrep", c("aaaa", "bbbb", "cccc"), c("a", "b")
    )
    scalar_pattern <- charport::as_charvec("a")
    reentered <- NULL
    position <- startsends_coll_frame_default_position(operation)

    actual <- startsends_coll_frame_capture(
      startsends_coll_frame_invoke(
        "altrep", operation, inputs, position
      ),
      warning_handler = function(condition) {
        reentered <<- charr:::ci_startswith_coll(
          inputs$subject, scalar_pattern
        )
      }
    )

    expect_match(actual$events, "not a multiple", fixed = TRUE)
    expect_identical(reentered, c(TRUE, FALSE, FALSE))
    expect_startsends_coll_frame_unmaterialized("altrep", inputs)
    expect_false(charport::charport_info(scalar_pattern)$is_materialized)
  }
})


test_that("collation starts and ends fallback warnings follow cleanup", {
  fallback <- list(locale = "zz_ZZ")
  oracle_inputs <- startsends_coll_frame_inputs(
    "stringi", "alpha", "a"
  )
  oracle <- startsends_coll_frame_capture(
    startsends_coll_frame_invoke(
      "stringi", "starts", oracle_inputs, 1L,
      opts_collator = fallback
    )
  )
  skip_if_not(
    length(oracle$events) == 1L && startsWith(oracle$events, "warning:"),
    "this ICU installation does not warn for the fallback locale"
  )

  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (operation in c("starts", "ends")) {
    position <- startsends_coll_frame_default_position(operation)
    for (backend in c("base", "altrep")) {
      inputs <- startsends_coll_frame_inputs(backend, "alpha", "a")
      expect_error(
        startsends_coll_frame_invoke(
          backend, operation, inputs, position,
          opts_collator = fallback
        ),
        info = paste(backend, operation)
      )
      expect_startsends_coll_frame_unmaterialized(backend, inputs)

      expect_identical(
        startsends_coll_frame_invoke(
          backend, operation, inputs, position
        ),
        TRUE,
        info = paste(backend, operation)
      )
      expect_startsends_coll_frame_unmaterialized(backend, inputs)
    }
  }
})
