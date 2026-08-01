count_coll_frame_function <- function(backend) {
  if (identical(backend, "stringi")) {
    return(stringi::stri_count_coll)
  }
  if (identical(backend, "base")) {
    return(get(
      "ci_count_coll",
      envir = charr:::.charr_base_leaf_environment,
      inherits = FALSE
    ))
  }
  if (identical(backend, "altrep")) {
    return(charr:::ci_count_coll)
  }

  stop("unknown collation-count backend", call. = FALSE)
}


count_coll_frame_inputs <- function(backend, subject, pattern) {
  if (identical(backend, "altrep")) {
    subject <- charport::as_charvec(subject)
    pattern <- charport::as_charvec(pattern)
  }
  list(subject = subject, pattern = pattern)
}


count_coll_frame_invoke <- function(backend, inputs, opts_collator = NULL) {
  count_coll_frame_function(backend)(
    inputs$subject, inputs$pattern, opts_collator = opts_collator
  )
}


count_coll_frame_capture <- function(expr, warning_handler = NULL) {
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


count_coll_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


expect_count_coll_frame_unmaterialized <- function(backend, inputs) {
  if (!identical(backend, "altrep")) {
    return(invisible(NULL))
  }

  expect_true(charport::is_charvec(inputs$subject))
  expect_false(charport::charport_info(inputs$subject)$is_materialized)
  expect_true(charport::is_charvec(inputs$pattern))
  expect_false(charport::charport_info(inputs$pattern)$is_materialized)
  invisible(NULL)
}


test_that("collation count preserves matching and encoding semantics", {
  latin1 <- count_coll_frame_marked(c(0x63, 0x61, 0x66, 0xe9), "latin1")
  malformed <- count_coll_frame_marked(c(0x61, 0xff, 0x61), "UTF-8")
  bom_a <- count_coll_frame_marked(c(0xef, 0xbb, 0xbf, 0x61), "UTF-8")

  cases <- list(
    list(
      subject = c("abc abc", "ABC abc", "", NA_character_),
      pattern = "abc",
      options = NULL
    ),
    list(
      subject = c("e\u0301é", "éé", latin1, malformed, bom_a),
      pattern = c("é", "é", "é", "a", "a"),
      options = list(normalization = TRUE)
    ),
    list(
      subject = c("one two one", "banana", "miss", "aaaa", "a"),
      pattern = c("one", "ana", "x", "aa", bom_a),
      options = list(normalization = TRUE)
    ),
    list(
      subject = "banana",
      pattern = c("a", "an", "x"),
      options = NULL
    )
  )

  for (case in cases) {
    oracle_inputs <- count_coll_frame_inputs(
      "stringi", case$subject, case$pattern
    )
    oracle <- count_coll_frame_invoke(
      "stringi", oracle_inputs, case$options
    )

    for (backend in c("base", "altrep")) {
      inputs <- count_coll_frame_inputs(
        backend, case$subject, case$pattern
      )
      actual <- count_coll_frame_invoke(backend, inputs, case$options)

      expect_identical(actual, oracle, info = backend)
      expect_count_coll_frame_unmaterialized(backend, inputs)
    }
  }
})


test_that("collation count preserves staged condition order", {
  bytes <- count_coll_frame_marked(c(0xff, 0xfe), "bytes")
  scenarios <- list(
    list(subject = c(bytes, "a", "a"), pattern = c("a", "b")),
    list(subject = bytes, pattern = ""),
    list(subject = "abc", pattern = bytes),
    list(subject = "abc", pattern = c("", bytes)),
    list(subject = c("a", "b", "a"), pattern = c("", "", "a"))
  )

  for (scenario in scenarios) {
    oracle_inputs <- count_coll_frame_inputs(
      "stringi", scenario$subject, scenario$pattern
    )
    oracle <- count_coll_frame_capture(
      count_coll_frame_invoke("stringi", oracle_inputs)
    )

    for (backend in c("base", "altrep")) {
      inputs <- count_coll_frame_inputs(
        backend, scenario$subject, scenario$pattern
      )
      actual <- count_coll_frame_capture(
        count_coll_frame_invoke(backend, inputs)
      )

      expect_identical(actual$events, oracle$events, info = backend)
      expect_identical(actual$value, oracle$value, info = backend)
      expect_count_coll_frame_unmaterialized(backend, inputs)

      valid <- count_coll_frame_inputs(backend, c("aaaa", ""), "a")
      expect_identical(
        count_coll_frame_invoke(backend, valid),
        c(4L, 0L),
        info = backend
      )
      expect_count_coll_frame_unmaterialized(backend, valid)
    }
  }
})


test_that("collation count does not inspect zero-recycled inputs", {
  bytes <- count_coll_frame_marked(c(0xff, 0xfe), "bytes")
  cases <- list(
    list(subject = character(), pattern = bytes),
    list(subject = bytes, pattern = character()),
    list(subject = character(), pattern = "")
  )

  for (case in cases) {
    for (backend in c("stringi", "base", "altrep")) {
      inputs <- count_coll_frame_inputs(
        backend, case$subject, case$pattern
      )
      actual <- count_coll_frame_capture(
        count_coll_frame_invoke(backend, inputs)
      )

      expect_identical(actual$value, integer(), info = backend)
      expect_identical(actual$events, character(), info = backend)
      expect_count_coll_frame_unmaterialized(backend, inputs)
    }
  }
})


test_that("collation count warning errors leave the Frame reusable", {
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (backend in c("base", "altrep")) {
    invalid <- count_coll_frame_inputs(backend, "abc", "")
    expect_error(
      count_coll_frame_invoke(backend, invalid),
      "empty search patterns are not supported",
      fixed = TRUE,
      info = backend
    )
    expect_count_coll_frame_unmaterialized(backend, invalid)

    valid <- count_coll_frame_inputs(backend, c("aaaa", ""), "a")
    expect_identical(
      count_coll_frame_invoke(backend, valid),
      c(4L, 0L),
      info = backend
    )
    expect_count_coll_frame_unmaterialized(backend, valid)
  }
})


test_that("collation count warnings permit ALTREP reentry", {
  inputs <- count_coll_frame_inputs(
    "altrep", c("aaaa", "bbbb", "cccc"), c("a", "b")
  )
  scalar_pattern <- charport::as_charvec("a")
  reentered <- NULL

  actual <- count_coll_frame_capture(
    count_coll_frame_invoke("altrep", inputs),
    warning_handler = function(condition) {
      reentered <<- charr:::ci_count_coll(inputs$subject, scalar_pattern)
    }
  )

  expect_match(actual$events, "not a multiple", fixed = TRUE)
  expect_identical(actual$value, c(4L, 4L, 0L))
  expect_identical(reentered, c(4L, 0L, 0L))
  expect_count_coll_frame_unmaterialized("altrep", inputs)
  expect_false(charport::charport_info(scalar_pattern)$is_materialized)
})


test_that("collation fallback warnings run after native cleanup", {
  fallback <- list(locale = "zz_ZZ")
  oracle_inputs <- count_coll_frame_inputs("stringi", "alpha", "a")
  oracle <- count_coll_frame_capture(
    count_coll_frame_invoke("stringi", oracle_inputs, fallback)
  )
  skip_if_not(
    length(oracle$events) == 1L && startsWith(oracle$events, "warning:"),
    "this ICU installation does not warn for the fallback locale"
  )

  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (backend in c("base", "altrep")) {
    inputs <- count_coll_frame_inputs(backend, "alpha", "a")
    expect_error(
      count_coll_frame_invoke(backend, inputs, fallback),
      info = backend
    )
    expect_count_coll_frame_unmaterialized(backend, inputs)

    expect_identical(
      count_coll_frame_invoke(backend, inputs),
      2L,
      info = backend
    )
    expect_count_coll_frame_unmaterialized(backend, inputs)
  }
})
