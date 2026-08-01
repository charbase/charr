locate_coll_frame_function <- function(backend, all = FALSE) {
  name <- if (all) "ci_locate_all_coll" else "ci_locate_first_coll"
  if (identical(backend, "stringi")) {
    return(if (all) {
      stringi::stri_locate_all_coll
    } else {
      stringi::stri_locate_first_coll
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

  stop("unknown collation-locate backend", call. = FALSE)
}


locate_coll_frame_inputs <- function(backend, subject, pattern) {
  if (identical(backend, "altrep")) {
    subject <- charport::as_charvec(subject)
    pattern <- charport::as_charvec(pattern)
  }
  list(subject = subject, pattern = pattern)
}


locate_coll_frame_invoke <- function(
    backend, inputs, all = FALSE, omit_no_match = FALSE,
    get_length = FALSE, opts_collator = NULL
) {
  fun <- locate_coll_frame_function(backend, all = all)
  if (!all) {
    return(fun(
      inputs$subject, inputs$pattern,
      opts_collator = opts_collator,
      get_length = get_length
    ))
  }
  fun(
    inputs$subject, inputs$pattern,
    omit_no_match = omit_no_match,
    opts_collator = opts_collator,
    get_length = get_length
  )
}


locate_coll_frame_capture <- function(expr, warning_handler = NULL) {
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


locate_coll_frame_marked <- function(bytes, encoding) {
  value <- rawToChar(as.raw(bytes))
  Encoding(value) <- encoding
  value
}


expect_locate_coll_frame_inputs_unmaterialized <- function(backend, inputs) {
  if (!identical(backend, "altrep")) {
    return(invisible(NULL))
  }

  expect_true(charport::is_charvec(inputs$subject))
  expect_false(charport::charport_info(inputs$subject)$is_materialized)
  expect_true(charport::is_charvec(inputs$pattern))
  expect_false(charport::charport_info(inputs$pattern)$is_materialized)
  invisible(NULL)
}


test_that("collation location preserves positions and result shapes", {
  latin1 <- locate_coll_frame_marked(
    c(0x63, 0x61, 0x66, 0xe9), "latin1"
  )
  malformed <- locate_coll_frame_marked(
    c(0x61, 0xff, 0x62), "UTF-8"
  )
  bom_a <- locate_coll_frame_marked(
    c(0xef, 0xbb, 0xbf, 0x61), "UTF-8"
  )
  subject <- c(
    "😀ä-a-A", "u\u0308xÜ", "Aarhus", "none", "",
    NA_character_, latin1, malformed, bom_a
  )
  pattern <- c("a", "ü", "Å", "a", "x", "a", "é", "b", "a")
  options <- list(locale = "de", strength = 1L, normalization = TRUE)

  calls <- list(
    list(all = FALSE, omit = FALSE, length = FALSE),
    list(all = FALSE, omit = FALSE, length = TRUE),
    list(all = TRUE, omit = FALSE, length = FALSE),
    list(all = TRUE, omit = TRUE, length = FALSE),
    list(all = TRUE, omit = FALSE, length = TRUE),
    list(all = TRUE, omit = TRUE, length = TRUE)
  )
  for (call in calls) {
    oracle_inputs <- locate_coll_frame_inputs(
      "stringi", subject, pattern
    )
    oracle <- locate_coll_frame_invoke(
      "stringi", oracle_inputs,
      all = call$all,
      omit_no_match = call$omit,
      get_length = call$length,
      opts_collator = options
    )

    for (backend in c("base", "altrep")) {
      inputs <- locate_coll_frame_inputs(backend, subject, pattern)
      actual <- locate_coll_frame_invoke(
        backend, inputs,
        all = call$all,
        omit_no_match = call$omit,
        get_length = call$length,
        opts_collator = options
      )

      expect_identical(actual, oracle, info = backend)
      expect_locate_coll_frame_inputs_unmaterialized(backend, inputs)
    }
  }
})


test_that("collation location preserves recycling and condition order", {
  bytes <- locate_coll_frame_marked(c(0xff, 0xfe), "bytes")
  scenarios <- list(
    list(subject = c("a", "b", "c"), pattern = c("", "b")),
    list(subject = bytes, pattern = "a"),
    list(subject = "abc", pattern = bytes),
    list(
      subject = "x02", pattern = "x2",
      options = list(numeric = TRUE)
    ),
    list(
      subject = c(bytes, "x", "y"), pattern = c("x", "y"),
      options = list(bogus = TRUE)
    )
  )

  for (scenario in scenarios) {
    for (all in c(FALSE, TRUE)) {
      oracle_inputs <- locate_coll_frame_inputs(
        "stringi", scenario$subject, scenario$pattern
      )
      oracle <- locate_coll_frame_capture(locate_coll_frame_invoke(
        "stringi", oracle_inputs, all = all,
        opts_collator = scenario$options
      ))

      for (backend in c("base", "altrep")) {
        inputs <- locate_coll_frame_inputs(
          backend, scenario$subject, scenario$pattern
        )
        actual <- locate_coll_frame_capture(locate_coll_frame_invoke(
          backend, inputs, all = all,
          opts_collator = scenario$options
        ))

        expect_identical(actual$events, oracle$events, info = backend)
        expect_identical(actual$value, oracle$value, info = backend)
        expect_locate_coll_frame_inputs_unmaterialized(backend, inputs)
      }
    }
  }
})


test_that("collation location does not inspect zero-recycled inputs", {
  bytes <- locate_coll_frame_marked(c(0xff, 0xfe), "bytes")
  cases <- list(
    list(subject = character(), pattern = bytes),
    list(subject = bytes, pattern = character()),
    list(subject = character(), pattern = "")
  )

  for (case in cases) {
    for (all in c(FALSE, TRUE)) {
      for (backend in c("stringi", "base", "altrep")) {
        inputs <- locate_coll_frame_inputs(
          backend, case$subject, case$pattern
        )
        actual <- locate_coll_frame_capture(locate_coll_frame_invoke(
          backend, inputs, all = all
        ))

        expect_identical(actual$events, character(), info = backend)
        expect_length(actual$value, 0)
        expect_locate_coll_frame_inputs_unmaterialized(backend, inputs)
      }
    }
  }
})


test_that("collation location warning errors leave the Frame reusable", {
  old <- options(warn = 2)
  on.exit(options(old), add = TRUE)

  for (backend in c("base", "altrep")) {
    invalid <- locate_coll_frame_inputs(backend, c("a", "b"), "")
    expect_error(
      locate_coll_frame_invoke(backend, invalid, all = TRUE),
      "empty search patterns are not supported",
      fixed = TRUE,
      info = backend
    )
    expect_locate_coll_frame_inputs_unmaterialized(backend, invalid)

    valid <- locate_coll_frame_inputs(backend, c("Aarhus", "blå"), "Å")
    expect_identical(
      locate_coll_frame_invoke(
        backend, valid,
        opts_collator = list(locale = "da", strength = 1L)
      ),
      structure(
        c(1L, 3L, 2L, 3L),
        dim = c(2L, 2L),
        dimnames = list(NULL, c("start", "end"))
      ),
      info = backend
    )
    expect_locate_coll_frame_inputs_unmaterialized(backend, valid)
  }
})


test_that("collation location warnings permit ALTREP reentry", {
  inputs <- locate_coll_frame_inputs(
    "altrep", c("Aarhus", "Århus", "blaa"), c("Å", "aa")
  )
  scalar_pattern <- charport::as_charvec("Å")
  reentered <- NULL

  actual <- locate_coll_frame_capture(
    locate_coll_frame_invoke(
      "altrep", inputs,
      opts_collator = list(locale = "da", strength = 1L)
    ),
    warning_handler = function(condition) {
      reentered <<- charr:::ci_locate_first_coll(
        inputs$subject, scalar_pattern,
        opts_collator = list(locale = "da", strength = 1L)
      )
    }
  )

  expect_true(any(grepl("not a multiple", actual$events, fixed = TRUE)))
  expect_identical(
    actual$value,
    suppressWarnings(stringi::stri_locate_first_coll(
      c("Aarhus", "Århus", "blaa"), c("Å", "aa"),
      opts_collator = list(locale = "da", strength = 1L)
    ))
  )
  expect_identical(
    reentered,
    stringi::stri_locate_first_coll(
      c("Aarhus", "Århus", "blaa"), "Å",
      opts_collator = list(locale = "da", strength = 1L)
    )
  )
  expect_locate_coll_frame_inputs_unmaterialized("altrep", inputs)
  expect_false(charport::charport_info(scalar_pattern)$is_materialized)
})
