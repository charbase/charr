detect_coll_frame_function <- function(backend) {
  if (identical(backend, "stringi")) {
    return(stringi::stri_detect_coll)
  }
  if (identical(backend, "base")) {
    return(get(
      "ci_detect_coll",
      envir = charr:::.charr_base_leaf_environment,
      inherits = FALSE
    ))
  }
  if (identical(backend, "altrep")) {
    return(charr:::ci_detect_coll)
  }

  stop("unknown collation-detect backend", call. = FALSE)
}


detect_coll_frame_inputs <- function(backend, subject, pattern) {
  if (identical(backend, "altrep")) {
    subject <- charport::as_charvec(subject)
    pattern <- charport::as_charvec(pattern)
  }
  list(subject = subject, pattern = pattern)
}


detect_coll_frame_invoke <- function(
    backend, inputs, negate = FALSE, max_count = -1L,
    opts_collator = NULL
) {
  detect_coll_frame_function(backend)(
    inputs$subject,
    inputs$pattern,
    negate = negate,
    max_count = max_count,
    opts_collator = opts_collator
  )
}


detect_coll_frame_capture <- function(expr, warning_handler = NULL) {
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


detect_coll_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


expect_detect_coll_frame_unmaterialized <- function(backend, inputs) {
  if (!identical(backend, "altrep")) {
    return(invisible(NULL))
  }

  expect_true(charport::is_charvec(inputs$subject))
  expect_false(charport::charport_info(inputs$subject)$is_materialized)
  expect_true(charport::is_charvec(inputs$pattern))
  expect_false(charport::charport_info(inputs$pattern)$is_materialized)
  invisible(NULL)
}


test_that("collation detect preserves matching and encoding semantics", {
  latin1 <- detect_coll_frame_marked(
    c(0x63, 0x61, 0x66, 0xe9), "latin1"
  )
  malformed <- detect_coll_frame_marked(
    c(0x61, 0xff, 0x61), "UTF-8"
  )
  bom_a <- detect_coll_frame_marked(
    c(0xef, 0xbb, 0xbf, 0x61), "UTF-8"
  )

  cases <- list(
    list(
      subject = c("abc abc", "ABC abc", "", NA_character_),
      pattern = "abc",
      negate = FALSE,
      max_count = -1L,
      options = NULL
    ),
    list(
      subject = c("éé", "éé", latin1, malformed, bom_a),
      pattern = c("é", "é", "é", "a", "a"),
      negate = TRUE,
      max_count = -1L,
      options = list(normalization = TRUE)
    ),
    list(
      subject = c(
        "Aarhus", "Århus", "blaa", "blå",
        "none", "", NA_character_, "Aalborg"
      ),
      pattern = c("Å", "aa"),
      negate = FALSE,
      max_count = 2L,
      options = list(locale = "da", strength = 1L)
    ),
    list(
      subject = "banana",
      pattern = c("a", "an", "x"),
      negate = FALSE,
      max_count = -1L,
      options = NULL
    )
  )

  for (case in cases) {
    oracle_inputs <- detect_coll_frame_inputs(
      "stringi", case$subject, case$pattern
    )
    oracle <- detect_coll_frame_invoke(
      "stringi",
      oracle_inputs,
      negate = case$negate,
      max_count = case$max_count,
      opts_collator = case$options
    )

    for (backend in c("base", "altrep")) {
      inputs <- detect_coll_frame_inputs(
        backend, case$subject, case$pattern
      )
      actual <- detect_coll_frame_invoke(
        backend,
        inputs,
        negate = case$negate,
        max_count = case$max_count,
        opts_collator = case$options
      )

      expect_identical(actual, oracle, info = backend)
      expect_detect_coll_frame_unmaterialized(backend, inputs)
    }
  }
})


test_that("collation detect preserves grouped max-count traversal", {
  cases <- list(
    list(
      subject = c("hit", "miss", "hit", "miss", "hit", "miss"),
      pattern = c("hit", "x"),
      negate = FALSE,
      max_count = 1L,
      expected = c(TRUE, NA, NA, NA, NA, NA)
    ),
    list(
      subject = c("hit", "", "miss", "hit"),
      pattern = c("hit", "x"),
      negate = TRUE,
      max_count = 2L,
      expected = c(FALSE, TRUE, TRUE, NA)
    )
  )

  for (case in cases) {
    oracle_inputs <- detect_coll_frame_inputs(
      "stringi", case$subject, case$pattern
    )
    oracle <- detect_coll_frame_invoke(
      "stringi",
      oracle_inputs,
      negate = case$negate,
      max_count = case$max_count
    )
    expect_identical(oracle, case$expected)

    for (backend in c("base", "altrep")) {
      inputs <- detect_coll_frame_inputs(
        backend, case$subject, case$pattern
      )
      actual <- detect_coll_frame_invoke(
        backend,
        inputs,
        negate = case$negate,
        max_count = case$max_count
      )

      expect_identical(actual, oracle, info = backend)
      expect_detect_coll_frame_unmaterialized(backend, inputs)
    }
  }
})


test_that("collation detect preserves staged condition order", {
  bytes <- detect_coll_frame_marked(c(0xff, 0xfe), "bytes")
  scenarios <- list(
    list(
      subject = c(bytes, "a", "a"), pattern = c("a", "b"),
      max_count = -1L
    ),
    list(subject = bytes, pattern = "a", max_count = 0L),
    list(
      subject = c("hit", bytes), pattern = "hit",
      max_count = 1L
    ),
    list(subject = bytes, pattern = "", max_count = 0L),
    list(subject = "abc", pattern = bytes, max_count = -1L),
    list(subject = "abc", pattern = c("", bytes), max_count = -1L),
    list(
      subject = c("a", "b", "a"), pattern = c("", "", "a"),
      max_count = -1L
    )
  )

  for (scenario in scenarios) {
    oracle_inputs <- detect_coll_frame_inputs(
      "stringi", scenario$subject, scenario$pattern
    )
    oracle <- detect_coll_frame_capture(detect_coll_frame_invoke(
      "stringi", oracle_inputs, max_count = scenario$max_count
    ))

    for (backend in c("base", "altrep")) {
      inputs <- detect_coll_frame_inputs(
        backend, scenario$subject, scenario$pattern
      )
      actual <- detect_coll_frame_capture(detect_coll_frame_invoke(
        backend, inputs, max_count = scenario$max_count
      ))

      expect_identical(actual$events, oracle$events, info = backend)
      expect_identical(actual$value, oracle$value, info = backend)
      expect_detect_coll_frame_unmaterialized(backend, inputs)

      valid <- detect_coll_frame_inputs(
        backend, c("alpha", "beta"), "a"
      )
      expect_identical(
        detect_coll_frame_invoke(backend, valid),
        c(TRUE, TRUE),
        info = backend
      )
      expect_detect_coll_frame_unmaterialized(backend, valid)
    }
  }
})


test_that("collation detect does not inspect zero-recycled inputs", {
  bytes <- detect_coll_frame_marked(c(0xff, 0xfe), "bytes")
  cases <- list(
    list(subject = character(), pattern = bytes),
    list(subject = bytes, pattern = character()),
    list(subject = character(), pattern = "")
  )

  for (case in cases) {
    for (backend in c("stringi", "base", "altrep")) {
      inputs <- detect_coll_frame_inputs(
        backend, case$subject, case$pattern
      )
      actual <- detect_coll_frame_capture(
        detect_coll_frame_invoke(backend, inputs)
      )

      expect_identical(actual$value, logical(), info = backend)
      expect_identical(actual$events, character(), info = backend)
      expect_detect_coll_frame_unmaterialized(backend, inputs)
    }
  }
})


test_that("collation detect warning errors leave the Frame reusable", {
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (backend in c("base", "altrep")) {
    invalid <- detect_coll_frame_inputs(backend, "abc", "")
    expect_error(
      detect_coll_frame_invoke(backend, invalid),
      "empty search patterns are not supported",
      fixed = TRUE,
      info = backend
    )
    expect_detect_coll_frame_unmaterialized(backend, invalid)

    valid <- detect_coll_frame_inputs(
      backend, c("alpha", "beta"), "a"
    )
    expect_identical(
      detect_coll_frame_invoke(backend, valid),
      c(TRUE, TRUE),
      info = backend
    )
    expect_detect_coll_frame_unmaterialized(backend, valid)
  }
})


test_that("collation detect warnings permit ALTREP reentry", {
  inputs <- detect_coll_frame_inputs(
    "altrep", c("aaaa", "bbbb", "cccc"), c("a", "b")
  )
  scalar_pattern <- charport::as_charvec("a")
  reentered <- NULL

  actual <- detect_coll_frame_capture(
    detect_coll_frame_invoke("altrep", inputs),
    warning_handler = function(condition) {
      reentered <<- charr:::ci_detect_coll(
        inputs$subject, scalar_pattern
      )
    }
  )

  expect_match(actual$events, "not a multiple", fixed = TRUE)
  expect_identical(actual$value, c(TRUE, TRUE, FALSE))
  expect_identical(reentered, c(TRUE, FALSE, FALSE))
  expect_detect_coll_frame_unmaterialized("altrep", inputs)
  expect_false(charport::charport_info(scalar_pattern)$is_materialized)
})


test_that("collation detect fallback warnings run after native cleanup", {
  fallback <- list(locale = "zz_ZZ")
  oracle_inputs <- detect_coll_frame_inputs("stringi", "alpha", "a")
  oracle <- detect_coll_frame_capture(
    detect_coll_frame_invoke(
      "stringi", oracle_inputs, opts_collator = fallback
    )
  )
  skip_if_not(
    length(oracle$events) == 1L && startsWith(oracle$events, "warning:"),
    "this ICU installation does not warn for the fallback locale"
  )

  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (backend in c("base", "altrep")) {
    inputs <- detect_coll_frame_inputs(backend, "alpha", "a")
    expect_error(
      detect_coll_frame_invoke(
        backend, inputs, opts_collator = fallback
      ),
      info = backend
    )
    expect_detect_coll_frame_unmaterialized(backend, inputs)

    expect_identical(
      detect_coll_frame_invoke(backend, inputs),
      TRUE,
      info = backend
    )
    expect_detect_coll_frame_unmaterialized(backend, inputs)
  }
})
