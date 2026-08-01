extract_coll_frame_function <- function(backend, all = FALSE) {
  name <- if (all) "ci_extract_all_coll" else "ci_extract_first_coll"
  if (identical(backend, "stringi")) {
    return(if (all) {
      stringi::stri_extract_all_coll
    } else {
      stringi::stri_extract_first_coll
    })
  }
  if (identical(backend, "base")) {
    return(get(
      name,
      envir = charr:::.charr_base_leaf_environment,
      inherits = FALSE
    ))
  }
  if (identical(backend, "altrep")) {
    return(get(name, envir = asNamespace("charr"), inherits = FALSE))
  }

  stop("unknown collation-extract backend", call. = FALSE)
}


extract_coll_frame_inputs <- function(backend, subject, pattern) {
  if (identical(backend, "altrep")) {
    subject <- charport::as_charvec(subject)
    pattern <- charport::as_charvec(pattern)
  }
  list(subject = subject, pattern = pattern)
}


extract_coll_frame_invoke <- function(
    backend, inputs, all = FALSE, simplify = FALSE,
    omit_no_match = FALSE, opts_collator = NULL
) {
  fun <- extract_coll_frame_function(backend, all = all)
  if (!all) {
    return(fun(
      inputs$subject, inputs$pattern,
      opts_collator = opts_collator
    ))
  }
  fun(
    inputs$subject, inputs$pattern,
    simplify = simplify,
    omit_no_match = omit_no_match,
    opts_collator = opts_collator
  )
}


extract_coll_frame_capture <- function(expr, warning_handler = NULL) {
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


extract_coll_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


expect_extract_coll_frame_inputs_unmaterialized <- function(backend, inputs) {
  if (!identical(backend, "altrep")) {
    return(invisible(NULL))
  }

  expect_true(charport::is_charvec(inputs$subject))
  expect_false(charport::charport_info(inputs$subject)$is_materialized)
  expect_true(charport::is_charvec(inputs$pattern))
  expect_false(charport::charport_info(inputs$pattern)$is_materialized)
  invisible(NULL)
}


expect_extract_coll_frame_output_unmaterialized <- function(value, all) {
  if (!all || !is.list(value)) {
    expect_true(charport::is_charvec(value))
    expect_false(charport::charport_info(value)$is_materialized)
    return(invisible(NULL))
  }

  expect_true(all(vapply(value, charport::is_charvec, logical(1))))
  expect_true(all(vapply(
    value,
    function(element) !charport::charport_info(element)$is_materialized,
    logical(1)
  )))
  invisible(NULL)
}


test_that("collation extraction preserves match text and output shapes", {
  latin1 <- extract_coll_frame_marked(
    c(0x63, 0x61, 0x66, 0xe9), "latin1"
  )
  malformed <- extract_coll_frame_marked(
    c(0x61, 0xff, 0x61), "UTF-8"
  )
  bom_a <- extract_coll_frame_marked(
    c(0xef, 0xbb, 0xbf, 0x61), "UTF-8"
  )
  subject <- c(
    "😀ä-a-A", "u\u0308xÜ", "Aarhus", "none", "",
    NA_character_, latin1, malformed, bom_a
  )
  pattern <- c("a", "ü", "Å", "a", "x", "a", "é", "a", "a")
  options <- list(locale = "de", strength = 1L, normalization = TRUE)

  calls <- list(
    list(all = FALSE, simplify = FALSE, omit = FALSE),
    list(all = TRUE, simplify = FALSE, omit = FALSE),
    list(all = TRUE, simplify = FALSE, omit = TRUE),
    list(all = TRUE, simplify = TRUE, omit = FALSE),
    list(all = TRUE, simplify = NA, omit = FALSE)
  )
  for (call in calls) {
    oracle_inputs <- extract_coll_frame_inputs(
      "stringi", subject, pattern
    )
    oracle <- extract_coll_frame_invoke(
      "stringi", oracle_inputs,
      all = call$all,
      simplify = call$simplify,
      omit_no_match = call$omit,
      opts_collator = options
    )

    for (backend in c("base", "altrep")) {
      inputs <- extract_coll_frame_inputs(backend, subject, pattern)
      actual <- extract_coll_frame_invoke(
        backend, inputs,
        all = call$all,
        simplify = call$simplify,
        omit_no_match = call$omit,
        opts_collator = options
      )

      if (identical(backend, "altrep")) {
        expect_extract_coll_frame_output_unmaterialized(
          actual, call$all
        )
      }
      expect_identical(actual, oracle, info = backend)
      expect_extract_coll_frame_inputs_unmaterialized(backend, inputs)
    }
  }
})


test_that("collation extraction preserves recycling and condition order", {
  bytes <- extract_coll_frame_marked(c(0xff, 0xfe), "bytes")
  scenarios <- list(
    list(subject = c("a", "b", "c"), pattern = c("", "b")),
    list(subject = bytes, pattern = "a"),
    list(subject = "abc", pattern = bytes),
    list(
      subject = "x02", pattern = "x2",
      options = list(numeric = TRUE)
    )
  )

  for (scenario in scenarios) {
    for (all in c(FALSE, TRUE)) {
      oracle_inputs <- extract_coll_frame_inputs(
        "stringi", scenario$subject, scenario$pattern
      )
      oracle <- extract_coll_frame_capture(extract_coll_frame_invoke(
        "stringi", oracle_inputs, all = all,
        opts_collator = scenario$options
      ))

      for (backend in c("base", "altrep")) {
        inputs <- extract_coll_frame_inputs(
          backend, scenario$subject, scenario$pattern
        )
        actual <- extract_coll_frame_capture(extract_coll_frame_invoke(
          backend, inputs, all = all,
          opts_collator = scenario$options
        ))

        expect_identical(actual$events, oracle$events, info = backend)
        expect_identical(actual$value, oracle$value, info = backend)
        expect_extract_coll_frame_inputs_unmaterialized(backend, inputs)
      }
    }
  }
})


test_that("collation extraction does not inspect zero-recycled inputs", {
  bytes <- extract_coll_frame_marked(c(0xff, 0xfe), "bytes")
  cases <- list(
    list(subject = character(), pattern = bytes),
    list(subject = bytes, pattern = character()),
    list(subject = character(), pattern = "")
  )

  for (case in cases) {
    for (all in c(FALSE, TRUE)) {
      for (backend in c("stringi", "base", "altrep")) {
        inputs <- extract_coll_frame_inputs(
          backend, case$subject, case$pattern
        )
        actual <- extract_coll_frame_capture(extract_coll_frame_invoke(
          backend, inputs, all = all
        ))

        expect_identical(actual$events, character(), info = backend)
        expect_length(actual$value, 0)
        expect_extract_coll_frame_inputs_unmaterialized(backend, inputs)
      }
    }
  }
})


test_that("collation extraction warning errors leave the Frame reusable", {
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (backend in c("base", "altrep")) {
    invalid <- extract_coll_frame_inputs(backend, c("a", "b"), "")
    expect_error(
      extract_coll_frame_invoke(backend, invalid, all = TRUE),
      "empty search patterns are not supported",
      fixed = TRUE,
      info = backend
    )
    expect_extract_coll_frame_inputs_unmaterialized(backend, invalid)

    valid <- extract_coll_frame_inputs(backend, c("Aarhus", "blå"), "Å")
    expect_identical(
      extract_coll_frame_invoke(
        backend, valid,
        opts_collator = list(locale = "da", strength = 1L)
      ),
      c("Aa", "å"),
      info = backend
    )
    expect_extract_coll_frame_inputs_unmaterialized(backend, valid)
  }
})


test_that("collation extraction warnings permit ALTREP reentry", {
  inputs <- extract_coll_frame_inputs(
    "altrep", c("Aarhus", "Århus", "blaa"), c("Å", "aa")
  )
  scalar_pattern <- charport::as_charvec("Å")
  reentered <- NULL

  actual <- extract_coll_frame_capture(
    extract_coll_frame_invoke(
      "altrep", inputs,
      opts_collator = list(locale = "da", strength = 1L)
    ),
    warning_handler = function(condition) {
      reentered <<- charr:::ci_extract_first_coll(
        inputs$subject, scalar_pattern,
        opts_collator = list(locale = "da", strength = 1L)
      )
    }
  )

  expect_match(actual$events, "not a multiple", fixed = TRUE)
  expect_identical(actual$value, c("Aa", "Å", "aa"))
  expect_identical(reentered, c("Aa", "Å", "aa"))
  expect_extract_coll_frame_inputs_unmaterialized("altrep", inputs)
  expect_false(charport::charport_info(scalar_pattern)$is_materialized)
})
