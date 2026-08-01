count_fixed_frame_function <- function(backend) {
  if (identical(backend, "stringi")) {
    return(stringi::stri_count_fixed)
  }
  if (identical(backend, "base")) {
    return(get(
      "ci_count_fixed",
      envir = charr:::.charr_base_leaf_environment,
      inherits = FALSE
    ))
  }
  if (identical(backend, "altrep")) {
    return(charr:::ci_count_fixed)
  }

  stop("unknown fixed-count backend", call. = FALSE)
}


count_fixed_frame_inputs <- function(backend, subject, pattern) {
  if (identical(backend, "altrep")) {
    subject <- charport::as_charvec(subject)
    pattern <- charport::as_charvec(pattern)
  }
  list(subject = subject, pattern = pattern)
}


count_fixed_frame_invoke <- function(backend, inputs, opts_fixed = NULL) {
  count_fixed_frame_function(backend)(
    inputs$subject, inputs$pattern, opts_fixed = opts_fixed
  )
}


expect_count_fixed_frame_unmaterialized <- function(backend, inputs) {
  if (!identical(backend, "altrep")) {
    return(invisible(NULL))
  }

  expect_true(charport::is_charvec(inputs$subject))
  expect_false(charport::charport_info(inputs$subject)$is_materialized)
  expect_true(charport::is_charvec(inputs$pattern))
  expect_false(charport::charport_info(inputs$pattern)$is_materialized)
  invisible(NULL)
}


count_fixed_frame_events <- function(expr, warning_handler = NULL) {
  events <- character()
  value <- tryCatch(
    withCallingHandlers(
      force(expr),
      warning = function(condition) {
        events <<- c(events, "warning")
        if (!is.null(warning_handler)) {
          warning_handler(condition)
        }
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


count_fixed_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


test_that("fixed-count Frame paths agree across matcher transitions", {
  latin1 <- count_fixed_frame_marked(c(0x61, 0xe9, 0x61), "latin1")
  malformed <- count_fixed_frame_marked(c(0x61, 0xff, 0x61), "UTF-8")
  bom_a <- count_fixed_frame_marked(c(0xef, 0xbb, 0xbf, 0x61), "UTF-8")
  long_pattern <- "0123456789abcdef"
  cases <- list(
    list(
      subject = c("aaaa", latin1, malformed, NA_character_, ""),
      pattern = "a",
      opts = NULL,
      expected = c(4L, 2L, 2L, NA_integer_, 0L)
    ),
    list(
      subject = c(
        "aaaaa", "abcabcabc", paste0(long_pattern, long_pattern), "aa"
      ),
      pattern = c("a", "abc", long_pattern, bom_a),
      opts = NULL,
      expected = c(5L, 3L, 2L, 2L)
    ),
    list(
      subject = c("AaAa", "ababa", "\u00c9\u00e9\u00c9"),
      pattern = c("aa", "aba", "\u00e9\u00e9"),
      opts = list(case_insensitive = TRUE, overlap = TRUE),
      expected = c(3L, 2L, 2L)
    )
  )

  for (case in cases) {
    oracle_inputs <- count_fixed_frame_inputs(
      "stringi", case$subject, case$pattern
    )
    oracle <- count_fixed_frame_invoke(
      "stringi", oracle_inputs, case$opts
    )
    expect_identical(oracle, case$expected)

    for (backend in c("base", "altrep")) {
      inputs <- count_fixed_frame_inputs(
        backend, case$subject, case$pattern
      )
      actual <- count_fixed_frame_invoke(backend, inputs, case$opts)

      expect_count_fixed_frame_unmaterialized(backend, inputs)
      expect_identical(actual, oracle, info = backend)
    }
  }
})


test_that("fixed-count Frame paths preserve condition order and recover", {
  bytes <- count_fixed_frame_marked(c(0xff, 0xfe), "bytes")
  scenarios <- list(
    list(
      subject = c(bytes, "a", "a"),
      pattern = c("a", "b"),
      events = c("warning", "error")
    ),
    list(subject = bytes, pattern = "", events = "error"),
    list(subject = "abc", pattern = bytes, events = "error")
  )

  for (scenario in scenarios) {
    oracle_inputs <- count_fixed_frame_inputs(
      "stringi", scenario$subject, scenario$pattern
    )
    oracle <- count_fixed_frame_events(
      count_fixed_frame_invoke("stringi", oracle_inputs)
    )
    expect_identical(oracle$events, scenario$events)

    for (backend in c("base", "altrep")) {
      inputs <- count_fixed_frame_inputs(
        backend, scenario$subject, scenario$pattern
      )
      actual <- count_fixed_frame_events(
        count_fixed_frame_invoke(backend, inputs)
      )

      expect_identical(actual$events, oracle$events, info = backend)
      expect_count_fixed_frame_unmaterialized(backend, inputs)

      valid_inputs <- count_fixed_frame_inputs(
        backend, c("aaaa", ""), "a"
      )
      expect_identical(
        count_fixed_frame_invoke(backend, valid_inputs),
        c(4L, 0L),
        info = backend
      )
      expect_count_fixed_frame_unmaterialized(backend, valid_inputs)
    }
  }
})


test_that("fixed-count warning errors leave the Frame reusable", {
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (backend in c("base", "altrep")) {
    invalid <- count_fixed_frame_inputs(backend, "abc", "")
    expect_error(
      count_fixed_frame_invoke(backend, invalid),
      "empty search patterns are not supported",
      fixed = TRUE,
      info = backend
    )
    expect_count_fixed_frame_unmaterialized(backend, invalid)

    valid <- count_fixed_frame_inputs(backend, c("aaaa", ""), "a")
    expect_identical(
      count_fixed_frame_invoke(backend, valid),
      c(4L, 0L),
      info = backend
    )
    expect_count_fixed_frame_unmaterialized(backend, valid)
  }
})


test_that("fixed-count warnings permit reentry on the same charvec", {
  inputs <- count_fixed_frame_inputs(
    "altrep", c("aaaa", "bbbb", "cccc"), c("a", "b")
  )
  scalar_pattern <- charport::as_charvec("a")
  reentered <- NULL

  actual <- count_fixed_frame_events(
    count_fixed_frame_invoke("altrep", inputs),
    warning_handler = function(condition) {
      reentered <<- charr:::ci_count_fixed(inputs$subject, scalar_pattern)
    }
  )

  expect_identical(actual$events, "warning")
  expect_identical(actual$value, c(4L, 4L, 0L))
  expect_identical(reentered, c(4L, 0L, 0L))
  expect_count_fixed_frame_unmaterialized("altrep", inputs)
  expect_false(charport::charport_info(scalar_pattern)$is_materialized)
})


test_that("fixed-count zero recycling does not inspect either input", {
  bytes <- count_fixed_frame_marked(c(0xff, 0xfe), "bytes")
  cases <- list(
    list(subject = character(), pattern = bytes),
    list(subject = bytes, pattern = character())
  )

  for (case in cases) {
    for (backend in c("stringi", "base", "altrep")) {
      inputs <- count_fixed_frame_inputs(
        backend, case$subject, case$pattern
      )
      expect_identical(
        count_fixed_frame_invoke(backend, inputs),
        integer(),
        info = backend
      )
      expect_count_fixed_frame_unmaterialized(backend, inputs)
    }
  }
})


test_that("fixed-count preserves empty-pattern warning multiplicity", {
  subject <- c("a", "b", "a")
  pattern <- c("", "", "a")

  for (backend in c("stringi", "base", "altrep")) {
    inputs <- count_fixed_frame_inputs(backend, subject, pattern)
    actual <- count_fixed_frame_events(
      count_fixed_frame_invoke(backend, inputs)
    )

    expect_identical(actual$events, c("warning", "warning"), info = backend)
    expect_identical(actual$value, c(NA_integer_, NA_integer_, 1L))
    expect_count_fixed_frame_unmaterialized(backend, inputs)
  }
})
